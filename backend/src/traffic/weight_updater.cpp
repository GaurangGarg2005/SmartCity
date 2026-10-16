// weight_updater.cpp — Greenshields speed-density + BPR weight model (Phase 2/4)
//
// Greenshields (default):
//   rho  = vehicles / capacity              [0..1]
//   v(e) = v_f × (1 - rho)                 linear speed-density
//   t(e) = length_m / max(v(e), v_min) + delay_s
//
// BPR (Bureau of Public Roads):
//   t(e) = t0 × (1 + 0.15 × (q/C)^4)
//   where t0 = free-flow travel time, q = flow (veh/h), C = capacity (veh/h)

#include "weight_updater.h"
#include <algorithm>
#include <cmath>

namespace smartcity {

static constexpr float DELTA_THRESHOLD = 1.0f;  // seconds

WeightUpdater::WeightUpdater(const Graph& g, const std::string& model)
    : g_(g), model_(model) {}

// ── Greenshields model ─────────────────────────────────────────────────────

float WeightUpdater::greenshields_weight(const Edge& e,
                                          const EdgeTrafficState& s) const {
    float cap = s.capacity > 0.0f ? s.capacity : static_cast<float>(e.lanes) * CAP_PER_LANE;
    float rho = std::clamp(s.vehicle_count / cap, 0.0f, 1.0f);
    float v   = e.free_flow_speed_ms * (1.0f - rho);
    v = std::max(v, V_MIN_MS);
    return e.length_m / v + e.delay_s;
}

// ── BPR model ─────────────────────────────────────────────────────────────

float WeightUpdater::bpr_weight(const Edge& e,
                                 const EdgeTrafficState& s) const {
    float t0  = e.free_flow_s();
    float cap = s.capacity > 0.0f ? s.capacity : static_cast<float>(e.lanes) * CAP_PER_LANE;
    // Convert instantaneous vehicle count to flow estimate (heuristic)
    float q   = s.vehicle_count * 3600.0f / (e.length_m / e.free_flow_speed_ms + 1.0f);
    float vol_cap = q / std::max(cap, 1.0f);
    return t0 * (1.0f + 0.15f * std::pow(vol_cap, 4.0f));
}

// ── update ────────────────────────────────────────────────────────────────

WeightSnapshotPtr WeightUpdater::update(const std::vector<EdgeTrafficState>& state,
                                         uint64_t tick_id) {
    auto snap = std::make_shared<WeightSnapshot>(g_.edge_count(), tick_id);

    for (uint32_t i = 0; i < g_.edge_count(); ++i) {
        const Edge& e  = g_.edges[i];
        const auto& s  = (i < state.size()) ? state[i] : EdgeTrafficState{};

        float w;
        if (model_ == "bpr") {
            w = bpr_weight(e, s);
        } else {
            w = greenshields_weight(e, s);
        }

        // Exponential smoothing: blend new weight with previous
        float prev = s.prev_weight > 0.0f ? s.prev_weight : e.free_flow_s();
        snap->weights[i] = ALPHA * w + (1.0f - ALPHA) * prev;
    }

    return snap;
}

// ── changed_edges ─────────────────────────────────────────────────────────

std::vector<uint32_t> WeightUpdater::changed_edges(
    const std::vector<EdgeTrafficState>& state) const {
    std::vector<uint32_t> changed;
    for (uint32_t i = 0; i < std::min<uint32_t>(g_.edge_count(), state.size()); ++i) {
        if (std::fabs(state[i].cur_weight - state[i].prev_weight) > DELTA_THRESHOLD)
            changed.push_back(i);
    }
    return changed;
}

} // namespace smartcity
