// server.cpp — SmartCity REST + WebSocket API (Phase 3)
//
// Endpoints (all documented in docs/API.md):
//   GET  /healthz
//   GET  /api/meta
//   GET  /api/search
//   GET  /api/reverse
//   POST /api/route
//   GET  /api/traffic
//   POST /api/sim/control
//   POST /api/sim/incident
//   WS   /ws

#include "server.h"
#include "../routing/polyline.h"
#include "../sim/predictor.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace smartcity {

using json = nlohmann::json;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static json make_error(int code, const std::string& msg) {
    return json{{"error", msg}, {"code", code}};
}

static void set_json_headers(crow::response& res) {
    res.set_header("Content-Type", "application/json; charset=utf-8");
    res.set_header("Cache-Control", "no-store");
}

static void set_security_headers(crow::response& res) {
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
    // CSP: allow OpenFreeMap tiles + fonts
    res.set_header("Content-Security-Policy",
        "default-src 'self'; "
        "script-src 'self' 'unsafe-inline'; "
        "style-src 'self' 'unsafe-inline' https://fonts.googleapis.com; "
        "font-src 'self' https://fonts.gstatic.com; "
        "connect-src 'self' wss: ws: https://tiles.openfreemap.org; "
        "img-src 'self' data: https://tiles.openfreemap.org blob:; "
        "worker-src blob: 'self';");
}

// ─── WsHub ────────────────────────────────────────────────────────────────────

void WsHub::add(crow::websocket::connection* c) {
    std::lock_guard<std::mutex> lock(mu_);
    conns_.insert(c);
}

void WsHub::remove(crow::websocket::connection* c) {
    std::lock_guard<std::mutex> lock(mu_);
    conns_.erase(c);
}

void WsHub::broadcast(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto* c : conns_) {
        try { c->send_text(msg); } catch (...) {}
    }
}

std::size_t WsHub::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return conns_.size();
}

// ─── route_to_json ────────────────────────────────────────────────────────────

nlohmann::json route_to_json(const DijkstraResult& r,
                              const Graph& g,
                              const std::string& label) {
    json route;
    route["label"]           = label;
    route["distance_m"]      = r.distance_m;
    route["duration_s"]      = r.duration_s;
    route["free_flow_s"]     = r.free_flow_s;
    route["delay_s"]         = std::max(0.0f, r.duration_s - r.free_flow_s);
    route["stats"]["nodes_settled"]  = r.stats.nodes_settled;
    route["stats"]["edges_relaxed"]  = r.stats.edges_relaxed;
    route["stats"]["dijkstra_ms"]    = r.stats.dijkstra_us / 1000.0f;

    // Geometry as Polyline6
    std::vector<std::pair<float,float>> pts;
    pts.reserve(r.node_path.size());
    for (uint32_t nid : r.node_path) {
        if (nid < g.node_count()) {
            const auto& n = g.nodes[nid];
            pts.emplace_back(n.lat, n.lon);
        }
    }
    route["geometry"] = encode_polyline6(pts);

    // GeoJSON coordinates for the route line
    json coords = json::array();
    for (auto& [lat, lon] : pts) coords.push_back({lon, lat});
    route["geometry_geojson"] = coords;

    // Turn-by-turn steps
    json steps = json::array();
    for (const auto& s : r.steps) {
        steps.push_back({
            {"maneuver",   s.maneuver},
            {"road_name",  s.road_name},
            {"distance_m", s.distance_m},
            {"duration_s", s.duration_s},
            {"location",   {s.lon, s.lat}},
        });
    }
    route["steps"] = steps;

    // Congestion segments: classify from total route delay ratio
    json segs = json::array();
    float route_ratio = r.free_flow_s > 0.001f ? r.duration_s / r.free_flow_s : 1.0f;
    for (std::size_t i = 0; i < r.edge_path.size(); ++i) {
        uint32_t ei = r.edge_path[i];
        if (ei < g.edge_count()) {
            std::string level = "free";
            if (route_ratio > 1.5f)       level = "heavy";
            else if (route_ratio > 1.2f)  level = "moderate";
            else if (route_ratio > 1.05f) level = "slow";
            segs.push_back({{"edge_idx", ei}, {"level", level}, {"seg", (int)i}});
        }
    }
    route["congestion"] = segs;

    return route;
}

// ─── register_routes ─────────────────────────────────────────────────────────

