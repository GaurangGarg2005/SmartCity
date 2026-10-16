#!/usr/bin/env python3
"""
tools/build_graph.py — SmartCity Phase 1 Data Pipeline

Downloads drivable roads from OpenStreetMap via Overpass API for the
Noida + Greater Noida bounding box. Builds a simplified directed graph:
  - Intersection nodes and dead-end nodes only
  - Degree-2 nodes contracted (geometry preserved on edges)
  - Largest strongly connected component (Tarjan SCC)
  - Compact binary output: data/graph.bin
  - Stats report: docs/graph_stats.md

Usage:
    python tools/build_graph.py [--bbox W,S,E,N] [--out data/graph.bin]
                                [--stats docs/graph_stats.md] [--cache data/osm_cache.xml]
                                [--signal-delay 8] [--no-cache]

Overpass usage notes:
  - One HTTP request, polite User-Agent, result cached locally.
  - Re-run with --no-cache to force fresh download.
  - Respects Overpass fair use (large bbox → large timeout).
"""

import argparse
import collections
import math
import os
import re
import struct
import sys
import time
import xml.etree.ElementTree as ET
from typing import Dict, List, Optional, Set, Tuple

try:
    import requests
except ImportError:
    sys.exit("Missing: pip install requests")

# ─── Constants ────────────────────────────────────────────────────────────────

OVERPASS_URL = "https://overpass-api.de/api/interpreter"
USER_AGENT   = "SmartCity-academic-graph-builder/1.0 (JIIT Noida student project; contact via GitHub)"

# Road classes to include (drivable only)
ROAD_CLASSES = {
    "motorway":       0,
    "motorway_link":  0,
    "trunk":          1,
    "trunk_link":     1,
    "primary":        2,
    "primary_link":   2,
    "secondary":      3,
    "secondary_link": 3,
    "tertiary":       4,
    "tertiary_link":  4,
    "unclassified":   5,
    "residential":    6,
    "living_street":  7,
}

# India-appropriate free-flow speeds (km/h) per road class
DEFAULT_SPEED_KMH = {
    0: 100,  # motorway
    1: 80,   # trunk
    2: 60,   # primary
    3: 50,   # secondary
    4: 40,   # tertiary
    5: 30,   # unclassified
    6: 20,   # residential
    7: 10,   # living_street
}

# Default lane count per road class
DEFAULT_LANES = {
    0: 3, 1: 2, 2: 2, 3: 2, 4: 1, 5: 1, 6: 1, 7: 1,
}

# Intersection delay (seconds) added to edge travel time
SIGNAL_DELAY_S_DEFAULT = 8    # signalised intersection
UNCONTROLLED_DELAY_S   = 2    # unsignalised

# Binary format constants
MAGIC   = b"SCGR"   # SmartCity GRaph
VERSION = 1

# ─── Helpers ──────────────────────────────────────────────────────────────────

def kmh_to_ms(kmh: float) -> float:
    return kmh / 3.6

def haversine_m(lat1, lon1, lat2, lon2) -> float:
    """Great-circle distance in metres."""
    R = 6_371_000.0
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlam = math.radians(lon2 - lon1)
    a = math.sin(dphi/2)**2 + math.cos(phi1)*math.cos(phi2)*math.sin(dlam/2)**2
    return 2 * R * math.asin(math.sqrt(a))

def parse_maxspeed(tag: str) -> Optional[float]:
    """Return speed in km/h or None."""
    if not tag:
        return None
    tag = tag.strip().lower()
    if tag in ("none", "unlimited", "signals", "variable"):
        return None
    # Handle "mph" suffix
    m = re.match(r"(\d+(?:\.\d+)?)\s*mph", tag)
    if m:
        return float(m.group(1)) * 1.60934
    # Handle plain number (assumed kph)
    m = re.match(r"(\d+(?:\.\d+)?)", tag)
    if m:
        return float(m.group(1))
    return None

# ─── Overpass download ────────────────────────────────────────────────────────

