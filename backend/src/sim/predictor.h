#pragma once
// predictor.h — Traffic prediction (Phase 5 stub)
#include "../routing/dijkstra.h"
#include "../graph/graph.h"
#include <vector>

namespace smartcity {

/// Predict edge weights at horizon_min minutes in the future.
/// Returns a WeightSnapshot for use in Dijkstra route queries.
WeightSnapshotPtr predict(const Graph& g,
                           const WeightStore& ws,
                           int horizon_min);

} // namespace smartcity
