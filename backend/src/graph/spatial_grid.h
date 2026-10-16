#pragma once
// spatial_grid.h — O(1) average nearest-node/edge lookup via uniform grid.
//
// Algorithm:
//   1. Divide the bounding box into CELL_COUNT × CELL_COUNT cells.
//   2. Assign each node to its cell.
//   3. nearest_node(lat, lon): check the containing cell + ring of neighbours
//      until we find a result or exhaust the grid.
//   4. snap_to_edge(lat, lon, graph): project query point onto every edge
//      in candidate nodes' adjacency lists; keep the closest projection
//      that respects the one-way direction of the edge.

#include "graph.h"
#include <cstdint>
#include <vector>
#include <optional>

namespace smartcity {

// ── SnappedPoint: result of snap_to_edge ─────────────────────────────────────
struct SnappedPoint {
    uint32_t edge_idx;     // index into Graph::edges
    float    offset_m;     // metres along the edge from the 'from' node
    float    lat, lon;     // projected coordinate on the edge
    float    dist_m;       // distance from query to the snapped point
    bool     valid = false;
};

// ── SpatialGrid ───────────────────────────────────────────────────────────────
class SpatialGrid {
public:
    explicit SpatialGrid() = default;
    explicit SpatialGrid(const Graph& g, int cell_count = 512);

    /// Find the k nearest node IDs to (lat, lon).  Returns empty if grid empty.
    std::vector<uint32_t> nearest_nodes(float lat, float lon, int k = 8) const;

    /// Project (lat, lon) onto the closest drivable edge in graph g.
    /// Returns invalid SnappedPoint if no edge is within max_dist_m.
    SnappedPoint snap_to_edge(float lat, float lon,
                              const Graph& g,
                              float max_dist_m = 300.0f) const;

    bool empty() const { return cells_.empty(); }

private:
    int     cell_count_ = 0;
    float   west_,  south_, east_, north_;
    float   cell_w_, cell_h_;          // width/height of a cell in degrees

    // cells_[row * cell_count_ + col] = list of node indices in that cell
    std::vector<std::vector<uint32_t>> cells_;

    int col_of(float lon) const;
    int row_of(float lat) const;
    std::pair<int,int> cell_of(float lat, float lon) const;
};

} // namespace smartcity
