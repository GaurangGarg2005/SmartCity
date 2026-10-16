// simulator.cpp — Traffic simulation engine (Phase 4)
//
// Algorithm summary
// ─────────────────
//   Each tick (1 Hz wall-clock):
//     1. Compute sim_dt = speed_mult × WALL_TICK_S × scenario_load_factor
//     2. Advance every vehicle by speed × dt along its current edge.
//        When a vehicle exits an edge, pick a random outgoing edge from its node.
//        Vehicles that can't advance (dead-end) are respawned.
//     3. Accumulate per-edge vehicle counts from vehicle positions.
//     4. Apply incidents (extra load on incident edges).
//     5. Call WeightUpdater::update() → publish to WeightStore.
//     6. Invoke on_tick_ callback (broadcasts via WS).
//
// Vehicle speed: uniformly distributed 60–80% of free-flow speed on their edge,
// jittered each tick to simulate realistic variance.
//
// Memory: O(N_vehicles) for vehicles[], O(E) for edge_state[].
// CPU: O(N + E) per tick — fast enough for 0.1 CPU on Render free tier.

#include "simulator.h"
#include "../util/logger.h"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <cassert>

namespace smartcity {

namespace {
    constexpr float WALL_TICK_S     = 1.0f;   // 1 Hz wall-clock ticks
    constexpr float MAX_TICK_MS     = 900.0f;  // Render 0.1 CPU budget safety
    constexpr float SPEED_RATIO_MIN = 0.55f;   // vehicle speed ratio vs free-flow
    constexpr float SPEED_RATIO_MAX = 0.85f;

    // IST = UTC+5:30. We can compute IST hour from UTC.
    float ist_hour_now() {
        auto now = std::chrono::system_clock::now();
        auto t   = std::chrono::system_clock::to_time_t(now);
        // UTC time + 330 minutes = IST
        int total_min = static_cast<int>(t / 60) % (24 * 60) + 330;
        return static_cast<float>(total_min % (24 * 60)) / 60.0f;
    }

    SimScenario string_to_scenario(const std::string& s) {
        if (s == "light")        return SimScenario::LIGHT;
        if (s == "heavy")        return SimScenario::HEAVY;
        if (s == "rush_hour")    return SimScenario::RUSH_HOUR;
        if (s == "follow_clock") return SimScenario::FOLLOW_CLOCK;
        return SimScenario::MODERATE;
    }
} // anon namespace

// ── Constructor / Destructor ───────────────────────────────────────────────

Simulator::Simulator(const Graph& g, WeightStore& ws,
                     const WeightUpdater& wu, int n_vehicles, int seed)
    : g_(g), ws_(ws), wu_(wu),
      n_vehicles_(n_vehicles), seed_(seed),
      rng_(static_cast<uint32_t>(seed))
{
    edge_state_.resize(g_.edge_count());

    // Pre-compute capacities
    for (uint32_t i = 0; i < g_.edge_count(); ++i) {
        const Edge& e = g_.edges[i];
        edge_state_[i].capacity = static_cast<float>(std::max(1, (int)e.lanes))
                                  * WeightUpdater::CAP_PER_LANE / 3600.0f;
        // Convert from veh/hr/lane to veh (instantaneous on edge)
        // Capacity in veh: length_m / (veh_length_m) × lanes, approximated:
        edge_state_[i].capacity = static_cast<float>(std::max(1, (int)e.lanes)) * 15.0f;
    }
}

Simulator::~Simulator() { stop(); }

// ── Public API ─────────────────────────────────────────────────────────────

void Simulator::start() {
    if (running_.exchange(true)) return;  // already running
    paused_  = false;
    thread_  = std::thread([this] { _run_loop(); });
    SC_LOG_INFO("sim", "Simulator thread started");
}

void Simulator::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void Simulator::pause() { paused_ = true; }

void Simulator::set_speed(float mult) {
    std::lock_guard<std::mutex> lk(mutex_);
    status_.speed_mult = std::clamp(mult, 0.1f, 20.0f);
}

void Simulator::set_scenario(SimScenario s) {
    std::lock_guard<std::mutex> lk(mutex_);
    status_.scenario = s;
}

void Simulator::set_vehicles(int n) {
    n_vehicles_.store(std::clamp(n, 50, 5000));
}

void Simulator::reset() {
    std::lock_guard<std::mutex> lk(mutex_);
    vehicles_.clear();
    incidents_.clear();
    tick_id_ = 0;
    for (auto& s : edge_state_) s.vehicle_count = 0;
}

void Simulator::create_incident(uint32_t edge_idx, float duration_s, float factor) {
    if (edge_idx >= g_.edge_count()) return;
    std::lock_guard<std::mutex> lk(mutex_);
    incidents_.push_back({ edge_idx, duration_s, std::clamp(factor, 0.05f, 5.0f) });
    SC_LOG_INFO("sim", ("Incident on edge " + std::to_string(edge_idx) +
                         " for " + std::to_string(duration_s) + "s").c_str());
}

void Simulator::on_tick(OnTickFn fn) {
    std::lock_guard<std::mutex> lk(mutex_);
    on_tick_ = std::move(fn);
}

SimStatus Simulator::status() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return status_;
}

// ── Main loop ──────────────────────────────────────────────────────────────

