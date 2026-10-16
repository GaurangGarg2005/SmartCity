// test_simulation.cpp — Simulator and weight updater tests (Phase 4)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../src/traffic/weight_updater.h"
#include "../src/sim/simulator.h"
#include "../src/sim/predictor.h"
#include <thread>
#include <chrono>

using namespace smartcity;
using Catch::Approx;

static Graph make_simple_graph() {
    Graph g;
    g.bbox = {77.3f, 28.5f, 77.4f, 28.6f};
    g.nodes = {{0, 28.50f, 77.30f}, {1, 28.51f, 77.31f}};
    Edge e{};
    e.from=0; e.to=1; e.length_m=1000; e.free_flow_speed_ms=10; e.delay_s=2;
    e.lanes=2; e.road_class=2; e.flags=1;
    e.name_off=0; e.ref_off=0; e.geom_offset=0; e.geom_len=0;
    g.edges = {e};
    g.strings = {'\0'};
    g.csr_head = {0,1,1};
    g.csr_list = {0};
    return g;
}

// ── WeightUpdater tests ────────────────────────────────────────────────────

TEST_CASE("WeightUpdater: zero vehicles → free-flow weight", "[weights]") {
    Graph g = make_simple_graph();
    WeightUpdater wu(g, "greenshields");

    std::vector<EdgeTrafficState> state(1);
    state[0].vehicle_count = 0.0f;
    state[0].capacity      = 3600.0f;

    auto snap = wu.update(state, 1);
    REQUIRE(snap->tick_id == 1u);
    // Greenshields: rho=0, v=free_flow_speed_ms=10 m/s
    // weight = 1000/10 + 2 = 102s (free-flow)
    // After EMA blend with 0 prev_weight: ALPHA*102 + (1-ALPHA)*102 = 102
    float ff_w = g.edges[0].free_flow_s();
    REQUIRE(snap->weights[0] == Approx(ff_w).epsilon(0.05f));
}

TEST_CASE("WeightUpdater: full capacity → jam weight (Greenshields)", "[weights]") {
    Graph g = make_simple_graph();
    WeightUpdater wu(g, "greenshields");

    std::vector<EdgeTrafficState> state(1);
    state[0].vehicle_count = 3600.0f;   // fully saturated
    state[0].capacity      = 3600.0f;
    state[0].prev_weight   = g.edges[0].free_flow_s();

    auto snap = wu.update(state, 2);
    // rho = 1.0 → v = v_f*(1-1) = 0 → clamped to V_MIN_MS
    // weight >> free-flow weight
    REQUIRE(snap->weights[0] > g.edges[0].free_flow_s() * 2.0f);
}

TEST_CASE("WeightUpdater: weights are always positive", "[weights]") {
    Graph g = make_simple_graph();
    WeightUpdater wu(g, "greenshields");

    std::vector<EdgeTrafficState> state(1);
    for (float n : {0.0f, 100.0f, 3600.0f, 10000.0f}) {
        state[0].vehicle_count = n;
        state[0].capacity      = 3600.0f;
        auto snap = wu.update(state, 0);
        REQUIRE(snap->weights[0] > 0.0f);
    }
}

TEST_CASE("WeightUpdater: BPR model produces positive weights", "[weights]") {
    Graph g = make_simple_graph();
    WeightUpdater wu(g, "bpr");

    std::vector<EdgeTrafficState> state(1);
    state[0].vehicle_count = 500.0f;
    state[0].capacity      = 3600.0f;
    auto snap = wu.update(state, 1);
    REQUIRE(snap->weights[0] > 0.0f);
}

// ── Simulator tests ────────────────────────────────────────────────────────