def build_overpass_query(bbox: str) -> str:
    w, s, e, n = bbox.split(",")
    # south,west,north,east is the Overpass bbox order
    area = f"{s},{w},{n},{e}"
    road_types = "|".join(ROAD_CLASSES.keys())
    return f"""
[out:xml][timeout:180];
(
  way["highway"~"^({road_types})$"]
     ["access"!~"^(private|no)$"]
     ({area});
);
out body;
>;
out skel qt;
""".strip()


def download_osm(bbox: str, cache_path: str, force: bool = False) -> ET.Element:
    if not force and os.path.exists(cache_path):
        size_mb = os.path.getsize(cache_path) / 1e6
        print(f"[overpass] Using cached OSM data: {cache_path} ({size_mb:.1f} MB)")
        return ET.parse(cache_path).getroot()

    query = build_overpass_query(bbox)
    print(f"[overpass] Downloading from {OVERPASS_URL} …")
    print(f"[overpass] bbox = {bbox}")

    resp = requests.post(
        OVERPASS_URL,
        data={"data": query},
        headers={"User-Agent": USER_AGENT},
        timeout=300,
        stream=True,
    )
    resp.raise_for_status()

    os.makedirs(os.path.dirname(cache_path) or ".", exist_ok=True)
    total = 0
    with open(cache_path, "wb") as f:
        for chunk in resp.iter_content(chunk_size=65536):
            f.write(chunk)
            total += len(chunk)
            if total % (5 * 1024 * 1024) < 65536:
                print(f"  … {total / 1e6:.1f} MB received")

    size_mb = total / 1e6
    print(f"[overpass] Downloaded {size_mb:.1f} MB -> {cache_path}")
    return ET.parse(cache_path).getroot()

# ─── OSM parsing ──────────────────────────────────────────────────────────────

def parse_osm(root: ET.Element, signal_delay: float, bbox: str = None):
    """
    Returns:
        nodes_latlon: {osm_node_id: (lat, lon)}
        edges_raw: list of dicts (from_id, to_id, length_m, speed_ms, lanes,
                                  road_class, oneway, name, ref, geometry,
                                  has_signal)
    """
    # Parse bbox filter
    bbox_filter = None
    if bbox:
        w, s, e, n = [float(x) for x in bbox.split(",")]
        bbox_filter = (w, s, e, n)

    # 1. Index all OSM nodes (filtered by bbox if provided)
    nodes_latlon: Dict[int, Tuple[float, float]] = {}
    signal_nodes: Set[int] = set()

    for node in root.iter("node"):
        nid = int(node.attrib["id"])
        lat = float(node.attrib["lat"])
        lon = float(node.attrib["lon"])
        # Apply bbox filter
        if bbox_filter:
            bw, bs, be, bn = bbox_filter
            if not (bs <= lat <= bn and bw <= lon <= be):
                continue
        nodes_latlon[nid] = (lat, lon)
        # Traffic signals
        for tag in node:
            if tag.tag == "tag" and tag.attrib.get("k") == "highway" and tag.attrib.get("v") == "traffic_signals":
                signal_nodes.add(nid)

    print(f"[parse] OSM nodes in bbox: {len(nodes_latlon):,}  |  signal nodes: {len(signal_nodes):,}")

    # 2. Parse ways
    edges_raw = []
    way_count = 0

    for way in root.iter("way"):
        tags: Dict[str, str] = {t.attrib["k"]: t.attrib["v"] for t in way if t.tag == "tag"}

        highway = tags.get("highway", "")
        if highway not in ROAD_CLASSES:
            continue

        # Access restriction
        access = tags.get("access", "")
        if access in ("private", "no"):
            continue
        motor = tags.get("motor_vehicle", tags.get("motorcar", ""))
        if motor in ("private", "no"):
            continue

        road_class = ROAD_CLASSES[highway]

        # Node refs
        nd_ids = [int(nd.attrib["ref"]) for nd in way if nd.tag == "nd"]
        nd_ids = [nid for nid in nd_ids if nid in nodes_latlon]
        if len(nd_ids) < 2:
            continue

        # Speed
        maxspeed_raw = tags.get("maxspeed", "")
        speed_kmh = parse_maxspeed(maxspeed_raw) or DEFAULT_SPEED_KMH[road_class]
        speed_ms  = kmh_to_ms(speed_kmh)

        # Lanes
        try:
            lanes = int(tags.get("lanes", DEFAULT_LANES[road_class]))
        except ValueError:
            lanes = DEFAULT_LANES[road_class]

        # Name / ref
        name = tags.get("name", "")
        ref  = tags.get("ref", "")

        # Oneway
        oneway_tag = tags.get("oneway", "no")
        junction   = tags.get("junction", "")
        if junction in ("roundabout", "circular"):
            oneway_tag = "yes"

        # Determine forward / backward edges
        if oneway_tag in ("yes", "1", "true"):
            directions = [(nd_ids, False)]           # forward only
        elif oneway_tag == "-1":
            directions = [(nd_ids[::-1], False)]     # reversed only
        else:
            directions = [(nd_ids, False), (nd_ids[::-1], False)]  # both

        for seq, _ in directions:
            # Split into individual edges between consecutive nodes
            for i in range(len(seq) - 1):
                u, v = seq[i], seq[i + 1]
                if u == v:
                    continue
                lat_u, lon_u = nodes_latlon[u]
                lat_v, lon_v = nodes_latlon[v]
                dist = haversine_m(lat_u, lon_u, lat_v, lon_v)
                if dist < 1.0:
                    continue  # degenerate

                has_signal = (v in signal_nodes)
                delay = signal_delay if has_signal else UNCONTROLLED_DELAY_S

                edges_raw.append({
                    "from":       u,
                    "to":         v,
                    "length_m":   dist,
                    "speed_ms":   speed_ms,
                    "lanes":      lanes,
                    "road_class": road_class,
                    "oneway":     True,          # always directed after expansion
                    "name":       name,
                    "ref":        ref,
                    "geometry":   [(lat_u, lon_u), (lat_v, lon_v)],
                    "delay_s":    delay,
                })
        way_count += 1

    print(f"[parse] Ways processed: {way_count:,}  |  raw directed edges: {len(edges_raw):,}")
    return nodes_latlon, edges_raw

