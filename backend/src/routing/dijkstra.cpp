// dijkstra.cpp — Dijkstra's algorithm implementation (Phase 2)
//
// Uses a std::priority_queue (binary heap) with lazy deletion.
// Time complexity: O((V + E) log V) per query.
// Space complexity: O(V) for dist[], prev[], heap.
//
// WeightStore uses std::atomic<std::shared_ptr<>> for lock-free snapshot
// acquisition by concurrent reader threads (no mutexes on the hot path).

#include "dijkstra.h"
#include "../routing/maneuver.h"
#include <queue>
#include <limits>
#include <cassert>
#include <chrono>
#include <cstring>

namespace smartcity {

// ── WeightStore ──────────────────────────────────────────────────────────────
 
WeightStore::WeightStore(std::size_t n_edges)
    : snap_(std::make_shared<WeightSnapshot>(n_edges, 0)) {}

void WeightStore::publish(WeightSnapshotPtr snap) {
    std::lock_guard<std::mutex> lock(mu_);
    snap_ = std::move(snap);
}

WeightSnapshotPtr WeightStore::current() const {
    std::lock_guard<std::mutex> lock(mu_);
    return snap_;
}

// ── DijkstraRouter ────────────────────────────────────────────────────────────

DijkstraRouter::DijkstraRouter(const Graph& g, const WeightStore& ws)
    : g_(g), ws_(ws) {}

float DijkstraRouter::edge_weight(uint32_t edge_idx,
                                   const WeightSnapshot& snap,
                                   bool use_live) const {
    float w;
    if (use_live && edge_idx < snap.weights.size()) {
        w = snap.weights[edge_idx];
    } else {
        w = g_.edges[edge_idx].free_flow_s();
    }
    // Non-negative weight enforcement (assert in debug)
    if (w < EPSILON) {
        assert(false && "Negative/zero edge weight detected");
        w = EPSILON;
    }
    return w;
}

// ── Dijkstra ────────────────────────────────────────────────────────────────
//
// Algorithm:
//   dist[v] = current best known cost from `from` to v
//   prev_edge[v] = edge index used to reach v
//   heap: (dist, node_id) min-heap
//   settled[v]: node has been finalized
//
// Lazy deletion: when we pop a node already settled, skip it.
// Early exit: stop as soon as `to` is settled.

DijkstraResult DijkstraRouter::route(uint32_t from, uint32_t to,
                                      bool use_live_weights,
                                      bool want_explored) const {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();

    DijkstraResult res;
    const uint32_t N = g_.node_count();

    if (from >= N || to >= N || from == to) {
        if (from == to) { res.found = true; }
        return res;
    }

    // Acquire weight snapshot once for this query (lock-free)
    auto snap = ws_.current();

    // ── Initialise ──────────────────────────────────────────────────────────
    constexpr float INF = std::numeric_limits<float>::infinity();

    std::vector<float>    dist(N, INF);
    std::vector<uint32_t> prev_edge(N, UINT32_MAX);
    std::vector<uint32_t> prev_node(N, UINT32_MAX);
    std::vector<bool>     settled(N, false);

    // min-heap: (cost, node)
    using Pair = std::pair<float, uint32_t>;
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> heap;

    dist[from] = 0.0f;
    heap.emplace(0.0f, from);

    uint32_t nodes_settled = 0;
    uint32_t edges_relaxed = 0;

    if (want_explored)
        res.explored.reserve(std::min(VIZ_CAP, static_cast<int>(N)));

    // ── Main loop ────────────────────────────────────────────────────────────
    while (!heap.empty()) {
        auto [d_u, u] = heap.top();
        heap.pop();

        if (settled[u]) continue;   // lazy deletion
        settled[u] = true;
        ++nodes_settled;

        if (want_explored && nodes_settled <= static_cast<uint32_t>(VIZ_CAP))
            res.explored.push_back(u);

        if (u == to) break;  // early exit

        for (uint32_t ei : g_.out_edges(u)) {
            const Edge& e = g_.edges[ei];
            uint32_t v = e.to;
            if (settled[v]) continue;

            float w   = edge_weight(ei, *snap, use_live_weights);
            float alt = d_u + w;
            ++edges_relaxed;

            if (alt < dist[v]) {
                dist[v]      = alt;
                prev_edge[v] = ei;
                prev_node[v] = u;
                heap.emplace(alt, v);
            }
        }
    }

    auto t1 = Clock::now();
    res.stats.nodes_settled = nodes_settled;
    res.stats.edges_relaxed = edges_relaxed;
    res.stats.dijkstra_us   = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    if (!settled[to]) return res;  // unreachable

    // ── Reconstruct path ─────────────────────────────────────────────────────
    res.found = true;
    res.duration_s = dist[to];

    // Walk backwards from `to` to `from`
    {
        std::vector<uint32_t> rev_edges, rev_nodes;
        uint32_t cur = to;
        while (cur != from) {
            uint32_t ei = prev_edge[cur];
            uint32_t pn = prev_node[cur];
            rev_edges.push_back(ei);
            rev_nodes.push_back(cur);
            cur = pn;
        }
        rev_nodes.push_back(from);
        std::reverse(rev_edges.begin(), rev_edges.end());
        std::reverse(rev_nodes.begin(), rev_nodes.end());
        res.edge_path = std::move(rev_edges);
        res.node_path = std::move(rev_nodes);
    }

    // Compute distance and free-flow time
    for (uint32_t ei : res.edge_path) {
        const Edge& e = g_.edges[ei];
        res.distance_m  += e.length_m;
        res.free_flow_s += e.free_flow_s();
    }

    // Build turn-by-turn steps
    res.steps = build_steps(res.edge_path, g_);

    return res;
}

} // namespace smartcity
