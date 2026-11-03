// predictor.cpp — Traffic weight predictor (Phase 5)
//
// Strategy: horizon-aware weight extrapolation.
//
// Given current weights (from WeightStore) and a horizon in minutes, we:
//   1. Take current weights as the baseline.
//   2. Apply a time-of-day IST multiplier at (now + horizon_min) relative to now.
//      This models the tendency for traffic to lighten or worsen by IST hour.
//   3. Blend predicted with free-flow: as horizon grows, uncertainty grows,
//      so we regress to free-flow (conservative routing).
//
// Confidence model:
//   weight_predicted(e) = alpha × w_now + (1 - alpha) × ff_e
//   where alpha = exp(-horizon_min / TAU_MIN)   (exponential decay to free-flow)
//   adjusted by the IST tod_ratio at (now + horizon):
//   weight_predicted(e) = w_now × tod_ratio × alpha + ff_e × (1 - alpha)
//
// This is NOT a trained ML model, but a principled heuristic that:
//   - gives free-flow for large horizons (safe conservative routing)
//   - respects diurnal patterns via IST hour multiplier
//   - is O(E) time and O(E) space

#include "predictor.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace smartcity {

namespace {
    constexpr float TAU_MIN = 20.0f;   // exponential decay constant (minutes)

    // IST hour → load multiplier (same table as simulator)
    float tod_multiplier(float ist_hour) {
        if (ist_hour >= 8.0f && ist_hour < 10.0f)  return 2.5f;
        if (ist_hour >= 17.0f && ist_hour < 20.0f) return 2.8f;
        if (ist_hour >= 10.0f && ist_hour < 17.0f) return 1.2f;
        if (ist_hour >= 20.0f && ist_hour < 22.0f) return 1.0f;
        return 0.3f;
    }

    float ist_hour_at(int future_minutes) {
        // Current epoch minutes in IST
        using namespace std::chrono;
        auto now_t = system_clock::to_time_t(system_clock::now());
        int total_min = static_cast<int>(now_t / 60) % (24 * 60)
                        + 330 + future_minutes;  // IST = UTC+330min
        return static_cast<float>(total_min % (24 * 60)) / 60.0f;
    }
} // anon

WeightSnapshotPtr predict(const Graph& g,
                           const WeightStore& ws,
                           int horizon_min) {
    if (horizon_min <= 0) return ws.current();   // no horizon → current weights

    auto current = ws.current();
    if (!current) {
        // Safety: return an empty snapshot
        return std::make_shared<WeightSnapshot>(g.edge_count(), 0);
    }

    auto predicted = std::make_shared<WeightSnapshot>(g.edge_count(),
                                                       current->tick_id);

    float alpha     = std::exp(-static_cast<float>(horizon_min) / TAU_MIN);
    float now_hr    = ist_hour_at(0);
    float future_hr = ist_hour_at(horizon_min);
    float tod_now   = tod_multiplier(now_hr);
    float tod_future = tod_multiplier(future_hr);
    // Ratio of future vs now load — if future is busier, weights go up
    float tod_ratio = (tod_now > 0.0f) ? (tod_future / tod_now) : 1.0f;

    for (uint32_t i = 0; i < g.edge_count(); ++i) {
        float ff  = g.edges[i].free_flow_s();
        float cur = (i < current->weights.size()) ? current->weights[i] : ff;
        // Blend: weighted average of scaled current and free-flow
        float scaled = cur * tod_ratio;
        predicted->weights[i] = alpha * scaled + (1.0f - alpha) * ff;
        // Never below free-flow (routing must be optimistic lower bound)
        predicted->weights[i] = std::max(predicted->weights[i], ff);
    }

    return predicted;
}

} // namespace smartcity
