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
    float cos_lat = std::cos((lat_a + lat_b) * 0.5f * DEG2RAD);
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
      east_ (g.bbox.east),  north_(g.bbox.north),
      g_(&g) {

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
    if (cells_.empty() || !g_ || g_->empty()) return {};

    auto [cr, cc] = cell_of(lat, lon);

    // Collect candidate nodes from an expanding ring of cells
    std::vector<std::pair<float, uint32_t>> cands;  // (dist, node_id)

    int max_rings = std::min(cell_count_, 64);
    for (int ring = 0; ring <= max_rings; ++ring) {
        int r0 = std::max(0, cr - ring), r1 = std::min(cell_count_ - 1, cr + ring);
        int c0 = std::max(0, cc - ring), c1 = std::min(cell_count_ - 1, cc + ring);

        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                if (ring > 0 && r > r0 && r < r1 && c > c0 && c < c1) continue;
                for (uint32_t nid : cells_[static_cast<std::size_t>(r * cell_count_ + c)]) {
                    if (nid < g_->nodes.size()) {
                        float d = dist_m(lat, lon, g_->nodes[nid].lat, g_->nodes[nid].lon);
                        cands.emplace_back(d, nid);
                    }
                }
            }
        }

        if (static_cast<int>(cands.size()) >= k * 4)
            break;
    }

    if (cands.empty()) {
        // Fallback: check closest from first 500 nodes in graph
        for (uint32_t nid = 0; nid < std::min<uint32_t>(500, static_cast<uint32_t>(g_->nodes.size())); ++nid) {
            float d = dist_m(lat, lon, g_->nodes[nid].lat, g_->nodes[nid].lon);
            cands.emplace_back(d, nid);
        }
    }

    std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    std::vector<uint32_t> result;
    for (int i = 0; i < std::min<int>(k, static_cast<int>(cands.size())); ++i) {
        result.push_back(cands[static_cast<std::size_t>(i)].second);
    }
    return result;
}

// ── snap_to_edge ──────────────────────────────────────────────────────────────

SnappedPoint SpatialGrid::snap_to_edge(float lat, float lon,
                                        const Graph& g,
                                        float max_dist_m) const {
    if (cells_.empty() || g.empty()) return {};

    auto [cr, cc] = cell_of(lat, lon);

    SnappedPoint best{};
    best.dist_m = std::numeric_limits<float>::max();

    bool found_one = false;
    int  found_ring = -1;

    int max_rings = std::clamp(static_cast<int>(std::ceil(max_dist_m / 40.0f)), 16, std::min(cell_count_, 80));

    for (int ring = 0; ring <= max_rings; ++ring) {
        if (found_one && ring > found_ring + 2) break;

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

                        // Bounding-box check with threshold
                        float tol = std::max(0.005f, (max_dist_m / 111000.0f));
                        float min_lat = std::min(na.lat, nb.lat) - tol;
                        float max_lat = std::max(na.lat, nb.lat) + tol;
                        float min_lon = std::min(na.lon, nb.lon) - tol;
                        float max_lon = std::max(na.lon, nb.lon) + tol;
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
