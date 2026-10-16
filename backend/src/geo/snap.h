#pragma once
// snap.h — Public interface for geo snapping
#include "../graph/graph.h"
#include "../graph/spatial_grid.h"

namespace smartcity {

/// Project (lat, lon) onto the nearest drivable edge in graph g.
SnappedPoint snap_to_edge(float lat, float lon, const Graph& g,
                           const SpatialGrid& grid,
                           float max_dist_m = 300.0f);

} // namespace smartcity
