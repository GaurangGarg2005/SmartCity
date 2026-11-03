// predictor.cpp — Traffic weight predictor (Phase 5)
#include "predictor.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace smartcity {

namespace {
    constexpr float TAU_MIN = 25.0f;   // exponential decay constant (minutes)

    // IST hour → load multiplier (same table as simulator)
    float tod_multiplier(float ist_hour) {
        if (ist_hour >= 8.0f && ist_hour < 10.0f)  return 2.5f;
        if (ist_hour >= 17.0f && ist_hour < 20.0f) return 2.8f;
        if (ist_hour >= 10.0f && ist_hour < 17.0f) return 1.2f;
        if (ist_hour >= 20.0f && ist_hour < 22.0f) return 1.0f;
        return 0.3f;
    }

    float ist_hour_at(int future_minutes) {
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
        return std::make_shared<WeightSnapshot>(g.edge_count(), 0);
    }

    auto predicted = std::make_shared<WeightSnapshot>(g.edge_count(),
                                                       current->tick_id);

    float alpha      = std::exp(-static_cast<float>(horizon_min) / TAU_MIN);
    float now_hr     = ist_hour_at(0);
    float future_hr  = ist_hour_at(horizon_min);
    float tod_now    = tod_multiplier(now_hr);
    float tod_future = tod_multiplier(future_hr);
    float tod_ratio  = (tod_now > 0.0f) ? (tod_future / tod_now) : 1.0f;

    for (uint32_t i = 0; i < g.edge_count(); ++i) {
        float ff  = g.edges[i].free_flow_s();
        float cur = (i < current->weights.size()) ? current->weights[i] : ff;

        // Dynamic horizon-based traffic buildup on major arterials and junctions:
        // As departure horizon increases (5..30 min), corridor density accumulates
        float horizon_buildup = 1.0f;
        if (g.edges[i].road_class <= 2) {
            // Arterials & expressways: significant congestion buildup over 5..30 min
            float pattern = 1.2f + 0.8f * std::sin(static_cast<float>(i % 31));
            horizon_buildup = 1.0f + (horizon_min * 0.035f) * pattern;
        } else if (g.edges[i].road_class <= 4) {
            float pattern = 1.0f + 0.6f * std::cos(static_cast<float>(i % 23));
            horizon_buildup = 1.0f + (horizon_min * 0.022f) * pattern;
        } else {
            horizon_buildup = 1.0f + (horizon_min * 0.008f);
        }

        float scaled = cur * tod_ratio * horizon_buildup;
        predicted->weights[i] = std::max(ff, alpha * scaled + (1.0f - alpha) * ff * horizon_buildup);
    }

    return predicted;
}

} // namespace smartcity
