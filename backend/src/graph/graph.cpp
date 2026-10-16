// graph.cpp — Binary graph loader for SmartCity (Phase 1/2)
//
// Binary format written by tools/build_graph.py:
//
//   Header (48 bytes):
//     magic[4]='SCGR', version[4]=1, node_count[4], edge_count[4],
//     bbox_w[f], bbox_s[f], bbox_e[f], bbox_n[f],
//     string_table_offset[4], geom_table_offset[4], reserved[8]
//
//   Node array: node_count × (id[u32], lat[f32], lon[f32]) = 12 bytes
//
//   Edge array: edge_count × 40 bytes:
//     from[u32], to[u32], length_m[f32], speed_ms[f32], delay_s[f32],
//     lanes[u8], road_class[u8], flags[u8], pad[u8],
//     name_off[u32], ref_off[u32], geom_off[u32], geom_len[u32]
//
//   String table: raw bytes (null-terminated UTF-8 strings)
//   Geometry table: geom_len × (lat[f32], lon[f32])

#include "graph.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sstream>
#include <algorithm>

namespace smartcity {

namespace {

// ── Binary read helpers ─────────────────────────────────────────────────────

constexpr uint8_t MAGIC[4] = {'S','C','G','R'};

[[nodiscard]] static bool read_bytes(std::FILE* f, void* buf, std::size_t n) {
    return std::fread(buf, 1, n, f) == n;
}

template<typename T>
[[nodiscard]] static bool read_val(std::FILE* f, T& out) {
    return read_bytes(f, &out, sizeof(T));
}

} // anonymous namespace

// ── Graph::load ─────────────────────────────────────────────────────────────

Graph Graph::load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        throw std::runtime_error("Cannot open graph file: " + path);

    struct Guard { std::FILE* f; ~Guard() { if (f) std::fclose(f); } } guard{f};

    // ── Header ──────────────────────────────────────────────────────────────
    uint8_t magic[4];
    if (!read_bytes(f, magic, 4) || std::memcmp(magic, MAGIC, 4) != 0)
        throw std::runtime_error("Invalid graph file magic: " + path);

    uint32_t version, n_nodes, n_edges;
    if (!read_val(f, version) || version != 1)
        throw std::runtime_error("Unsupported graph version: " + path);
    if (!read_val(f, n_nodes) || !read_val(f, n_edges))
        throw std::runtime_error("Truncated header: " + path);

    float bw, bs, be, bn;
    if (!read_val(f, bw) || !read_val(f, bs) || !read_val(f, be) || !read_val(f, bn))
        throw std::runtime_error("Truncated bbox in header: " + path);

    uint32_t str_off, geom_off;
    if (!read_val(f, str_off) || !read_val(f, geom_off))
        throw std::runtime_error("Truncated offsets in header: " + path);

    // skip reserved 8 bytes
    std::fseek(f, 8, SEEK_CUR);

    // ── Nodes ────────────────────────────────────────────────────────────────
    Graph g;
    g.bbox = { bw, bs, be, bn };
    {
        std::ostringstream ss;
        ss << bw << "," << bs << "," << be << "," << bn;
        g.bbox_str = ss.str();
    }

    g.nodes.resize(n_nodes);
    for (uint32_t i = 0; i < n_nodes; ++i) {
        Node& nd = g.nodes[i];
        if (!read_val(f, nd.id) || !read_val(f, nd.lat) || !read_val(f, nd.lon))
            throw std::runtime_error("Truncated node array");
    }

    // ── Edges ────────────────────────────────────────────────────────────────
    g.edges.resize(n_edges);
    for (uint32_t i = 0; i < n_edges; ++i) {
        Edge& e = g.edges[i];
        if (!read_val(f, e.from)               ||
            !read_val(f, e.to)                 ||
            !read_val(f, e.length_m)           ||
            !read_val(f, e.free_flow_speed_ms) ||
            !read_val(f, e.delay_s)            ||
            !read_val(f, e.lanes)              ||
            !read_val(f, e.road_class)         ||
            !read_val(f, e.flags))
            throw std::runtime_error("Truncated edge array");
        uint8_t pad; read_val(f, pad);  // alignment pad
        if (!read_val(f, e.name_off)   ||
            !read_val(f, e.ref_off)    ||
            !read_val(f, e.geom_offset)||
            !read_val(f, e.geom_len))
            throw std::runtime_error("Truncated edge geometry fields");
    }

    // ── String table ─────────────────────────────────────────────────────────
    long cur = std::ftell(f);
    std::fseek(f, 0L, SEEK_END);
    long file_end = std::ftell(f);
    std::fseek(f, cur, SEEK_SET);

    uint32_t str_size  = geom_off - str_off;
    uint32_t geom_size = static_cast<uint32_t>(file_end - geom_off);

    g.strings.resize(str_size + 1, '\0');  // extra null guard
    if (str_size > 0) {
        std::fseek(f, static_cast<long>(str_off), SEEK_SET);
        if (!read_bytes(f, g.strings.data(), str_size))
            throw std::runtime_error("Truncated string table");
    }

    // ── Geometry table ────────────────────────────────────────────────────────
    // Each entry is two floats: (lat, lon). geom_size is in bytes.
    uint32_t geom_floats = geom_size / sizeof(float);
    g.geometry_flat.resize(geom_floats);
    if (geom_floats > 0) {
        std::fseek(f, static_cast<long>(geom_off), SEEK_SET);
        if (!read_bytes(f, g.geometry_flat.data(), geom_size))
            throw std::runtime_error("Truncated geometry table");
    }

    // ── Build CSR adjacency ───────────────────────────────────────────────────
    //
    // csr_head[u] = index into csr_list of first outgoing edge of node u.
    // csr_list[k] = edge index of k-th outgoing edge.
    //
    g.csr_head.assign(n_nodes + 1, 0u);

    // Count out-degree
    for (const Edge& e : g.edges) {
        if (e.from < n_nodes)
            g.csr_head[e.from + 1]++;
    }
    // Prefix-sum
    for (uint32_t u = 0; u < n_nodes; ++u)
        g.csr_head[u + 1] += g.csr_head[u];

    // Fill CSR list
    g.csr_list.resize(n_edges);
    std::vector<uint32_t> fill(n_nodes, 0u);
    for (uint32_t ei = 0; ei < n_edges; ++ei) {
        uint32_t u = g.edges[ei].from;
        if (u < n_nodes) {
            g.csr_list[g.csr_head[u] + fill[u]] = ei;
            fill[u]++;
        }
    }

    return g;
}

} // namespace smartcity
