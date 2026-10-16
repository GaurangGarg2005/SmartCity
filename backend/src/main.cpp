// main.cpp — SmartCity server entry point (Phase 3)
//
// Startup sequence:
//   1. Load config from env
//   2. Load graph.bin (CSR + spatial grid)
//   3. Load places.json (search index)
//   4. Init WeightStore (free-flow weights)
//   5. Init WeightUpdater, Simulator (paused at start)
//   6. Register all Crow routes
//   7. Start sim (publishes weight snapshots via WS each tick)
//   8. Serve on PORT (default 8080)

#include <crow.h>
#include <crow/middlewares/cors.h>
#include <nlohmann/json.hpp>

#include "api/server.h"
#include "graph/graph.h"
#include "graph/spatial_grid.h"
#include "routing/dijkstra.h"
#include "search/search_index.h"
#include "sim/simulator.h"
#include "traffic/weight_updater.h"
#include "util/config.h"
#include "util/logger.h"

#include <cstdlib>
#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>

using namespace smartcity;

int main() {
    // ── Config ────────────────────────────────────────────────────────────────
    Config cfg = Config::from_env();
    SC_LOG_INFO("main", ("SmartCity starting on port " +
                          std::to_string(cfg.port)).c_str());

    // ── Load graph ────────────────────────────────────────────────────────────
    SC_LOG_INFO("main", ("Loading graph: " + cfg.graph_path).c_str());
    Graph g = Graph::load(cfg.graph_path);
    if (g.empty()) {
        SC_LOG_ERR("main", "Graph is empty — aborting");
        return 1;
    }
    SC_LOG_INFO("main", ("Graph loaded: " +
                          std::to_string(g.node_count()) + " nodes, " +
                          std::to_string(g.edge_count()) + " edges").c_str());

    // ── Spatial grid ─────────────────────────────────────────────────────────
    SC_LOG_INFO("main", "Building spatial grid...");
    SpatialGrid grid(g, 512);
    SC_LOG_INFO("main", "Spatial grid ready");

    // ── Search index ──────────────────────────────────────────────────────────
    SC_LOG_INFO("main", ("Loading places: " + cfg.places_path).c_str());
    SearchIndex search;
    if (!search.load(cfg.places_path)) {
        SC_LOG_WARN("main", "Places index failed to load — search disabled");
    } else {
        SC_LOG_INFO("main", (std::to_string(search.size()) +
                              " places loaded").c_str());
    }

    // ── WeightStore — initialise with free-flow weights ───────────────────────
    WeightStore ws(g.edge_count());
    {
        auto snap = std::make_shared<WeightSnapshot>(g.edge_count(), 0);
        for (uint32_t i = 0; i < g.edge_count(); ++i)
            snap->weights[i] = g.edges[i].free_flow_s();
        ws.publish(snap);
    }
    SC_LOG_INFO("main", "WeightStore initialised (free-flow)");

    // ── WeightUpdater + Simulator ─────────────────────────────────────────────
    WeightUpdater wu(g, cfg.weight_model);
    Simulator     sim(g, ws, wu, cfg.sim_vehicles, cfg.sim_seed);

    // ── WsHub ─────────────────────────────────────────────────────────────────
    WsHub hub;

    // Wire sim tick → weight publish → WS broadcast
    sim.on_tick([&](const WeightSnapshotPtr& snap, const SimStatus& st) {
        ws.publish(snap);
        // Broadcast sim_state to all connected clients
        nlohmann::json msg = {
            {"type",         "sim_state"},
            {"tick",         snap->tick_id},
            {"vehicle_count", st.vehicle_count},
            {"speed_mult",    st.speed_mult},
            {"tick_ms",       st.tick_ms},
            {"reduced_fidelity", st.reduced_fidelity},
            {"data_source",  "SIMULATED LIVE"},
        };
        hub.broadcast(msg.dump());
    });

    // Start simulation
    sim.start();
    SC_LOG_INFO("main", ("Simulator started: " +
                          std::to_string(cfg.sim_vehicles) +
                          " vehicles").c_str());

    // ── Crow app ──────────────────────────────────────────────────────────────
    crow::App<crow::CORSHandler> app;

    // CORS: allow frontend origin
    auto& cors = app.get_middleware<crow::CORSHandler>();
    cors.global()
        .headers("Content-Type", "Authorization")
        .methods("GET"_method, "POST"_method, "OPTIONS"_method)
        .origin(cfg.cors_origin.empty() ? "*" : cfg.cors_origin);

    // Global request size limit (prevent abuse)
    app.server_name("SmartCity/1.0");

    // Wire all routes
    ServerContext ctx{g, grid, ws, search, ws, sim, cfg};
    register_routes(app, ctx, hub);

    // ── Heartbeat thread: ping all WS clients every 25 s ─────────────────────
    std::atomic<bool> running{true};
    std::thread heartbeat_thread([&hub, &running]() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(25));
            if (!running) break;
            hub.broadcast(nlohmann::json{{"type","ping"},
                {"ts", std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()}
            }.dump());
        }
    });

    // ── Run ───────────────────────────────────────────────────────────────────
    SC_LOG_INFO("main", ("Listening on 0.0.0.0:" +
                          std::to_string(cfg.port)).c_str());
    app.bindaddr("0.0.0.0")
       .port(static_cast<uint16_t>(cfg.port))
       .multithreaded()
       .run();

    // ── Shutdown ──────────────────────────────────────────────────────────────
    running = false;
    heartbeat_thread.join();
    sim.stop();
    SC_LOG_INFO("main", "SmartCity shutdown complete");
    return 0;
}
