// snap.cpp — Wrapper around SpatialGrid::snap_to_edge (Phase 2)
#include "snap.h"
#include "../graph/spatial_grid.h"

namespace smartcity {

SnappedPoint snap_to_edge(float lat, float lon, const Graph& g,
                           const SpatialGrid& grid, float max_dist_m) {
    return grid.snap_to_edge(lat, lon, g, max_dist_m);
}

} // namespace smartcity