void register_routes(crow::App<crow::CORSHandler>& app,
                     ServerContext& ctx,
                     WsHub& hub) {

    const Graph&       g    = ctx.graph;
    const SpatialGrid& grid = ctx.grid;
    WeightStore&       ws   = ctx.weights;
    const SearchIndex& si   = ctx.search;
    Simulator&         sim  = ctx.sim;
    const Config&      cfg  = ctx.cfg;

    // ── GET /healthz ─────────────────────────────────────────────────────────
    CROW_ROUTE(app, "/healthz")
    ([]() {
        return crow::response(200, "ok");
    });

    // ── GET /api/meta ─────────────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/meta")
    ([&g, &si, &sim, &cfg, &hub]() {
        json body = {
            {"bbox",   {g.bbox.west, g.bbox.south, g.bbox.east, g.bbox.north}},
            {"graph",  {
                {"nodes", g.node_count()},
                {"edges", g.edge_count()},
            }},
            {"places", si.size()},
            {"sim",    {
                {"running",       sim.status().running},
                {"vehicle_count", sim.status().vehicle_count},
                {"speed_mult",    sim.status().speed_mult},
                {"tick_id",       sim.status().tick_id},
            }},
            {"ws_clients", hub.size()},
            {"features",   {
                {"live_user_locations", true},
                {"tomtom_traffic",      !cfg.tomtom_key.empty()},
                {"open_meteo",          false},
            }},
            {"data_source", "SIMULATED LIVE"},
        };
        crow::response res(body.dump());
        set_json_headers(res);
        set_security_headers(res);
        return res;
    });

    // ── GET /api/search ───────────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/search")
    ([&si, &g](const crow::request& req) {
        auto q     = req.url_params.get("q");
        auto lat_s = req.url_params.get("lat");
        auto lon_s = req.url_params.get("lon");
        auto lim_s = req.url_params.get("limit");

        if (!q || std::string(q).empty()) {
            crow::response res(400, make_error(400, "q required").dump());
            set_json_headers(res);
            return res;
        }

        float bias_lat = g.bbox.south + (g.bbox.north - g.bbox.south) * 0.5f;
        float bias_lon = g.bbox.west  + (g.bbox.east  - g.bbox.west ) * 0.5f;
        if (lat_s) try { bias_lat = std::stof(lat_s); } catch (...) {}
        if (lon_s) try { bias_lon = std::stof(lon_s); } catch (...) {}
        int limit = 8;
        if (lim_s) try { limit = std::min(20, std::stoi(lim_s)); } catch (...) {}

        auto results = si.search(q, bias_lat, bias_lon, limit);
        json arr = json::array();
        for (const auto& r : results) {
            arr.push_back({
                {"id",      r.place->id},
                {"name",    r.place->name},
                {"type",    r.place->type},
                {"address", r.place->address},
                {"lat",     r.place->lat},
                {"lon",     r.place->lon},
                {"score",   r.score},
            });
        }
        json body = {{"results", arr}, {"source", "local_index"}};
        crow::response res(body.dump());
        set_json_headers(res);
        return res;
    });

    // ── GET /api/reverse ─────────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/reverse")
    ([&grid, &g](const crow::request& req) {
        auto lat_s = req.url_params.get("lat");
        auto lon_s = req.url_params.get("lon");
        if (!lat_s || !lon_s) {
            crow::response res(400, make_error(400, "lat and lon required").dump());
            set_json_headers(res);
            return res;
        }
        float lat = 0, lon = 0;
        try { lat = std::stof(lat_s); lon = std::stof(lon_s); }
        catch (...) {
            crow::response res(400, make_error(400, "Invalid lat/lon").dump());
            set_json_headers(res);
            return res;
        }

        auto snap = grid.snap_to_edge(lat, lon, g, 500.0f);
        json body;
        if (snap.valid) {
            const Edge& e = g.edges[snap.edge_idx];
            body = {
                {"lat",      snap.lat},
                {"lon",      snap.lon},
                {"dist_m",   snap.dist_m},
                {"edge_idx", snap.edge_idx},
                {"road_name", g.edge_name(e)},
                {"road_ref",  g.edge_ref(e)},
                {"offset_m",  snap.offset_m},
            };
        } else {
            body = {{"error", "No road found within 500 m"}};
        }
        crow::response res(body.dump());
        set_json_headers(res);
        return res;
    });

    // ── POST /api/route ───────────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/route").methods(crow::HTTPMethod::POST)
    ([&g, &grid, &ws](const crow::request& req) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) {
            crow::response res(400, make_error(400, "Invalid JSON").dump());
            set_json_headers(res);
            return res;
        }

        // Parse from/to as node IDs or lat/lon
        auto resolve_node = [&](const json& field) -> int64_t {
            if (field.is_number_integer()) return field.get<int64_t>();
            if (field.is_object() && field.contains("lat") && field.contains("lon")) {
                float lat = field["lat"].get<float>();
                float lon = field["lon"].get<float>();
                auto snap = grid.snap_to_edge(lat, lon, g, 500.0f);
                if (!snap.valid) return -1;
                return static_cast<int64_t>(g.edges[snap.edge_idx].from);
            }
            return -1;
        };

        if (!body.contains("from") || !body.contains("to")) {
            crow::response res(400, make_error(400, "from and to required").dump());
            set_json_headers(res);
            return res;
        }

        int64_t from_id = resolve_node(body["from"]);
        int64_t to_id   = resolve_node(body["to"]);
        if (from_id < 0 || to_id < 0 ||
            from_id >= static_cast<int64_t>(g.node_count()) ||
            to_id   >= static_cast<int64_t>(g.node_count())) {
            crow::response res(404, make_error(404, "Origin or destination not found in graph").dump());
            set_json_headers(res);
            return res;
        }

        bool want_explored = body.value("want_explored", false);
        bool use_live      = body.value("mode", "traffic") == "traffic";
        int  n_alts        = std::min(body.value("alternatives", 0), 3);
        int  horizon_min   = std::clamp(body.value("horizon_min", 0), 0, 60);

        // For prediction: build a temporary WeightStore seeded with predicted weights
        WeightSnapshotPtr route_snap;
        if (horizon_min > 0 && use_live) {
            route_snap = predict(g, ws, horizon_min);
        } else {
            route_snap = ws.current();
        }
        // Use a temporary WeightStore seeded with the chosen snapshot
        WeightStore route_ws(g.edge_count());
        route_ws.publish(route_snap);
        DijkstraRouter router(g, route_ws);

        auto best = router.route(static_cast<uint32_t>(from_id),
                                  static_cast<uint32_t>(to_id),
                                  use_live, want_explored);

        if (!best.found) {
            crow::response res(404, make_error(404, "No route found").dump());
            set_json_headers(res);
            return res;
        }

        json routes = json::array();
        routes.push_back(route_to_json(best, g, "Fastest"));

        // Explored nodes for Dijkstra visualizer
        json explored = json::array();
        if (want_explored) {
            for (uint32_t nid : best.explored) {
                if (nid < g.node_count()) {
                    explored.push_back({g.nodes[nid].lon, g.nodes[nid].lat});
                }
            }
        }

        json resp = {
            {"routes",      routes},
            {"explored",    explored},
            {"from",        {{"node", from_id}}},
            {"to",          {{"node", to_id}}},
            {"horizon_min", horizon_min},
            {"data_source", horizon_min > 0 ? "SIMULATED PREDICTED" : "SIMULATED LIVE"},
        };
        crow::response res(resp.dump());
        set_json_headers(res);
        return res;
    });

    // ── GET /api/traffic ─────────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/traffic")
    ([&g, &ws](const crow::request& req) {
        auto bbox_s = req.url_params.get("bbox");
        auto zoom_s = req.url_params.get("zoom");
        int zoom = zoom_s ? std::stoi(zoom_s) : 14;

        float w2 = g.bbox.west, s2 = g.bbox.south,
              e2 = g.bbox.east, n2 = g.bbox.north;
        if (bbox_s) {
            std::istringstream ss(bbox_s);
            char comma;
            ss >> w2 >> comma >> s2 >> comma >> e2 >> comma >> n2;
        }

        // Filter by road class based on zoom (keep payloads small)
        uint8_t min_class = 0;
        if (zoom < 12) min_class = 3;        // only primary+
        else if (zoom < 14) min_class = 5;   // secondary+
        else min_class = 7;                  // all

        auto snap = ws.current();
        json edges = json::array();
        int count = 0;

        for (uint32_t ei = 0; ei < g.edge_count() && count < 2000; ++ei) {
            const Edge& e  = g.edges[ei];
            const Node& na = g.nodes[e.from];
            const Node& nb = g.nodes[e.to];

            if (e.road_class > min_class) continue;
            // Viewport check (midpoint)
            float mid_lat = (na.lat + nb.lat) * 0.5f;
            float mid_lon = (na.lon + nb.lon) * 0.5f;
            if (mid_lat < s2 || mid_lat > n2 || mid_lon < w2 || mid_lon > e2) continue;

            float live_w = (ei < snap->weights.size()) ? snap->weights[ei] : e.free_flow_s();
            float ff_w   = e.free_flow_s();
            float ratio  = ff_w > 0 ? live_w / ff_w : 1.0f;

            std::string level = "free";
            if (ratio > 1.5f) level = "heavy";
            else if (ratio > 1.2f) level = "moderate";
            else if (ratio > 1.05f) level = "slow";

            float speed_ms = e.length_m / std::max(live_w - e.delay_s, 0.5f);

            edges.push_back({
                {"id",         ei},
                {"from",       {na.lon, na.lat}},
                {"to",         {nb.lon, nb.lat}},
                {"level",      level},
                {"speed_kmh",  speed_ms * 3.6f},
                {"weight_s",   live_w},
                {"ff_s",       ff_w},
                {"road_class", e.road_class},
                {"name",       g.edge_name(e)},
            });
            ++count;
        }

        json body = {{"edges", edges}, {"tick", snap->tick_id},
                     {"data_source", "SIMULATED LIVE"}};
        crow::response res(body.dump());
        set_json_headers(res);
        return res;
    });

    // ── POST /api/sim/control ─────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/sim/control").methods(crow::HTTPMethod::POST)
    ([&sim](const crow::request& req) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) {
            crow::response res(400, make_error(400, "Invalid JSON").dump());
            set_json_headers(res);
            return res;
        }

        std::string action = body.value("action", "");
        if (action == "play")   { sim.start(); }
        else if (action == "pause")  { sim.pause(); }
        else if (action == "reset")  { sim.reset(); }
        else if (action == "speed")  {
            float mult = body.value("multiplier", 1.0f);
            sim.set_speed(std::clamp(mult, 0.1f, 10.0f));
        }
        else if (action == "set_vehicles") {
            int n = std::clamp(body.value("n", 500), 0, 2000);
            sim.set_vehicles(n);
        }
        else if (action == "set_scenario") {
            std::string sc = body.value("scenario", "moderate");
            SimScenario s = SimScenario::MODERATE;
            if (sc == "light")       s = SimScenario::LIGHT;
            else if (sc == "heavy")  s = SimScenario::HEAVY;
            else if (sc == "rush_hour") s = SimScenario::RUSH_HOUR;
            else if (sc == "follow_clock") s = SimScenario::FOLLOW_CLOCK;
            sim.set_scenario(s);
        } else {
            crow::response res(400, make_error(400, "Unknown action: " + action).dump());
            set_json_headers(res);
            return res;
        }

        auto st = sim.status();
        json resp = {
            {"ok", true},
            {"sim", {
                {"running",       st.running},
                {"vehicle_count", st.vehicle_count},
                {"speed_mult",    st.speed_mult},
                {"tick_id",       st.tick_id},
            }},
        };
        crow::response res(resp.dump());
        set_json_headers(res);
        return res;
    });

    // ── POST /api/sim/incident ────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/sim/incident").methods(crow::HTTPMethod::POST)
    ([&sim, &g, &grid](const crow::request& req) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) {
            crow::response res(400, make_error(400, "Invalid JSON").dump());
            set_json_headers(res);
            return res;
        }

        uint32_t edge_idx = UINT32_MAX;
        if (body.contains("edge_id")) {
            edge_idx = body["edge_id"].get<uint32_t>();
        } else if (body.contains("lat") && body.contains("lon")) {
            float lat = body["lat"].get<float>();
            float lon = body["lon"].get<float>();
            auto snap = grid.snap_to_edge(lat, lon, g, 300.0f);
            if (snap.valid) edge_idx = snap.edge_idx;
        }

        if (edge_idx >= g.edge_count()) {
            crow::response res(400, make_error(400, "No edge found for incident location").dump());
            set_json_headers(res);
            return res;
        }

        std::string type = body.value("type", "slow");
        float factor   = std::clamp(body.value("factor", 0.3f), 0.01f, 1.0f);
        float duration = std::clamp(body.value("duration_min", 10.0f), 1.0f, 120.0f);
        sim.create_incident(edge_idx, duration * 60.0f, factor);

        json resp = {{"ok", true}, {"edge_idx", edge_idx},
                     {"type", type}, {"duration_min", duration}};
        crow::response res(resp.dump());
        set_json_headers(res);
        return res;
    });

    // ── GET /api/graph/stats ──────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/graph/stats")
    ([&g]() {
        float total_km = 0;
        for (const auto& e : g.edges) total_km += e.length_m;
        total_km /= 1000.0f;
        float avg_deg = g.edge_count() / (float)std::max(g.node_count(), 1u);

        json body = {
            {"nodes",     g.node_count()},
            {"edges",     g.edge_count()},
            {"avg_out_degree", avg_deg},
            {"total_km",  total_km},
            {"bbox",      {g.bbox.west, g.bbox.south, g.bbox.east, g.bbox.north}},
        };
        crow::response res(body.dump());
        set_json_headers(res);
        return res;
    });

    // ── WS /ws ────────────────────────────────────────────────────────────────
    CROW_WEBSOCKET_ROUTE(app, "/ws")
    .onopen([&hub](crow::websocket::connection& conn) {
        hub.add(&conn);
        SC_LOG_INFO("ws", ("Client connected. Total: " +
            std::to_string(hub.size())).c_str());
        // Send hello
        json hello = {
            {"type",       "hello"},
            {"server",     "SmartCity"},
            {"data_source","SIMULATED LIVE"},
        };
        conn.send_text(hello.dump());
    })
    .onclose([&hub](crow::websocket::connection& conn, const std::string& reason, uint16_t) {
        hub.remove(&conn);
        SC_LOG_INFO("ws", ("Client disconnected: " + reason).c_str());
    })
    .onmessage([&g, &grid, &ws, &hub](
            crow::websocket::connection& conn,
            const std::string& data,
            bool /*is_binary*/) {
        json msg;
        try { msg = json::parse(data); }
        catch (...) {
            conn.send_text(make_error(400, "Invalid JSON").dump());
            return;
        }

        std::string type = msg.value("type", "");

        if (type == "ping") {
            conn.send_text(json{{"type","pong"},{"ts", msg.value("ts",0)}}.dump());
            return;
        }

        if (type == "subscribe") {
            // Store subscription in connection's user context
            // (Crow websocket doesn't expose per-conn state directly;
            //  respond with ack and trust the frontend to manage)
            conn.send_text(json{{"type","subscribed"}}.dump());
            return;
        }

        if (type == "share_location") {
            // Validate consent, range, rate; Phase 4 full impl
            bool consent = msg.value("consent", false);
            if (!consent) {
                conn.send_text(json{{"type","error"},
                    {"message","consent required for share_location"}}.dump());
                return;
            }
            float lat = msg.value("lat", 0.0f);
            float lon = msg.value("lon", 0.0f);
            float spd = msg.value("speed", 0.0f);
            // Basic range validation
            if (lat < 28.0f || lat > 29.0f || lon < 77.0f || lon > 78.0f) {
                conn.send_text(json{{"type","error"},
                    {"message","location out of service area"}}.dump());
                return;
            }
            // Phase 4: blend into edge speed estimates
            conn.send_text(json{{"type","location_ack"},{"spd_kmh",spd*3.6f}}.dump());
            return;
        }

        if (type == "watch_route") {
            conn.send_text(json{
                {"type","watch_ack"},
                {"from", msg.contains("from") ? msg["from"] : json{}},
                {"to",   msg.contains("to")   ? msg["to"]   : json{}},
            }.dump());
            return;
        }

        if (type == "unwatch_route") {
            conn.send_text(json{{"type","unwatch_ack"}}.dump());
            return;
        }

        conn.send_text(json{{"type","error"},
            {"message","unknown message type: " + type}}.dump());
    })
    .onerror([](crow::websocket::connection& conn, const std::string& err) {
        SC_LOG_WARN("ws", err.c_str());
    });

    // ── Catch-all: serve frontend static files ────────────────────────────────
    CROW_ROUTE(app, "/")
    ([]() {
        crow::mustache::set_base("./frontend");
        auto page = crow::mustache::load_text("index.html");
        crow::response res(200, page);
        res.set_header("Content-Type", "text/html; charset=utf-8");
        set_security_headers(res);
        return res;
    });

    CROW_ROUTE(app, "/<path>")
    ([](const crow::request&, std::string path) {
        crow::mustache::set_base("./frontend");
        auto mime = [&](const std::string& p) -> std::string {
            if (p.ends_with(".js"))           return "application/javascript";
            if (p.ends_with(".css"))          return "text/css";
            if (p.ends_with(".json") ||
                p.ends_with(".webmanifest"))  return "application/json";
            if (p.ends_with(".png"))          return "image/png";
            if (p.ends_with(".svg"))          return "image/svg+xml";
            if (p.ends_with(".webp"))         return "image/webp";
            if (p.ends_with(".woff2"))        return "font/woff2";
            if (p.ends_with(".html"))         return "text/html; charset=utf-8";
            return "application/octet-stream";
        };
        try {
            auto text = crow::mustache::load_text(path);
            crow::response res(200, text);
            res.set_header("Content-Type", mime(path));
            return res;
        } catch (...) {
            return crow::response(404);
        }
    });
}

} // namespace smartcity