# ─── Graph construction ───────────────────────────────────────────────────────

def build_adjacency(edges_raw):
    """Build neighbour sets from raw edge list."""
    out_edges  = collections.defaultdict(list)   # node → [edge_index]
    in_degree  = collections.defaultdict(int)
    out_degree = collections.defaultdict(int)
    all_nodes  = set()

    for i, e in enumerate(edges_raw):
        u, v = e["from"], e["to"]
        out_edges[u].append(i)
        in_degree[v]  += 1
        out_degree[u] += 1
        all_nodes.add(u)
        all_nodes.add(v)

    return out_edges, in_degree, out_degree, all_nodes


def contract_degree2(edges_raw, nodes_latlon):
    """
    Contract nodes with in-degree == 1 and out-degree == 1
    (i.e., not intersections or dead ends) while merging geometry.
    Returns a new condensed edge list.
    """
    print("[contract] Building adjacency for contraction …")
    out_edges, in_deg, out_deg, all_nodes = build_adjacency(edges_raw)

    # Merge consecutive edges sharing the same road attributes
    in_deg_total  = collections.defaultdict(int)
    out_deg_total = collections.defaultdict(int)
    in_edge_of    = collections.defaultdict(list)
    out_edge_of   = collections.defaultdict(list)

    for i, e in enumerate(edges_raw):
        u, v = e["from"], e["to"]
        out_deg_total[u] += 1
        in_deg_total[v]  += 1
        out_edge_of[u].append(i)
        in_edge_of[v].append(i)

    # A node is contractible if it has exactly 1 incoming and 1 outgoing edge
    # AND the two edges share the same road class and name
    def is_contractible(node):
        if in_deg_total[node] != 1 or out_deg_total[node] != 1:
            return False
        in_e  = edges_raw[in_edge_of[node][0]]
        out_e = edges_raw[out_edge_of[node][0]]
        return (in_e["road_class"] == out_e["road_class"] and
                in_e["name"]       == out_e["name"])

    contracted = set()
    new_edges  = []
    visited    = set()

    for i, edge in enumerate(edges_raw):
        if i in visited:
            continue
        visited.add(i)

        u     = edge["from"]
        v     = edge["to"]
        geom  = list(edge["geometry"])
        length = edge["length_m"]
        speed  = edge["speed_ms"]
        lanes  = edge["lanes"]
        rclass = edge["road_class"]
        name   = edge["name"]
        ref    = edge["ref"]
        delay  = edge["delay_s"]

        # Walk forward contracting degree-2 nodes
        while is_contractible(v):
            out_i = out_edge_of[v][0]
            if out_i in visited:
                break
            nxt = edges_raw[out_i]
            visited.add(out_i)
            contracted.add(v)

            # Merge geometry (drop duplicate shared point)
            nxt_geom = nxt["geometry"]
            if geom and nxt_geom and geom[-1] == nxt_geom[0]:
                nxt_geom = nxt_geom[1:]
            geom.extend(nxt_geom)
            length += nxt["length_m"]
            # Use minimum speed (most conservative)
            speed = min(speed, nxt["speed_ms"])
            # Use signal delay if any segment has it
            delay = max(delay, nxt["delay_s"])
            v = nxt["to"]

        new_edges.append({
            "from":       u,
            "to":         v,
            "length_m":   length,
            "speed_ms":   speed,
            "lanes":      lanes,
            "road_class": rclass,
            "oneway":     True,
            "name":       name,
            "ref":        ref,
            "geometry":   geom,
            "delay_s":    delay,
        })

    print(f"[contract] Before: {len(edges_raw):,} edges  |  After: {len(new_edges):,} edges  |  Contracted {len(contracted):,} nodes")
    return new_edges

