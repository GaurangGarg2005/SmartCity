#pragma once
// dijkstra.h — Dijkstra's algorithm with an indexed binary heap (priority queue).
//
// Key design decisions:
//   - Reads edge weights from an atomic WeightSnapshot (lock-free reader).
//   - Returns path, total weight, AND stats (nodes settled, edges relaxed, µs).
//   - Can optionally return the settled-node order (for the UI visualizer, capped).
//   - Supports a "free-flow" mode that ignores live weights (uses free_flow_s()).
//   - Non-negative weight enforcement: clamps weights to EPSILON, asserts in debug.

#include "../graph/graph.h"
#include <vector>
#include <optional>
#include <cstdint>
#include <chrono>
#include <memory>
#include <atomic>

namespace smartcity {

// ── WeightSnapshot ──────────────────────────────────────────────────────────
// Published by the simulation thread; readers take a snapshot atomically.
// Weights are in seconds. Index matches Graph::edges[] index.
struct WeightSnapshot {
    std::vector<float> weights;   // per-edge travel time (seconds)
    uint64_t           tick_id;   // incremented each sim tick

    explicit WeightSnapshot(std::size_t n_edges, uint64_t tick = 0)
        : weights(n_edges, 0.0f), tick_id(tick) {}
};

// Thread-safe double-buffer: sim writes, routers read atomically.
using WeightSnapshotPtr = std::shared_ptr<const WeightSnapshot>;

class WeightStore {
public:
    explicit WeightStore(std::size_t n_edges);

    /// Called by sim thread to publish a new snapshot.
    void publish(WeightSnapshotPtr snap);

    /// Called by router threads to obtain current snapshot (lock-free).
    WeightSnapshotPtr current() const;

private:
    std::atomic<WeightSnapshotPtr> ptr_;
};

// ── RouteStep (turn-by-turn) ────────────────────────────────────────────────
struct RouteStep {
    std::string maneuver;    // "turn-left", "turn-right", "straight", "arrive"
    std::string road_name;
    float       distance_m;
    float       duration_s;
    float       lat, lon;    // location of the maneuver
};

// ── RouteStats ──────────────────────────────────────────────────────────────
struct RouteStats {
    uint32_t nodes_settled  = 0;
    uint32_t edges_relaxed  = 0;
    uint32_t dijkstra_us    = 0;   // microseconds
};

// ── DijkstraResult ──────────────────────────────────────────────────────────
struct DijkstraResult {
    bool                      found        = false;
    std::vector<uint32_t>     edge_path;         // sequence of edge indices
    std::vector<uint32_t>     node_path;         // sequence of node indices
    float                     distance_m   = 0;
    float                     duration_s   = 0;  // under given weights
    float                     free_flow_s  = 0;  // always free-flow
    std::vector<RouteStep>    steps;
    RouteStats                stats;

    // Optional: settled-node order for visualizer (capped at VIZ_CAP)
    std::vector<uint32_t>     explored;
};

// ── DijkstraRouter ──────────────────────────────────────────────────────────
class DijkstraRouter {
public:
    static constexpr int VIZ_CAP  = 5000;   // max settled nodes returned
    static constexpr float EPSILON = 0.001f; // min weight (seconds)

    explicit DijkstraRouter(const Graph& g, const WeightStore& ws);

    /// Run Dijkstra from node `from` to node `to`.
    /// If `use_live_weights` is false, uses free-flow edge weights.
    /// If `want_explored` is true, returns settled-node order (capped).
    DijkstraResult route(uint32_t from, uint32_t to,
                         bool     use_live_weights = true,
                         bool     want_explored    = false) const;

private:
    const Graph&       g_;
    const WeightStore& ws_;

    float edge_weight(uint32_t edge_idx,
                      const WeightSnapshot& snap,
                      bool use_live) const;
};

} // namespace smartcity
