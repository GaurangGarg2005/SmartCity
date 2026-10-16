#pragma once
// simulator.h — Traffic simulation engine (Phase 4)
//
// Design:
//   • Spawns N synthetic vehicles. Each vehicle is a (current_edge, progress_m)
//     pair. Every tick, vehicles advance and their edge occupancy is accumulated.
//   • Edge occupancy → WeightUpdater → WeightSnapshot → published to WeightStore.
//   • Incidents overlay a congestion multiplier on a specific edge for a fixed
//     duration, implemented as a decaying extra vehicle load.
//   • Tick rate: 1 Hz wall-clock (speed_mult × sim seconds per tick).
//   • Adaptive sleep: if tick takes longer than 900 ms (Render CPU budget), the
//     simulator raises reduced_fidelity = true and skips the next tick.

#include "../graph/graph.h"
#include "../routing/dijkstra.h"
#include "../traffic/weight_updater.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <random>
#include <unordered_map>

namespace smartcity {

enum class SimScenario { LIGHT, MODERATE, HEAVY, RUSH_HOUR, FOLLOW_CLOCK };

struct SimStatus {
    bool        running        = false;
    int         vehicle_count  = 0;
    float       speed_mult     = 1.0f;
    SimScenario scenario       = SimScenario::MODERATE;
    uint64_t    tick_id        = 0;
    float       tick_ms        = 0.0f;
    bool        reduced_fidelity = false;
};

// An active incident on one edge.
struct Incident {
    uint32_t edge_idx;
    float    remaining_s;   // sim-seconds remaining
    float    factor;        // congestion multiplier on that edge's vehicle load
};

// A simulated vehicle agent.
struct Vehicle {
    uint32_t edge_idx;        // current edge
    float    progress_m;      // distance travelled on current edge (0..length_m)
    float    speed_ms;        // current speed (m/s)
};

class Simulator {
public:
    using OnTickFn = std::function<void(const WeightSnapshotPtr&,
                                        const SimStatus&)>;

    explicit Simulator(const Graph& g,
                       WeightStore& ws,
                       const WeightUpdater& wu,
                       int  n_vehicles = 500,
                       int  seed       = 42);
    ~Simulator();

    void start();
    void stop();
    void pause();
    void set_speed(float mult);           // 1.0 – 10.0
    void set_scenario(SimScenario s);
    void set_vehicles(int n);
    void reset();
    void create_incident(uint32_t edge_idx, float duration_s, float factor);
    void on_tick(OnTickFn fn);

    SimStatus status() const;

private:
    const Graph&       g_;
    WeightStore&       ws_;
    const WeightUpdater& wu_;

    std::atomic<int>   n_vehicles_;
    int                seed_;

    std::atomic<bool>  running_{false};
    std::atomic<bool>  paused_{false};
    std::thread        thread_;

    mutable std::mutex mutex_;
    SimStatus          status_;
    OnTickFn           on_tick_;

    // Simulation state (protected by mutex_)
    std::vector<Vehicle>          vehicles_;
    std::vector<EdgeTrafficState> edge_state_;
    std::vector<Incident>         incidents_;
    uint64_t                      tick_id_{0};

    std::mt19937 rng_;

    // Internals
    void _run_loop();
    void _tick(float sim_dt_s);
    void _spawn_vehicle(std::uniform_int_distribution<uint32_t>& edge_dist);
    void _rebalance_vehicles();
    float _scenario_load_factor() const;
    float _time_of_day_factor() const;   // IST hour → load multiplier
    void  _apply_incidents(float dt_s);
};

} // namespace smartcity
