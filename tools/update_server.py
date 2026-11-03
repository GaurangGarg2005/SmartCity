with open('backend/src/api/server.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

target = '    // ── POST /api/sim/control'
addition = '''    // ── GET /api/vehicles ─────────────────────────────────────────────────────
    CROW_ROUTE(app, "/api/vehicles")
    ([&sim](const crow::request& req) {
        auto horizon_s = req.url_params.get("horizon");
        int horizon_min = horizon_s ? std::clamp(std::stoi(horizon_s), 0, 120) : 0;
        auto positions = sim.vehicle_positions(horizon_min);
        json body = {
            {"horizon_min", horizon_min},
            {"count",       positions.size()},
            {"positions",   positions}
        };
        crow::response res(body.dump());
        set_json_headers(res);
        return res;
    });

    // ── POST /api/sim/control'''

assert target in text, 'target not found'
text = text.replace(target, addition, 1)

with open('backend/src/api/server.cpp', 'w', encoding='utf-8') as f:
    f.write(text)
print('Successfully added /api/vehicles to server.cpp')

