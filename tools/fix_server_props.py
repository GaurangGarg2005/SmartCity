with open('backend/src/api/server.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

target = '''        json resp = {
            {"ok",           true},
            {"edge_idx",     snap.edge_idx},
            {"road_name",    e.name},
            {"speed_limit",  e.speed_limit_kmh},
            {"vehicle_count", sim.status().vehicle_count},
            {"message",      "Your vehicle has been added to the live road simulation!"}
        };'''

replacement = '''        json resp = {
            {"ok",           true},
            {"edge_idx",     snap.edge_idx},
            {"road_name",    g.edge_name(e)},
            {"speed_limit",  e.free_flow_speed_ms * 3.6f},
            {"vehicle_count", sim.status().vehicle_count},
            {"message",      "Your vehicle has been added to the live road simulation!"}
        };'''

def norm(s): return s.replace('\r\n', '\n')
assert norm(target) in norm(text), 'target not found'
new_text = norm(text).replace(norm(target), norm(replacement))
with open('backend/src/api/server.cpp', 'w', encoding='utf-8') as f:
    f.write(new_text)
print('Updated server.cpp successfully!')

