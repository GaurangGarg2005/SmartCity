#pragma once
// maneuver.h — Build turn-by-turn steps from a route edge path.
#include "../graph/graph.h"
#include "../routing/dijkstra.h"
#include <vector>

namespace smartcity {

/// Build a sequence of RouteStep objects from an ordered list of edge indices.
std::vector<RouteStep> build_steps(const std::vector<uint32_t>& edge_path,
                                    const Graph& g);

} // namespace smartcity