# ─── Strongly Connected Component (Tarjan's algorithm) ───────────────────────

def tarjan_scc(node_ids, edges_compact):
    """
    Tarjan iterative SCC on the contracted graph.
    Returns list of SCCs (each is a set of node IDs), largest first.
    """
    print("[scc] Running Tarjan SCC …")
    adj = collections.defaultdict(list)
    all_nodes_in_edges = set()
    for e in edges_compact:
        adj[e["from"]].append(e["to"])
        all_nodes_in_edges.add(e["from"])
        all_nodes_in_edges.add(e["to"])

    nodes = list(all_nodes_in_edges)
    index_counter = [0]
    index   = {}
    lowlink = {}
    on_stack = {}
    stack  = []
    sccs   = []

    # Iterative Tarjan
    def strongconnect(root):
        call_stack = [(root, iter(adj[root]))]
        index[root]   = index_counter[0]
        lowlink[root] = index_counter[0]
        index_counter[0] += 1
        on_stack[root] = True
        stack.append(root)

        while call_stack:
            v, children = call_stack[-1]
            try:
                w = next(children)
                if w not in index:
                    index[w]   = index_counter[0]
                    lowlink[w] = index_counter[0]
                    index_counter[0] += 1
                    on_stack[w] = True
                    stack.append(w)
                    call_stack.append((w, iter(adj[w])))
                elif on_stack.get(w):
                    lowlink[v] = min(lowlink[v], index[w])
            except StopIteration:
                call_stack.pop()
                if call_stack:
                    parent = call_stack[-1][0]
                    lowlink[parent] = min(lowlink[parent], lowlink[v])
                if lowlink[v] == index[v]:
                    scc = set()
                    while True:
                        w = stack.pop()
                        on_stack[w] = False
                        scc.add(w)
                        if w == v:
                            break
                    sccs.append(scc)

    for n in nodes:
        if n not in index:
            strongconnect(n)

    sccs.sort(key=len, reverse=True)
    if sccs:
        print(f"[scc] SCCs found: {len(sccs):,}  |  Largest: {len(sccs[0]):,} nodes")
    return sccs

