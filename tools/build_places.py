#!/usr/bin/env python3
"""
tools/build_places.py — SmartCity Phase 1 Places Index Builder

Downloads POIs and named roads from OpenStreetMap via Overpass API
for the Noida + Greater Noida bounding box. Builds data/places.json
used for autocomplete search in the frontend and for vehicle
hotspot sampling in the simulation.

Categories included:
  - education: universities, colleges, schools
  - hospital: hospitals, clinics
  - mall: shopping malls, commercial centres
  - transit: metro stations, railway stations
  - park: parks, gardens
  - office: office parks, tech parks
  - landmark: notable named places
  - road: named primary+ roads for routing autocomplete

Usage:
    python tools/build_places.py [--bbox W,S,E,N] [--out data/places.json]
                                 [--cache data/places_cache.xml] [--no-cache]
"""

import argparse
import json
import math
import os
import sys
import time
import xml.etree.ElementTree as ET

try:
    import requests
except ImportError:
    sys.exit("Missing: pip install requests")

OVERPASS_URL = "https://overpass-api.de/api/interpreter"
USER_AGENT   = "SmartCity-academic-places-builder/1.0 (JIIT Noida student project)"

# ─── Hotspot weight by category (for vehicle OD sampling) ─────────────────────
CATEGORY_WEIGHT = {
    "education": 3.0,
    "hospital":  2.0,
    "mall":      2.5,
    "transit":   4.0,   # metro stations generate lots of traffic
    "park":      1.0,
    "office":    3.0,
    "landmark":  1.5,
    "road":      0.5,
}

# ─── Hardcoded landmarks (always included) ─────────────────────────────────────
HARDCODED = [
    {
        "id":       "jiit_noida",
        "name":     "JIIT Noida",
        "lat":      28.5705,
        "lon":      77.3219,
        "type":     "education",
        "address":  "Sector 62, Noida, Uttar Pradesh 201309",
        "weight":   5.0,
    },
    {
        "id":       "sector_18_noida",
        "name":     "Sector 18 Metro Station",
        "lat":      28.5706,
        "lon":      77.3214,
        "type":     "transit",
        "address":  "Sector 18, Noida, Uttar Pradesh",
        "weight":   4.0,
    },
    {
        "id":       "botanical_garden_metro",
        "name":     "Botanical Garden Metro Station",
        "lat":      28.5634,
        "lon":      77.3382,
        "type":     "transit",
        "address":  "Sector 128, Noida, Uttar Pradesh",
        "weight":   4.0,
    },
    {
        "id":       "dlf_mall_noida",
        "name":     "DLF Mall of India",
        "lat":      28.5673,
        "lon":      77.3215,
        "type":     "mall",
        "address":  "Sector 18, Noida, Uttar Pradesh",
        "weight":   3.0,
    },
    {
        "id":       "noida_city_centre_metro",
        "name":     "Noida City Centre Metro Station",
        "lat":      28.5673,
        "lon":      77.3191,
        "type":     "transit",
        "address":  "Sector 32, Noida, Uttar Pradesh",
        "weight":   4.0,
    },
    {
        "id":       "amity_univ_noida",
        "name":     "Amity University Noida",
        "lat":      28.5449,
        "lon":      77.3330,
        "type":     "education",
        "address":  "Sector 125, Noida, Uttar Pradesh",
        "weight":   3.0,
    },
    {
        "id":       "fortis_hospital_noida",
        "name":     "Fortis Hospital Noida",
        "lat":      28.5474,
        "lon":      77.3562,
        "type":     "hospital",
        "address":  "Sector 62, Noida, Uttar Pradesh",
        "weight":   2.5,
    },
    {
        "id":       "knowledge_park_greater_noida",
        "name":     "Knowledge Park II, Greater Noida",
        "lat":      28.4721,
        "lon":      77.5035,
        "type":     "education",
        "address":  "Knowledge Park II, Greater Noida, Uttar Pradesh",
        "weight":   3.0,
    },
    {
        "id":       "greater_noida_metro",
        "name":     "Greater Noida Depot Metro Station",
        "lat":      28.4744,
        "lon":      77.5036,
        "type":     "transit",
        "address":  "Greater Noida, Uttar Pradesh",
        "weight":   4.0,
    },
    {
        "id":       "noida_expressway_toll",
        "name":     "Noida–Greater Noida Expressway",
        "lat":      28.5161,
        "lon":      77.4022,
        "type":     "road",
        "address":  "Noida Expressway, Uttar Pradesh",
        "weight":   1.0,
    },
]

# ─── Overpass download ─────────────────────────────────────────────────────────

def build_places_query(bbox: str) -> str:
    w, s, e, n = bbox.split(",")
    area = f"{s},{w},{n},{e}"
    return f"""
[out:xml][timeout:120];
(
  node["amenity"~"^(university|college|school|hospital|clinic|pharmacy)$"]({area});
  node["shop"~"^(mall|department_store|supermarket)$"]({area});
  node["public_transport"="stop_position"]["railway"="station"]({area});
  node["station"="subway"]({area});
  node["railway"~"^(station|halt)$"]({area});
  node["leisure"~"^(park|garden|nature_reserve)$"]({area});
  node["landuse"~"^(commercial|office|industrial)$"]["name"]({area});
  node["place"~"^(suburb|neighbourhood|quarter)$"]["name"]({area});
  way["highway"~"^(primary|secondary|tertiary)$"]["name"]({area});
  relation["highway"~"^(primary|secondary)$"]["name"]({area});
);
out center;
""".strip()


