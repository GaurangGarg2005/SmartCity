import os
import subprocess

GXX = r"C:\msys64\mingw64\bin\g++.exe"
INCLUDES = [
    "-DASIO_STANDALONE",
    "-ID:/SmartCity/backend/src",
    "-ID:/SmartCity/build/_deps/crow-src/include",
    "-ID:/SmartCity/build/_deps/nlohmann_json-src/include",
    "-isystem", "D:/SmartCity/build/_deps/asio-src/asio/include",
    "-std=c++20",
    "-O2",
]

env = os.environ.copy()
env["PATH"] = r"C:\msys64\mingw64\bin;" + env.get("PATH", "")

files_to_compile = [
    ("backend/src/main.cpp", "build/src/CMakeFiles/smartcity_server.dir/main.cpp.obj"),
    ("backend/src/routing/dijkstra.cpp", "build/src/CMakeFiles/smartcity_server.dir/routing/dijkstra.cpp.obj"),
    ("backend/src/sim/simulator.cpp", "build/src/CMakeFiles/smartcity_server.dir/sim/simulator.cpp.obj"),
    ("backend/src/sim/predictor.cpp", "build/src/CMakeFiles/smartcity_server.dir/sim/predictor.cpp.obj"),
    ("backend/src/graph/spatial_grid.cpp", "build/src/CMakeFiles/smartcity_server.dir/graph/spatial_grid.cpp.obj"),
    ("backend/src/routing/alternatives.cpp", "build/src/CMakeFiles/smartcity_server.dir/routing/alternatives.cpp.obj"),
    ("backend/src/api/server.cpp", "build/src/CMakeFiles/smartcity_server.dir/api/server.cpp.obj"),
    ("backend/src/util/config.cpp", "build/src/CMakeFiles/smartcity_server.dir/util/config.cpp.obj"),
    ("backend/src/util/logger.cpp", "build/src/CMakeFiles/smartcity_server.dir/util/logger.cpp.obj"),
]

for src, obj in files_to_compile:
    print(f"Compiling {src} -> {obj}...")
    cmd = [GXX] + INCLUDES + ["-c", src, "-o", obj]
    res = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Error compiling {src}:\n{res.stderr}")
        exit(1)

# Collect all object files
obj_files = []
for root, dirs, files in os.walk("build/src/CMakeFiles/smartcity_server.dir"):
    for f in files:
        if f.endswith(".obj"):
            obj_files.append(os.path.join(root, f))

print(f"Linking {len(obj_files)} object files into build/src/smartcity_server.exe...")
link_cmd = [GXX] + obj_files + ["-o", "build/src/smartcity_server.exe", "-lws2_32", "-lmswsock"]
res = subprocess.run(link_cmd, env=env, capture_output=True, text=True)
if res.returncode != 0:
    print(f"Error linking:\n{res.stderr}")
    exit(1)

print("SUCCESS: smartcity_server.exe built successfully!")