# ─── Binary graph writer ──────────────────────────────────────────────────────
#
# Layout:
#   Header (48 bytes):
#     magic[4], version[4], node_count[4], edge_count[4],
#     bbox_w[4f], bbox_s[4f], bbox_e[4f], bbox_n[4f],
#     string_table_offset[4], geom_table_offset[4], reserved[8]
#   Node array: node_count × (id[4], lat[4f], lon[4f])  = 12 bytes each
#   Edge array: edge_count × struct (40 bytes each):
#     from[4], to[4], length_m[4f], speed_ms[4f], delay_s[4f],
#     lanes[1], road_class[1], flags[1], pad[1],
#     name_off[4], ref_off[4], geom_off[4], geom_len[4]
#   String table: null-terminated UTF-8 strings
#   Geometry table: geom_len × (lat[4f], lon[4f])

def write_graph_bin(out_path, node_ids, node_latlon, edges, bbox):
    """Write the compact binary graph file."""
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)

    # Re-index nodes → 0 … N-1
    node_id_map = {osm_id: i for i, osm_id in enumerate(node_ids)}

    # Build string table
    string_table = bytearray()
    string_offsets = {}

    def intern_str(s: str) -> int:
        if s not in string_offsets:
            string_offsets[s] = len(string_table)
            string_table.extend(s.encode("utf-8") + b"\x00")
        return string_offsets[s]

    intern_str("")  # offset 0 = empty string

    # Build geometry table
    geom_table = bytearray()
    geom_records = []  # (offset, length) per edge

    for e in edges:
        geom = e["geometry"]
        off  = len(geom_table) // 8   # in units of (lat,lon) pairs
        for lat, lon in geom:
            geom_table += struct.pack("<ff", lat, lon)
        geom_records.append((off, len(geom)))

    # Now build name/ref offsets
    edge_name_offs = []
    edge_ref_offs  = []
    for e in edges:
        edge_name_offs.append(intern_str(e["name"]))
        edge_ref_offs.append(intern_str(e["ref"]))

    w, s, e_coord, n = [float(x) for x in bbox.split(",")]

    # Header offsets
    header_size   = 48
    node_size     = 12   # id[4] lat[4] lon[4]
    edge_size_b   = 40
    n_nodes       = len(node_ids)
    n_edges       = len(edges)
    string_offset = header_size + n_nodes * node_size + n_edges * edge_size_b
    geom_offset   = string_offset + len(string_table)

    with open(out_path, "wb") as f:
        # Header
        f.write(MAGIC)
        f.write(struct.pack("<I", VERSION))
        f.write(struct.pack("<I", n_nodes))
        f.write(struct.pack("<I", n_edges))
        f.write(struct.pack("<ffff", w, s, e_coord, n))
        f.write(struct.pack("<II", string_offset, geom_offset))
        f.write(b"\x00" * 8)  # reserved

        # Nodes
        for osm_id in node_ids:
            lat, lon = node_latlon[osm_id]
            idx = node_id_map[osm_id]
            f.write(struct.pack("<Iff", idx, lat, lon))

        # Edges
        for i, edge in enumerate(edges):
            u = node_id_map.get(edge["from"])
            v = node_id_map.get(edge["to"])
            if u is None or v is None:
                continue
            flags = 0x01 if edge["oneway"] else 0x00
            f.write(struct.pack("<IIfffBBBB",
                u, v,
                edge["length_m"],
                edge["speed_ms"],
                edge["delay_s"],
                min(edge["lanes"], 7),
                edge["road_class"],
                flags,
                0,  # pad
            ))
            geom_off, geom_len = geom_records[i]
            f.write(struct.pack("<III",
                edge_name_offs[i],
                edge_ref_offs[i],
                geom_off,
            ))
            f.write(struct.pack("<I", geom_len))

        # String table
        f.write(bytes(string_table))

        # Geometry table
        f.write(bytes(geom_table))

    size_mb = os.path.getsize(out_path) / 1e6
    print(f"[write] {out_path}  ({size_mb:.2f} MB)  "
          f"nodes={n_nodes:,}  edges={n_edges:,}")
    return n_nodes, n_edges, size_mb

# ─── Stats report ─────────────────────────────────────────────────────────────

