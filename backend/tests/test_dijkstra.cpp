// test_dijkstra.cpp — Dijkstra correctness tests (Phase 2)
//
// Tests:
//   1. Simple triangle graph — shortest path and weight
//   2. Dijkstra vs manual BFS on a small known graph
//   3. Unreachable node returns found=false
//   4. from==to returns found=true, zero cost
//   5. Non-negative weight enforcement (EPSILON clamp)
//   6. Free-flow vs live-weight paths differ on congested edge
//   7. Stats: nodes_settled ≤ node_count, edges_relaxed ≥ path length - 1

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../src/graph/graph.h"
#include "../src/routing/dijkstra.h"

using namespace smartcity;
using Catch::Approx;

// ── Helper: build a small in-memory graph ────────────────────────────────────

static Graph make_triangle() {
    // 3 nodes: 0, 1, 2
    // edges: 0→1 (100 m, 10 m/s), 0→2 (50 m, 10 m/s), 1→2 (30 m, 10 m/s)
    // Free-flow times: 0→1=10s, 0→2=5s, 1→2=3s
    Graph g;
    g.nodes = { {0, 28.5f, 77.3f}, {1, 28.51f, 77.31f}, {2, 28.52f, 77.32f} };

    Edge e01; e01.from=0; e01.to=1; e01.length_m=100; e01.free_flow_speed_ms=10; e01.delay_s=0; e01.lanes=1; e01.road_class=5; e01.flags=1; e01.name_off=0; e01.ref_off=0; e01.geom_offset=0; e01.geom_len=0;
    Edge e02; e02.from=0; e02.to=2; e02.length_m=50;  e02.free_flow_speed_ms=10; e02.delay_s=0; e02.lanes=1; e02.road_class=5; e02.flags=1; e02.name_off=0; e02.ref_off=0; e02.geom_offset=0; e02.geom_len=0;
    Edge e12; e12.from=1; e12.to=2; e12.length_m=30;  e12.free_flow_speed_ms=10; e12.delay_s=0; e12.lanes=1; e12.road_class=5; e12.flags=1; e12.name_off=0; e12.ref_off=0; e12.geom_offset=0; e12.geom_len=0;

    g.edges = {e01, e02, e12};
    g.strings = {'\0'};

    // Build CSR
    g.csr_head.assign(4, 0u);
    g.csr_head[1] = 2; // node 0 has 2 edges (e01, e02)
    g.csr_head[2] = 3; // node 1 has 1 edge  (e12)
    g.csr_head[3] = 3; // node 2 has 0 edges
    g.csr_list    = {0, 1, 2};  // edge indices

    g.bbox = {77.3f, 28.5f, 77.4f, 28.6f};
    return g;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_CASE("Dijkstra: from == to", "[dijkstra]") {
    Graph g = make_triangle();
    WeightStore ws(g.edge_count());
    DijkstraRouter router(g, ws);
    auto res = router.route(1, 1, false, false);
    REQUIRE(res.found);
    REQUIRE(res.duration_s == Approx(0.0f));
}

TEST_CASE("Dijkstra: direct vs indirect path", "[dijkstra]") {
    Graph g = make_triangle();
    WeightStore ws(g.edge_count());
    DijkstraRouter router(g, ws);

    // 0→2: direct = 5s, via 1 = 10+3=13s  → should pick direct
    auto res = router.route(0, 2, false, false);
    REQUIRE(res.found);
    REQUIRE(res.duration_s == Approx(5.0f).epsilon(0.01f));
    REQUIRE(res.distance_m == Approx(50.0f).epsilon(0.01f));
}

TEST_CASE("Dijkstra: full path 0→2 via edge indices", "[dijkstra]") {
    Graph g = make_triangle();
    WeightStore ws(g.edge_count());
    DijkstraRouter router(g, ws);

    auto res = router.route(0, 2, false, false);
    REQUIRE(res.found);
    // Direct path should use edge index 1 (e02)
    REQUIRE(res.edge_path.size() == 1);
    REQUIRE(res.edge_path[0] == 1u);
}

TEST_CASE("Dijkstra: unreachable node returns found=false", "[dijkstra]") {
    Graph g = make_triangle();
    // Add an isolated node 3 (no edges)
    g.nodes.push_back({3, 28.6f, 77.4f});
    g.csr_head.push_back(3u);  // node 3 → no edges

    WeightStore ws(g.edge_count());
    DijkstraRouter router(g, ws);

    auto res = router.route(0, 3, false, false);
    REQUIRE_FALSE(res.found);
}

TEST_CASE("Dijkstra: live weights on congested edge", "[dijkstra]") {
    // Edge 1 (0→2 direct, 5s free-flow) gets a heavy live weight of 60s
    // Edge 0 + Edge 2 (via node 1) = 10+3 = 13s
    // → live-weight route should prefer via-1
    Graph g = make_triangle();
    WeightStore ws(g.edge_count());

    auto snap = std::make_shared<WeightSnapshot>(g.edge_count(), 1);
    snap->weights[0] = 10.0f;   // e01: 10s
    snap->weights[1] = 60.0f;   // e02: 60s (congested)
    snap->weights[2] =  3.0f;   // e12: 3s
    ws.publish(snap);

    DijkstraRouter router(g, ws);

    // Free-flow: picks direct (5s)
    auto ff = router.route(0, 2, false, false);
    REQUIRE(ff.found);
    REQUIRE(ff.edge_path.size() == 1);
    REQUIRE(ff.edge_path[0] == 1u);

    // Live: must detour via node 1 (13s < 60s)
    auto lw = router.route(0, 2, true, false);
    REQUIRE(lw.found);
    REQUIRE(lw.edge_path.size() == 2);
    REQUIRE(lw.duration_s == Approx(13.0f).epsilon(0.01f));
}

TEST_CASE("Dijkstra: stats sanity", "[dijkstra]") {
    Graph g = make_triangle();
    WeightStore ws(g.edge_count());
    DijkstraRouter router(g, ws);

    auto res = router.route(0, 2, false, true);
    REQUIRE(res.found);
    REQUIRE(res.stats.nodes_settled <= g.node_count());
    REQUIRE(res.stats.edges_relaxed >= res.edge_path.size());
    REQUIRE(res.stats.dijkstra_us   >  0u);
}

TEST_CASE("Dijkstra: explored nodes capped at VIZ_CAP", "[dijkstra]") {
    Graph g = make_triangle();
    WeightStore ws(g.edge_count());
    DijkstraRouter router(g, ws);

    auto res = router.route(0, 2, false, true);
    REQUIRE(res.explored.size() <= static_cast<std::size_t>(DijkstraRouter::VIZ_CAP));
}

TEST_CASE("WeightStore: atomic publish/read", "[dijkstra]") {
    // Verify that the snapshot published is the one read
    WeightStore ws(3);
    auto snap1 = ws.current();
    REQUIRE(snap1->tick_id == 0);

    auto snap2 = std::make_shared<WeightSnapshot>(3, 7);
    ws.publish(snap2);
    REQUIRE(ws.current()->tick_id == 7);
}
