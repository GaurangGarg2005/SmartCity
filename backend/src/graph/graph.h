#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <stdexcept>

namespace smartcity {

// ── Graph node ─────────────────────────────────────────────────────────────
struct Node {
    uint32_t id;   // internal 0-based index (matches position in nodes[])
    float    lat, lon;
};

// ── Graph edge ─────────────────────────────────────────────────────────────
// The "free-flow weight" (seconds) is computed lazily:
//   weight = length_m / free_flow_speed_ms + delay_s
struct Edge {
    uint32_t from, to;
    float    length_m;
    float    free_flow_speed_ms;  // metres/second
    float    delay_s;             // intersection delay (seconds)
    uint8_t  lanes;
    uint8_t  road_class;   // 0=motorway … 7=living_street
    uint8_t  flags;        // bit 0 = oneway
    // String table offsets (into Graph::strings)
    uint32_t name_off;
    uint32_t ref_off;
    // Geometry: indices into Graph::geometry_flat
    uint32_t geom_offset;  // in units of (lat,lon) float pairs
    uint32_t geom_len;     // number of (lat,lon) pairs

    // Convenience: free-flow travel time in seconds
    float free_flow_s() const {
        return length_m / std::max(free_flow_speed_ms, 0.001f) + delay_s;
    }

    bool oneway() const { return (flags & 0x01) != 0; }
};

// ── Bounding box ────────────────────────────────────────────────────────────
struct BBox {
    float west, south, east, north;
    bool contains(float lat, float lon) const {
        return lat >= south && lat <= north && lon >= west && lon <= east;
    }
};

// ── Graph (CSR adjacency) ───────────────────────────────────────────────────
//
// Memory layout:
//   nodes[i]         — node i
//   csr_head[i]      — first entry in csr_list for node i's outgoing edges
//   csr_head[N]      — sentinel (== csr_list.size())
//   csr_list[k]      — edge index of k-th outgoing edge (flat list)
//   edges[j]         — edge j
//   strings          — null-terminated UTF-8 string table; index by byte offset
//   geometry_flat    — interleaved (lat, lon) float pairs for all polylines
//
struct Graph {
    // Core data
    std::vector<Node>     nodes;
    std::vector<Edge>     edges;

    // CSR adjacency (outgoing edges per node)
    std::vector<uint32_t> csr_head;   // size = nodes.size() + 1
    std::vector<uint32_t> csr_list;   // size = edges.size()

    // String table
    std::vector<char>     strings;

    // Geometry flat buffer: pairs of (lat, lon) floats
    std::vector<float>    geometry_flat;

    // Metadata
    BBox        bbox;
    std::string bbox_str;

    // ── API ──────────────────────────────────────────────────────────────────

    bool empty() const { return nodes.empty(); }
    uint32_t node_count() const { return static_cast<uint32_t>(nodes.size()); }
    uint32_t edge_count() const { return static_cast<uint32_t>(edges.size()); }

    /// Outgoing edges of node u: iterate csr_list[csr_head[u] .. csr_head[u+1])
    std::span<const uint32_t> out_edges(uint32_t u) const {
        return { csr_list.data() + csr_head[u],
                 csr_list.data() + csr_head[u + 1] };
    }

    /// Name string of edge e (may be empty)
    const char* edge_name(const Edge& e) const {
        return strings.data() + e.name_off;
    }

    /// Ref string of edge e (may be empty)
    const char* edge_ref(const Edge& e) const {
        return strings.data() + e.ref_off;
    }

    /// Geometry polyline of edge e — returns alternating lat,lon floats
    std::span<const float> edge_geometry(const Edge& e) const {
        return { geometry_flat.data() + e.geom_offset * 2,
                 e.geom_len * 2 };
    }

    // ── Load from binary file ─────────────────────────────────────────────────
    static Graph load(const std::string& path);
};

} // namespace smartcity
