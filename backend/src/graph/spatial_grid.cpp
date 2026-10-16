// spatial_grid.cpp — Uniform spatial grid for nearest-node and snap-to-edge.
#include "spatial_grid.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <cassert>

namespace smartcity {

// ── Helpers ──────────────────────────────────────────────────────────────────

static constexpr float DEG2RAD = 3.14159265358979f / 180.0f;
static constexpr float EARTH_R = 6'371'000.0f;  // metres

// Fast approximate great-circle distance in metres (Equirectangular).
static float dist_m(float lat1, float lon1, float lat2, float lon2) {
    float dlat = (lat2 - lat1) * DEG2RAD;
    float dlon = (lon2 - lon1) * DEG2RAD * std::cos((lat1 + lat2) * 0.5f * DEG2RAD);
    return EARTH_R * std::sqrt(dlat * dlat + dlon * dlon);
}

// Project point P onto segment AB; return (t, foot_lat, foot_lon) where
// t ∈ [0,1] is the fractional position along AB.
static std::tuple<float, float, float>
project_onto_segment(float lat_p, float lon_p,
                     float lat_a, float lon_a,
                     float lat_b, float lon_b) {
    // Work in flat (dx, dy) space (equirectangular, fine for ≤ a few km)
    float cos_lat = std::cos((lat_a + lat_b) * 0.5f * DEG2RAD);
    float ax = (lon_a - lon_a) * cos_lat, ay = lat_a - lat_a; // = 0,0
    float bx = (lon_b - lon_a) * cos_lat, by = lat_b - lat_a;
    float px = (lon_p - lon_a) * cos_lat, py = lat_p - lat_a;

    float seg_len2 = bx * bx + by * by;
    float t = 0.0f;
    if (seg_len2 > 1e-12f)
        t = std::clamp((px * bx + py * by) / seg_len2, 0.0f, 1.0f);

    float foot_lat = lat_a + t * (lat_b - lat_a);
    float foot_lon = lon_a + t * (lon_b - lon_a);
    return { t, foot_lat, foot_lon };
}

// ── Constructor ───────────────────────────────────────────────────────────────

SpatialGrid::SpatialGrid(const Graph& g, int cell_count)
    : cell_count_(cell_count),
      west_ (g.bbox.west),  south_(g.bbox.south),
      east_ (g.bbox.east),  north_(g.bbox.north) {

    assert(cell_count > 0);
    cell_w_ = (east_  - west_)  / static_cast<float>(cell_count_);
    cell_h_ = (north_ - south_) / static_cast<float>(cell_count_);
    if (cell_w_ <= 0.0f || cell_h_ <= 0.0f) return;

    cells_.resize(static_cast<std::size_t>(cell_count_ * cell_count_));

    for (const Node& n : g.nodes) {
        auto [r, c] = cell_of(n.lat, n.lon);
        if (r >= 0 && r < cell_count_ && c >= 0 && c < cell_count_)
            cells_[static_cast<std::size_t>(r * cell_count_ + c)].push_back(n.id);
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

int SpatialGrid::col_of(float lon) const {
    return static_cast<int>((lon - west_)  / cell_w_);
}
int SpatialGrid::row_of(float lat) const {
    return static_cast<int>((lat - south_) / cell_h_);
}
std::pair<int,int> SpatialGrid::cell_of(float lat, float lon) const {
    return { row_of(lat), col_of(lon) };
}

// ── nearest_nodes ─────────────────────────────────────────────────────────────

std::vector<uint32_t> SpatialGrid::nearest_nodes(float lat, float lon, int k) const {
    if (cells_.empty()) return {};

    auto [cr, cc] = cell_of(lat, lon);

    // Collect candidate nodes from an expanding ring of cells
    std::vector<std::pair<float, uint32_t>> cands;  // (dist, node_id)

    for (int ring = 0; ring <= cell_count_; ++ring) {
        int r0 = std::max(0, cr - ring), r1 = std::min(cell_count_ - 1, cr + ring);
        int c0 = std::max(0, cc - ring), c1 = std::min(cell_count_ - 1, cc + ring);

        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                // Only process the border of the ring (not interior, already done)
                if (ring > 0 && r > r0 && r < r1 && c > c0 && c < c1) continue;
                for (uint32_t nid : cells_[static_cast<std::size_t>(r * cell_count_ + c)]) {
                    // We need the actual lat/lon; use node_id == position in nodes[]
                    // This works because build_graph.py writes nodes in sorted order
                    // and the CSR index equals the internal sequential id.
                    // However since we only store node ids in cells, we need the Graph
                    // to get lat/lon. To avoid storing a Graph* here we use a simpler
                    // approach: store (node_idx, lat, lon) in a flat parallel array.
                    // But for the Phase 1 skeleton we just collect node IDs and the
                    // caller can look up lat/lon in Graph::nodes[nid].
                    cands.emplace_back(0.0f, nid);
                }
            }
        }

        // Stop once we have enough candidates or have covered the entire grid
        if (static_cast<int>(cands.size()) >= k * 4 || ring == cell_count_)
            break;
    }

    return {};  // caller should use snap_to_edge instead; see below
}

// ── snap_to_edge ──────────────────────────────────────────────────────────────

SnappedPoint SpatialGrid::snap_to_edge(float lat, float lon,
                                        const Graph& g,
                                        float max_dist_m) const {
    if (cells_.empty() || g.empty()) return {};

    auto [cr, cc] = cell_of(lat, lon);

    SnappedPoint best{};
    best.dist_m = std::numeric_limits<float>::max();

    // Expand search rings until we find a candidate within max_dist_m,
    // then do one more ring to avoid missing nearby edges.
    bool found_one = false;
    int  found_ring = -1;

    for (int ring = 0; ring <= std::min(cell_count_, 16); ++ring) {
        if (found_one && ring > found_ring + 1) break;

        int r0 = std::max(0, cr - ring), r1 = std::min(cell_count_ - 1, cr + ring);
        int c0 = std::max(0, cc - ring), c1 = std::min(cell_count_ - 1, cc + ring);

        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                if (ring > 0 && r > r0 && r < r1 && c > c0 && c < c1) continue;

                for (uint32_t nid : cells_[static_cast<std::size_t>(r * cell_count_ + c)]) {
                    // Check all outgoing edges of this node
                    for (uint32_t ei : g.out_edges(nid)) {
                        const Edge& e = g.edges[ei];
                        const Node& na = g.nodes[e.from];
                        const Node& nb = g.nodes[e.to];

                        // Quick bounding-box rejection
                        float min_lat = std::min(na.lat, nb.lat) - 0.002f;
                        float max_lat = std::max(na.lat, nb.lat) + 0.002f;
                        float min_lon = std::min(na.lon, nb.lon) - 0.002f;
                        float max_lon = std::max(na.lon, nb.lon) + 0.002f;
                        if (lat < min_lat || lat > max_lat ||
                            lon < min_lon || lon > max_lon) continue;

                        auto [t, foot_lat, foot_lon] =
                            project_onto_segment(lat, lon, na.lat, na.lon, nb.lat, nb.lon);

                        float d = dist_m(lat, lon, foot_lat, foot_lon);
                        if (d < best.dist_m && d <= max_dist_m) {
                            best.dist_m   = d;
                            best.edge_idx = ei;
                            best.lat      = foot_lat;
                            best.lon      = foot_lon;
                            best.offset_m = t * e.length_m;
                            best.valid    = true;
                            found_one     = true;
                            found_ring    = ring;
                        }
                    }
                }
            }
        }
    }

    return best;
}

} // namespace smartcity
