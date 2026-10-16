#pragma once
// weight_updater.h — Computes edge travel times from vehicle density.
// Two models: Greenshields (default) and BPR (Bureau of Public Roads).
#include "../graph/graph.h"
#include "../routing/dijkstra.h"
#include <vector>
#include <string>

namespace smartcity {

// Per-edge traffic state maintained by the simulator.
struct EdgeTrafficState {
    float vehicle_count  = 0.0f;   // current vehicles on this edge
    float capacity       = 0.0f;   // lanes × capacity_per_lane
    float smoothed_speed_ms = 0.0f; // exponentially smoothed actual speed

    // Delta tracking for incremental WS broadcast
    float prev_weight = 0.0f;
    float cur_weight  = 0.0f;
    bool  changed     = false;
};

// ── WeightUpdater ────────────────────────────────────────────────────────────
class WeightUpdater {
public:
    static constexpr float ALPHA          = 0.3f;   // EMA smoothing
    static constexpr float CAP_PER_LANE   = 1800.0f; // vehicles/hour/lane
    static constexpr float V_MIN_MS       = 1.0f / 3.6f; // 1 km/h (avoids div-by-zero)

    explicit WeightUpdater(const Graph& g, const std::string& model = "greenshields");

    /// Recompute edge weights from vehicle counts and publish a new snapshot.
    /// Returns the new snapshot (caller broadcasts deltas via WS).
    WeightSnapshotPtr update(const std::vector<EdgeTrafficState>& state,
                              uint64_t tick_id);

    /// Which edges changed by more than DELTA_THRESHOLD seconds since last tick
    std::vector<uint32_t> changed_edges(const std::vector<EdgeTrafficState>& state) const;

    const Graph& graph() const { return g_; }

private:
    const Graph& g_;
    std::string  model_;  // "greenshields" or "bpr"

    float greenshields_weight(const Edge& e, const EdgeTrafficState& s) const;
    float bpr_weight         (const Edge& e, const EdgeTrafficState& s) const;
};

} // namespace smartcity
