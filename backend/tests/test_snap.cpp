// test_snap.cpp — Snap-to-edge tests (Phase 2)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "../src/graph/graph.h"
#include "../src/graph/spatial_grid.h"

using namespace smartcity;
using Catch::Approx;

// ── Helper: build a simple 4-node square graph ────────────────────────────────
//   0 (28.50, 77.30) -- 1 (28.50, 77.31)
//   |                                   |
//   2 (28.51, 77.30) -- 3 (28.51, 77.31)
//   Edges: 0→1, 1→3, 0→2, 2→3 (all ~111 m sides)

static Graph make_square() {
    Graph g;
    g.bbox = { 77.30f, 28.50f, 77.31f, 28.51f };
    g.nodes = {
        {0, 28.50f, 77.30f},
        {1, 28.50f, 77.31f},
        {2, 28.51f, 77.30f},
        {3, 28.51f, 77.31f},
    };

    auto make_edge = [](uint32_t from, uint32_t to,
                        float lat_a, float lon_a,
                        float lat_b, float lon_b) -> Edge {
        Edge e{};
        e.from = from; e.to = to;
        e.length_m = 111.0f;  // approx 0.001 degree at this latitude
        e.free_flow_speed_ms = 10.0f;
        e.delay_s = 0.0f;
        e.lanes = 1; e.road_class = 5; e.flags = 1;
        e.name_off = 0; e.ref_off = 0; e.geom_offset = 0; e.geom_len = 0;
        return e;
    };

    g.edges = {
        make_edge(0,1, 28.50f,77.30f, 28.50f,77.31f),  // 0→1 (horizontal top)
        make_edge(1,3, 28.50f,77.31f, 28.51f,77.31f),  // 1→3 (vertical right)
        make_edge(0,2, 28.50f,77.30f, 28.51f,77.30f),  // 0→2 (vertical left)
        make_edge(2,3, 28.51f,77.30f, 28.51f,77.31f),  // 2→3 (horizontal bottom)
    };
    g.strings = {'\0'};

    // CSR: node 0 → edges 0,2; node 1 → edge 1; node 2 → edge 3; node 3 → none
    g.csr_head = {0, 2, 3, 4, 4};
    g.csr_list = {0, 2, 1, 3};

    return g;
}

TEST_CASE("SpatialGrid: snap point on edge 0→1 midpoint", "[snap]") {
    Graph g = make_square();
    SpatialGrid grid(g, 32);

    // Query exactly the midpoint of edge 0→1 (top horizontal edge)
    float mid_lat = 28.50f;
    float mid_lon = 77.305f;  // midpoint
    auto res = grid.snap_to_edge(mid_lat, mid_lon, g, 500.0f);

    REQUIRE(res.valid);
    REQUIRE(res.dist_m < 50.0f);  // should be very close
}

TEST_CASE("SpatialGrid: snap query outside max_dist returns invalid", "[snap]") {
    Graph g = make_square();
    SpatialGrid grid(g, 32);

    // Query far outside the graph bbox
    auto res = grid.snap_to_edge(30.0f, 80.0f, g, 100.0f);
    REQUIRE_FALSE(res.valid);
}

TEST_CASE("SpatialGrid: empty graph returns invalid", "[snap]") {
    Graph g;
    g.bbox = {77.0f, 28.0f, 78.0f, 29.0f};
    SpatialGrid grid;
    auto res = grid.snap_to_edge(28.5f, 77.3f, g, 500.0f);
    REQUIRE_FALSE(res.valid);
}