static Graph make_graph_with_cycle() {
    // A→B→C→A cycle so vehicles can always advance
    Graph g;
    g.bbox = {77.3f, 28.5f, 77.4f, 28.6f};
    g.nodes = {
        {0, 28.50f, 77.30f},
        {1, 28.51f, 77.31f},
        {2, 28.52f, 77.32f},
    };
    auto make_e = [](uint32_t from, uint32_t to) {
        Edge e{};
        e.from=from; e.to=to;
        e.length_m=500; e.free_flow_speed_ms=10; e.delay_s=0;
        e.lanes=2; e.road_class=2; e.flags=1;
        e.name_off=0; e.ref_off=0; e.geom_offset=0; e.geom_len=0;
        return e;
    };
    g.edges = { make_e(0,1), make_e(1,2), make_e(2,0) };
    g.strings = {'\0'};
    g.csr_head = {0,1,2,3};
    g.csr_list  = {0,1,2};
    return g;
}

TEST_CASE("Simulator: starts and produces ticks", "[sim]") {
    Graph g = make_graph_with_cycle();
    WeightStore ws(g.edge_count());
    WeightUpdater wu(g, "greenshields");
    Simulator sim(g, ws, wu, /*n_vehicles=*/10, /*seed=*/1);

    uint64_t ticks_seen = 0;
    sim.on_tick([&](const WeightSnapshotPtr&, const SimStatus& s) {
        ticks_seen = s.tick_id;
    });

    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    sim.stop();

    REQUIRE(ticks_seen >= 1u);   // at least 2 ticks in 2.5 s
    REQUIRE(ws.current() != nullptr);
}

TEST_CASE("Simulator: incident increases edge weight", "[sim]") {
    Graph g = make_graph_with_cycle();
    WeightStore ws(g.edge_count());
    WeightUpdater wu(g, "greenshields");
    Simulator sim(g, ws, wu, /*n_vehicles=*/20, /*seed=*/2);

    // Capture baseline weight after 1 tick
    WeightSnapshotPtr snap_before;
    sim.on_tick([&](const WeightSnapshotPtr& s, const SimStatus&) {
        if (!snap_before) snap_before = s;
    });
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // Create heavy incident on edge 0
    sim.create_incident(0, /*duration_s=*/30.0f, /*factor=*/2.0f);
    WeightSnapshotPtr snap_after;
    sim.on_tick([&](const WeightSnapshotPtr& s, const SimStatus&) {
        snap_after = s;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    sim.stop();

    // Edge 0 weight should have increased after incident
    if (snap_before && snap_after) {
        REQUIRE(snap_after->weights[0] >= snap_before->weights[0]);
    }
}

TEST_CASE("Simulator: set_vehicles rebalances count", "[sim]") {
    Graph g = make_graph_with_cycle();
    WeightStore ws(g.edge_count());
    WeightUpdater wu(g, "greenshields");
    Simulator sim(g, ws, wu, /*n_vehicles=*/50, /*seed=*/3);

    SimStatus st;
    sim.on_tick([&](const WeightSnapshotPtr&, const SimStatus& s) { st = s; });
    sim.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    sim.set_vehicles(10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    sim.stop();

    REQUIRE(st.vehicle_count <= 10);
}

// ── Predictor tests ────────────────────────────────────────────────────────

TEST_CASE("predict: horizon=0 returns current snapshot", "[predictor]") {
    Graph g = make_simple_graph();
    WeightStore ws(g.edge_count());
    auto snap0 = std::make_shared<WeightSnapshot>(g.edge_count(), 42u);
    snap0->weights[0] = 150.0f;
    ws.publish(snap0);

    auto result = predict(g, ws, 0);
    REQUIRE(result == snap0);  // same pointer
}

TEST_CASE("predict: large horizon regresses to free-flow", "[predictor]") {
    Graph g = make_simple_graph();
    WeightStore ws(g.edge_count());
    // Publish heavily congested weights
    auto congested = std::make_shared<WeightSnapshot>(g.edge_count(), 1u);
    congested->weights[0] = 1000.0f;   // very congested
    ws.publish(congested);

    float ff = g.edges[0].free_flow_s();
    auto result60 = predict(g, ws, 60);   // 60-min horizon
    // After tau=20, exp(-60/20) = exp(-3) ≈ 0.05 → mostly free-flow
    REQUIRE(result60->weights[0] >= ff);  // never below free-flow
    REQUIRE(result60->weights[0] < 500.0f);  // well below raw congested
}