def download_places(bbox: str, cache_path: str, force: bool = False) -> ET.Element:
    if not force and os.path.exists(cache_path):
        print(f"[overpass] Using cached places data: {cache_path}")
        return ET.parse(cache_path).getroot()

    query = build_places_query(bbox)
    print(f"[overpass] Downloading places …")
    resp = requests.post(
        OVERPASS_URL,
        data={"data": query},
        headers={"User-Agent": USER_AGENT},
        timeout=180,
    )
    resp.raise_for_status()
    os.makedirs(os.path.dirname(cache_path) or ".", exist_ok=True)
    with open(cache_path, "wb") as f:
        f.write(resp.content)
    print(f"[overpass] Downloaded {len(resp.content)/1e3:.0f} KB -> {cache_path}")
    return ET.fromstring(resp.content)

# ─── Classify and extract places ──────────────────────────────────────────────

def classify_node(tags: dict) -> str:
    amenity = tags.get("amenity", "")
    shop    = tags.get("shop", "")
    railway = tags.get("railway", "")
    station = tags.get("station", "")
    public_t= tags.get("public_transport", "")
    leisure = tags.get("leisure", "")
    place   = tags.get("place", "")

    if amenity in ("university", "college", "school"):
        return "education"
    if amenity in ("hospital", "clinic", "pharmacy"):
        return "hospital"
    if shop in ("mall", "department_store", "supermarket"):
        return "mall"
    if railway in ("station", "halt") or station == "subway":
        return "transit"
    if public_t == "stop_position":
        return "transit"
    if leisure in ("park", "garden", "nature_reserve"):
        return "park"
    if place in ("suburb", "neighbourhood", "quarter"):
        return "landmark"
    return "landmark"


def extract_places(root: ET.Element) -> list:
    places = []
    seen_names = set()

    for elem in root:
        tags = {t.attrib["k"]: t.attrib["v"] for t in elem if t.tag == "tag"}
        name = tags.get("name", "").strip()
        if not name:
            continue

        # Get lat/lon
        if elem.tag == "node":
            try:
                lat = float(elem.attrib["lat"])
                lon = float(elem.attrib["lon"])
            except (KeyError, ValueError):
                continue
        elif elem.tag in ("way", "relation"):
            center = elem.find("center")
            if center is None:
                continue
            try:
                lat = float(center.attrib["lat"])
                lon = float(center.attrib["lon"])
            except (KeyError, ValueError):
                continue
        else:
            continue

        highway = tags.get("highway", "")
        if highway in ("primary", "secondary", "tertiary"):
            category = "road"
        else:
            category = classify_node(tags)

        # De-duplicate by name + category
        key = (name.lower(), category)
        if key in seen_names:
            continue
        seen_names.add(key)

        osm_id = elem.attrib.get("id", "")
        addr_parts = []
        for k in ("addr:housenumber", "addr:street", "addr:suburb",
                  "addr:city", "addr:state"):
            if k in tags:
                addr_parts.append(tags[k])
        address = ", ".join(addr_parts) or tags.get("addr:full", "")

        places.append({
            "id":      f"osm_{elem.tag}_{osm_id}",
            "name":    name,
            "lat":     round(lat, 6),
            "lon":     round(lon, 6),
            "type":    category,
            "address": address,
            "weight":  CATEGORY_WEIGHT.get(category, 1.0),
        })

    return places


def merge_deduplicate(hardcoded: list, osm_places: list) -> list:
    """Merge hardcoded + OSM, dedup by name proximity."""
    merged = list(hardcoded)
    existing_names = {p["name"].lower() for p in merged}

    for p in osm_places:
        if p["name"].lower() not in existing_names:
            merged.append(p)
            existing_names.add(p["name"].lower())

    # Sort: transit first (highest weight), then name
    merged.sort(key=lambda p: (-p.get("weight", 1.0), p["name"]))
    return merged

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="SmartCity places builder")
    parser.add_argument("--bbox",     default="77.30,28.38,77.60,28.65")
    parser.add_argument("--out",      default="data/places.json")
    parser.add_argument("--cache",    default="data/places_cache.xml")
    parser.add_argument("--no-cache", action="store_true")
    args = parser.parse_args()

    t0 = time.time()

    root = download_places(args.bbox, args.cache, force=args.no_cache)
    osm_places = extract_places(root)
    print(f"[places] Extracted {len(osm_places):,} OSM places")

    all_places = merge_deduplicate(HARDCODED, osm_places)
    print(f"[places] Total after merge: {len(all_places):,}")

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(all_places, f, ensure_ascii=False, indent=2)

    size_kb = os.path.getsize(args.out) / 1024
    elapsed = time.time() - t0
    print(f"[done] {args.out}  ({size_kb:.0f} KB)  {len(all_places):,} places  in {elapsed:.1f}s")

    # Category summary
    from collections import Counter
    cats = Counter(p["type"] for p in all_places)
    for cat, cnt in cats.most_common():
        print(f"  {cat}: {cnt}")


if __name__ == "__main__":
    main()