def write_stats(stats_path, n_nodes, n_edges, total_km, avg_deg,
                scc_nodes, total_nodes_before_scc, size_mb, bbox, build_time):
    os.makedirs(os.path.dirname(stats_path) or ".", exist_ok=True)
    coverage = 100.0 * scc_nodes / max(total_nodes_before_scc, 1)
    with open(stats_path, "w", encoding="utf-8") as f:
        f.write(f"# SmartCity — Graph Statistics\n\n")
        f.write(f"Generated: {time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}\n\n")
        f.write(f"| Metric | Value |\n")
        f.write(f"|---|---|\n")
        f.write(f"| BBox | {bbox} |\n")
        f.write(f"| Nodes (largest SCC) | {n_nodes:,} |\n")
        f.write(f"| Edges (directed) | {n_edges:,} |\n")
        f.write(f"| Average out-degree | {avg_deg:.2f} |\n")
        f.write(f"| Total road length | {total_km:.1f} km |\n")
        f.write(f"| SCC coverage | {coverage:.1f}% of all reachable nodes |\n")
        f.write(f"| graph.bin size | {size_mb:.2f} MB |\n")
        f.write(f"| Build time | {build_time:.1f} s |\n")
        f.write(f"\n> Node count target: ≤ 150,000\n")
        if n_nodes > 150_000:
            f.write(f"> ⚠️ Node count exceeds target! Consider tightening the bbox.\n")
        else:
            f.write(f"> ✅ Node count within budget.\n")
    print(f"[stats] {stats_path}")

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="SmartCity graph builder")
    parser.add_argument("--bbox",       default="77.30,28.38,77.60,28.65",
                        help="W,S,E,N  (default: Noida+Greater Noida)")
    parser.add_argument("--out",        default="data/graph.bin")
    parser.add_argument("--stats",      default="docs/graph_stats.md")
    parser.add_argument("--cache",      default="data/osm_cache.xml")
    parser.add_argument("--signal-delay", type=float, default=SIGNAL_DELAY_S_DEFAULT)
    parser.add_argument("--no-cache",   action="store_true")
    args = parser.parse_args()

    t0 = time.time()

    # 1. Download / load OSM data
    root = download_osm(args.bbox, args.cache, force=args.no_cache)

    # 2. Parse into raw directed edges (filter nodes by active bbox)
    nodes_latlon, edges_raw = parse_osm(root, args.signal_delay, bbox=args.bbox)

    # 3. Contract degree-2 nodes
    edges_compact = contract_degree2(edges_raw, nodes_latlon)

    # 4. Extract largest SCC
    sccs = tarjan_scc(None, edges_compact)
    if not sccs:
        sys.exit("[scc] No strongly connected components found — empty graph?")

    largest_scc = sccs[0]
    total_nodes_before_scc = sum(len(s) for s in sccs)

    # Filter edges to largest SCC
    edges_scc = [e for e in edges_compact
                 if e["from"] in largest_scc and e["to"] in largest_scc]

    # Node IDs in SCC (keep order for stable indexing)
    scc_node_ids = sorted(largest_scc)

    # Compute stats
    total_km = sum(e["length_m"] for e in edges_scc) / 1000.0
    avg_deg  = len(edges_scc) / max(len(scc_node_ids), 1)

    # 5. Write binary
    n_nodes, n_edges, size_mb = write_graph_bin(
        args.out, scc_node_ids, nodes_latlon, edges_scc, args.bbox
    )

    build_time = time.time() - t0

    # 6. Write stats report
    write_stats(args.stats, n_nodes, n_edges, total_km, avg_deg,
                len(largest_scc), total_nodes_before_scc,
                size_mb, args.bbox, build_time)

    # 7. Final size check
    print(f"\n[done] Build complete in {build_time:.1f} s")
    if n_nodes > 150_000:
        print(f"⚠️  Node count {n_nodes:,} exceeds 150k target. "
              "Consider tightening bbox or increasing contraction.")
    else:
        print(f"✅ Node count {n_nodes:,} within 150k budget.")

    if size_mb > 20:
        print(f"⚠️  graph.bin is {size_mb:.1f} MB — approaching 25 MB data budget.")
    else:
        print(f"✅ graph.bin size {size_mb:.1f} MB within budget.")


if __name__ == "__main__":
    main()
