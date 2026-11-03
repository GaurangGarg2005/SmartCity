# Update simulator.h
with open('backend/src/sim/simulator.h', 'r', encoding='utf-8') as f:
    sim_h = f.read()

target_h = '    void create_incident(uint32_t edge_idx, float duration_s, float factor);'
addition_h = '''    void create_incident(uint32_t edge_idx, float duration_s, float factor);
    void add_vehicle_at(uint32_t edge_idx, float progress_m);'''

assert target_h in sim_h, 'target_h not found'
sim_h = sim_h.replace(target_h, addition_h, 1)

with open('backend/src/sim/simulator.h', 'w', encoding='utf-8') as f:
    f.write(sim_h)
print('Updated simulator.h')

# Update simulator.cpp
with open('backend/src/sim/simulator.cpp', 'r', encoding='utf-8') as f:
    sim_cpp = f.read()

target_cpp = 'void Simulator::create_incident(uint32_t edge_idx, float duration_s, float factor) {'
addition_cpp = '''void Simulator::add_vehicle_at(uint32_t edge_idx, float progress_m) {
    if (edge_idx >= g_.edge_count()) return;
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    Vehicle v;
    v.edge_idx = edge_idx;
    v.progress_m = std::clamp(progress_m, 0.0f, g_.edges[edge_idx].length_m);
    v.speed_ms = g_.edges[edge_idx].free_flow_speed_ms * 0.85f;
    vehicles_.push_back(v);
    n_vehicles_.store(static_cast<int>(vehicles_.size()));
}

void Simulator::create_incident(uint32_t edge_idx, float duration_s, float factor) {'''

assert target_cpp in sim_cpp, 'target_cpp not found'
sim_cpp = sim_cpp.replace(target_cpp, addition_cpp, 1)

with open('backend/src/sim/simulator.cpp', 'w', encoding='utf-8') as f:
    f.write(sim_cpp)
print('Updated simulator.cpp')

# Update server.cpp
with open('backend/src/api/server.cpp', 'r', encoding='utf-8') as f:
    srv = f.read()

target_srv = '    // ── POST /api/sim/control'
addition_srv = '''    // ── POST /api/sim/my-vehicle ──────────────────────────────────────────────
    CROW_ROUTE(app, "/api/sim/my-vehicle").methods(crow::HTTPMethod::POST)
    ([&sim, &g, &grid](const crow::request& req) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) {
            crow::response res(400, make_error(400, "Invalid JSON").dump());
            set_json_headers(res);
            return res;
        }

        float lat = body.value("lat", 0.0f);
        float lon = body.value("lon", 0.0f);

        auto snap = grid.snap_to_edge(lat, lon, g, 500.0f);
        if (!snap.valid || snap.edge_idx >= g.edge_count()) {
            crow::response res(400, make_error(400, "Location not near a valid road").dump());
            set_json_headers(res);
            return res;
        }

        sim.add_vehicle_at(snap.edge_idx, snap.dist_m);
        const Edge& e = g.edges[snap.edge_idx];

        json resp = {
            {"ok",           true},
            {"edge_idx",     snap.edge_idx},
            {"road_name",    e.name},
            {"speed_limit",  e.speed_limit_kmh},
            {"vehicle_count", sim.status().vehicle_count},
            {"message",      "Your vehicle has been added to the live road simulation!"}
        };
        crow::response res(resp.dump());
        set_json_headers(res);
        return res;
    });

    // ── POST /api/sim/control'''

assert target_srv in srv, 'target_srv not found'
srv = srv.replace(target_srv, addition_srv, 1)

with open('backend/src/api/server.cpp', 'w', encoding='utf-8') as f:
    f.write(srv)
print('Updated server.cpp with /api/sim/my-vehicle')