void Simulator::_run_loop() {
    using clock = std::chrono::steady_clock;
    using ms    = std::chrono::milliseconds;
    using us    = std::chrono::microseconds;

    // Initial vehicle spawn
    {
        std::lock_guard<std::mutex> lk(mutex_);
        _rebalance_vehicles();
    }

    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(ms(200));
            continue;
        }

        auto tick_start = clock::now();

        float speed_mult, sim_dt_s;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            speed_mult = status_.speed_mult;
            sim_dt_s   = WALL_TICK_S * speed_mult * _scenario_load_factor();
            _tick(sim_dt_s);
        }

        auto tick_end  = clock::now();
        float tick_ms  = static_cast<float>(
            std::chrono::duration_cast<us>(tick_end - tick_start).count()) / 1000.0f;

        {
            std::lock_guard<std::mutex> lk(mutex_);
            status_.tick_ms = tick_ms;
            status_.reduced_fidelity = (tick_ms > MAX_TICK_MS);
        }

        // Sleep for the remainder of the 1-second wall tick
        long sleep_us = static_cast<long>(
            std::max(0.0f, (WALL_TICK_S * 1000.0f - tick_ms) * 1000.0f));
        std::this_thread::sleep_for(us(sleep_us));
    }

    SC_LOG_INFO("sim", "Simulator thread stopped");
}

// ── Tick ───────────────────────────────────────────────────────────────────

void Simulator::_tick(float sim_dt_s) {
    // 1. Clear per-edge counts
    for (auto& s : edge_state_) {
        s.prev_weight = s.cur_weight;
        s.vehicle_count = 0.0f;
    }

    // 2. Rebalance vehicle count to n_vehicles_
    _rebalance_vehicles();

    // 3. Advance vehicles
    std::uniform_real_distribution<float> speed_jitter(SPEED_RATIO_MIN, SPEED_RATIO_MAX);
    std::uniform_int_distribution<uint32_t> edge_dist(0, g_.edge_count() - 1);

    for (auto& v : vehicles_) {
        if (v.edge_idx >= g_.edge_count()) {
            _spawn_vehicle(edge_dist); continue;
        }
        const Edge& e = g_.edges[v.edge_idx];
        v.speed_ms = e.free_flow_speed_ms * speed_jitter(rng_);
        v.progress_m += v.speed_ms * sim_dt_s;

        if (v.progress_m >= e.length_m) {
            // Reached end of edge — pick next edge from destination node
            uint32_t to_node = e.to;
            if (to_node < g_.node_count()) {
                auto out = g_.out_edges(to_node);
                if (!out.empty()) {
                    std::uniform_int_distribution<size_t> pick(0, out.size() - 1);
                    v.edge_idx    = out[pick(rng_)];
                    v.progress_m  = 0.0f;
                } else {
                    _spawn_vehicle(edge_dist);
                }
            } else {
                _spawn_vehicle(edge_dist);
            }
        }

        // Accumulate occupancy
        if (v.edge_idx < edge_state_.size()) {
            edge_state_[v.edge_idx].vehicle_count += 1.0f;
        }
    }

    // 4. Incidents
    _apply_incidents(sim_dt_s);

    // 5. Update weights
    ++tick_id_;
    auto snap = wu_.update(edge_state_, tick_id_);

    // Update edge state with new weights
    for (uint32_t i = 0; i < g_.edge_count() && i < edge_state_.size(); ++i) {
        edge_state_[i].cur_weight  = snap->weights[i];
        edge_state_[i].prev_weight = snap->weights[i];
    }

    // 6. Publish to WeightStore
    ws_.publish(snap);

    // 7. Update status
    status_.running       = true;
    status_.vehicle_count = static_cast<int>(vehicles_.size());
    status_.tick_id       = tick_id_;

    // 8. Invoke on_tick callback
    if (on_tick_) {
        on_tick_(snap, status_);
    }
}

// ── Helpers ────────────────────────────────────────────────────────────────

void Simulator::_spawn_vehicle(std::uniform_int_distribution<uint32_t>& edge_dist) {
    Vehicle v;
    v.edge_idx    = edge_dist(rng_);
    v.progress_m  = 0.0f;
    v.speed_ms    = 0.0f;  // set in tick
    vehicles_.push_back(v);
}

void Simulator::_rebalance_vehicles() {
    int target = n_vehicles_.load();
    int current = static_cast<int>(vehicles_.size());

    if (current < target) {
        std::uniform_int_distribution<uint32_t> edge_dist(0, g_.edge_count() - 1);
        for (int i = current; i < target; ++i) {
            _spawn_vehicle(edge_dist);
        }
    } else if (current > target) {
        vehicles_.resize(static_cast<size_t>(target));
    }
}

float Simulator::_scenario_load_factor() const {
    switch (status_.scenario) {
        case SimScenario::LIGHT:       return 0.4f;
        case SimScenario::MODERATE:    return 1.0f;
        case SimScenario::HEAVY:       return 1.8f;
        case SimScenario::RUSH_HOUR:   return 2.5f;
        case SimScenario::FOLLOW_CLOCK: return _time_of_day_factor();
        default: return 1.0f;
    }
}

float Simulator::_time_of_day_factor() const {
    float h = ist_hour_now();
    // Morning rush: 8–10, Evening rush: 17–20, Night: 0–5
    if (h >= 8.0f && h < 10.0f)  return 2.5f;   // rush
    if (h >= 17.0f && h < 20.0f) return 2.8f;   // evening rush (heaviest)
    if (h >= 10.0f && h < 17.0f) return 1.2f;   // business hours
    if (h >= 20.0f && h < 22.0f) return 1.0f;   // early night
    return 0.3f;                                 // late night
}

void Simulator::_apply_incidents(float dt_s) {
    for (auto it = incidents_.begin(); it != incidents_.end(); ) {
        it->remaining_s -= dt_s;
        if (it->remaining_s <= 0.0f) {
            it = incidents_.erase(it);
        } else {
            // Add extra virtual vehicles on the incident edge to model congestion
            if (it->edge_idx < edge_state_.size()) {
                float extra = edge_state_[it->edge_idx].capacity * it->factor;
                edge_state_[it->edge_idx].vehicle_count += extra;
            }
            ++it;
        }
    }
}

} // namespace smartcity
