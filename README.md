# SmartCity 🗺️

> A Graph-Theoretic Framework for Intelligent Real-Time Traffic Prediction and Adaptive Route Optimization  
> Course: Fundamentals of Graph Theory Lab (26B16CS320) · JIIT Noida · B.Tech CSE/IT · 5th Semester

[![CI](https://github.com/YOUR_USERNAME/smartcity/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/smartcity/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## Live Demo

> 🌐 **Live URL:** _Pending Phase 9 deployment_

---

## What Is SmartCity?

SmartCity models the Noida + Greater Noida road network as a weighted directed graph **G = (V, E)** where:
- **V** = road intersections (≈ nodes)
- **E** = road segments (≈ directed edges)
- **w(u,v)** = estimated travel time in seconds (updates in real-time)

The optimal route is `argmin Σ w(e)` over all paths P from source to destination, computed by **our own C++ implementation of Dijkstra's algorithm**.

A simulated traffic engine (500 vehicles by default) updates edge weights every second and pushes deltas to all connected browsers via WebSocket. The map renders on **MapLibre GL JS** with **OpenStreetMap** data.

---

## Screenshots

> _(Added during Phase 6)_

---

## Run Locally (one command)

```bash
docker compose up
```

Then open http://localhost:8080 in your browser.

### Prerequisites
- Docker ≥ 24.x with Compose v2
- No other dependencies needed (everything is in the image)

### Build from source (without Docker)

```bash
# 1. Build the graph data (Python 3.11+, run once)
pip install -r tools/requirements.txt
python tools/build_graph.py
python tools/build_places.py

# 2. Build and run the C++ backend
cd backend
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/smartcity_server

# 3. Serve the frontend (in another terminal)
# The server already serves frontend/ as static files on port 8080.
```

---

## Deploy to Render (free)

See [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) for full step-by-step instructions.

Quick summary:
1. Fork this repo to your GitHub account
2. Create a Render account at render.com
3. "New Web Service" → connect your fork → Docker runtime
4. Set env vars from `.env.example` in Render's dashboard
5. Deploy — Render builds and serves the image automatically

---

## Architecture

```
Browser (MapLibre GL JS)  ──HTTP/WS──►  Crow C++ Server
                                              │
                          ┌───────────────────┤
                          │                   │
                    data/graph.bin       sim_thread
                    data/places.json     (vehicles, weights)
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for full Mermaid diagrams.

---

## Graph Theory

The graph theory is **visible in the UI**: enable **Graph Theory Mode** to see vertices, edge weights `w(e)`, and a step-by-step Dijkstra visualizer showing the settled-node frontier expanding from source to target.

See [docs/ALGORITHMS.md](docs/ALGORITHMS.md) for:
- Graph model and weight equations (Greenshields speed-density)
- Dijkstra correctness sketch and O((V+E) log V) complexity
- Prediction method and limitations

---

## Data Sources

| Badge shown in UI | What it means |
|---|---|
| 🟢 Simulated Live | Our C++ vehicle simulation (always on) |
| 🔵 Live users: N | Consented anonymous crowd-sourcing |
| 🌦️ Weather-adjusted | Open-Meteo rain factor (no API key) |
| 📡 Real data: ON | TomTom calibration (requires API key, OFF by default) |

Simulated data is **never presented as real traffic**.

---

## Tech Stack

| Layer | Technology |
|---|---|
| Backend | C++17, CMake, Crow 1.2.0 |
| Routing | Hand-written Dijkstra (CSR graph) |
| Serialization | nlohmann/json 3.11.3 |
| Tests | Catch2 3.6.0 |
| Frontend | Plain HTML/CSS/JS (ES modules) |
| Map | MapLibre GL JS 4.5.2 (vendored) |
| Basemap | OpenFreeMap (OpenStreetMap data) |
| Deployment | Docker, Render free tier |

---

## Attributions

- Map data © [OpenStreetMap contributors](https://www.openstreetmap.org/copyright) (ODbL)
- Basemap style: [OpenFreeMap](https://openfreemap.org/) (MIT)
- Map renderer: [MapLibre GL JS](https://maplibre.org/) (BSD-2-Clause)
- Weather: [Open-Meteo](https://open-meteo.com/) (CC BY 4.0)

---

## SDGs

This project addresses **UN Sustainable Development Goal 11** (Sustainable Cities and Communities) as primary, and SDGs 9 (Industry, Innovation and Infrastructure) and 13 (Climate Action) as secondary.

---

## Known Limitations

- No real citywide traffic data by default (all simulation unless TomTom key provided)
- No turn restrictions
- No public transport routing
- No user accounts
- No machine learning (documented as future scope)
- Graph covers Noida + Greater Noida only

---

## License

MIT — see [LICENSE](LICENSE).
