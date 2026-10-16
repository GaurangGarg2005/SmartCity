#pragma once
// server.h — SmartCity HTTP + WebSocket API (Phase 3)
// All endpoints documented in docs/API.md

#include <crow.h>
#include "../graph/graph.h"
#include "../graph/spatial_grid.h"
#include "../routing/dijkstra.h"
#include "../routing/alternatives.h"
#include "../routing/polyline.h"
#include "../search/search_index.h"
#include "../sim/simulator.h"
#include "../sim/predictor.h"
#include "../traffic/weight_updater.h"
#include "../util/config.h"
#include "../util/logger.h"
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <string>

namespace smartcity {

// ── Forward declaration ────────────────────────────────────────────────────
class WsHub;

// ── ServerContext: all shared read-only + shared state ───────────────────
struct ServerContext {
    const Graph&        graph;
    const SpatialGrid&  grid;
    WeightStore&        weights;
    const SearchIndex&  search;
    WeightStore&        ws_store;
    Simulator&          sim;
    const Config&       cfg;
};

// ── Per-connection WebSocket state ────────────────────────────────────────
struct WsClientState {
    // Subscribed bounding box (for traffic deltas)
    float sub_west = 0, sub_south = 0, sub_east = 0, sub_north = 0;
    bool  subscribed = false;
    bool  consented  = false;   // share_location consent

    // Route watch
    bool     watching_route   = false;
    uint32_t watch_from       = 0, watch_to = 0;
    float    last_watch_s     = 0;  // last route duration sent
    uint64_t last_reroute_tick = 0;
};

// ── WsHub: broadcast to all connected clients ─────────────────────────────
class WsHub {
public:
    void add(crow::websocket::connection* c);
    void remove(crow::websocket::connection* c);
    void broadcast(const std::string& msg);
    std::size_t size() const;

private:
    mutable std::mutex mu_;
    std::unordered_set<crow::websocket::connection*> conns_;
};

// ── Route builder ─────────────────────────────────────────────────────────
// Builds JSON for a DijkstraResult.
nlohmann::json route_to_json(const DijkstraResult& r,
                              const Graph& g,
                              const std::string& label);

// ── register_routes: attaches all CROW_ROUTEs to app ─────────────────────
void register_routes(crow::App<crow::CORSHandler>& app,
                     ServerContext& ctx,
                     WsHub& hub);

} // namespace smartcity
