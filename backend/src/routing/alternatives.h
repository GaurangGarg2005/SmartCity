#pragma once
// alternatives.h — Penalty-method alternative routes (Phase 5 stub)
#include "../routing/dijkstra.h"
#include "../graph/graph.h"
#include <vector>

namespace smartcity {

struct AlternativeRoutes {
    std::vector<DijkstraResult> routes;   // [0] = best, [1..2] = alternatives
};

/// Compute up to `max_alts` alternative routes using the edge-penalty method.
/// Rejects alternatives sharing > 70% of edges with best, or > 1.5x best time.
AlternativeRoutes compute_alternatives(uint32_t from, uint32_t to,
                                        const Graph& g,
                                        const WeightStore& ws,
                                        int max_alts = 3,
                                        float penalty_factor = 1.3f,
                                        float max_stretch   = 1.5f,
                                        float max_overlap   = 0.7f);

} // namespace smartcity
