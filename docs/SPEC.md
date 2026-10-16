# SmartCity — Master Build Brief

## 0. WORKING AGREEMENT
1. Use Planning mode. Before writing code, save this entire brief verbatim to docs/SPEC.md, then produce an Implementation Plan and a Task List. Wait for my approval of the plan.
2. Build in the phases of Section 12. Re-read docs/SPEC.md at the start of every phase. Do not start a phase until the previous phase's acceptance checks pass. Log results in docs/PROGRESS.md.
3. Verify your own work: run unit tests and benchmarks in the terminal; use the browser subagent to exercise the UI at desktop (1440x900) and mobile (390x844) sizes, saving screenshots and a recording per phase as artifacts.
4. Ask me ONLY at human gates: creating accounts or logging in, pushing to GitHub, deploy approvals, API keys, anything that costs money. Never invent, hard-code or commit secrets. Read them from environment variables and ship a .env.example.
5. Pin all dependency versions. Before relying on any third-party free tier, API or usage policy, re-check its current official documentation (limits and terms change) and record what you found in docs/DEPLOYMENT.md.
6. HONESTY RULE: never present simulated data as real traffic in the UI, docs or logs. Every traffic-related UI surface shows a data-source label (see Section 2).
7. Do not copy Google's logos, icons, fonts or assets. Replicate familiar interaction patterns only, with original visuals under the name "SmartCity".

## 1. PROJECT CONTEXT
Title: "SmartCity: A Graph-Theoretic Framework for Intelligent Real-Time Traffic Prediction and Adaptive Route Optimization". Course: Fundamentals of Graph Theory Lab (26B16CS320), JIIT Noida, B.Tech CSE/IT, 5th semester.
Core idea from the approved synopsis:
- The road network is a weighted graph G=(V,E). Intersections are vertices, roads are edges, and w(u,v) is the estimated travel time.
- Route = argmin over paths P of the sum of w(e). Dijkstra's algorithm, implemented by us in C++, is the routing engine (non-negative weights only).
- Edge weights update as traffic changes (simulated vehicles plus optional, consenting live users), and the route is recalculated.
- Traffic estimation uses vehicle position, speed and direction to estimate short-term movement and congestion. It is explicitly an approximation and a demonstration of prediction, not a claim of accurate citywide forecasting.
- Display the suggested route and alternatives on an interactive MapLibre GL JS map with OpenStreetMap data. The final route choice stays with the user.
- Backend: C++ with the Crow framework. HTTP APIs plus WebSockets. SQLite optional.
- Evaluate with test scenarios: different source/destination pairs, traffic levels and changing weights. Compare selected routes and estimated travel times.
- SDGs: 11 (primary), 9, 13.
Because this is a graph theory lab project, the graph theory must be VISIBLE and explainable in both the UI and the documentation.

## 2. PRODUCT VISION AND WHAT "REAL-TIME" MEANS
Goal: a polished app that looks and behaves like Google Maps on phones and desktops, powered by our own C++ routing and traffic engine, deployed free, usable from any device via a public HTTPS URL.
Real-time has three layers. Each is labelled in the UI by a "Data source" badge:
1. SIMULATED LIVE (always on): a server-side C++ simulation ticks about once per second and pushes updates over WebSocket to all connected devices.
2. CROWD-SOURCED LIVE (opt-in): users who explicitly consent share anonymous location, speed and heading. This feeds edge-speed estimates.
3. EXTERNAL REAL DATA (optional, feature-flagged, OFF when no key is configured):
   a. TomTom Traffic Flow sampling (non-tile endpoint) for a small set of pre-chosen arterial points, used to calibrate edge speeds. Strictly budget requests to stay well under the free daily quota. Check TomTom's current terms about using the data with a non-TomTom basemap and about deriving or storing data before implementing, and document the result.
   b. Open-Meteo current weather (no key) as a rain slowdown factor, polled every ~15 minutes, shown as "weather-adjusted".
The app must work fully with layers 1 and 2 only.

## 3. FIXED TECH STACK
- Backend: C++17 or newer, CMake, Crow (with WebSocket support), nlohmann/json, optional SQLite, Catch2 or GoogleTest. Multi-threaded, thread-safe.
- Core routing: implement graph structures and Dijkstra YOURSELF. Do not use OSRM, GraphHopper, Valhalla or any routing library.
- Frontend: plain HTML/CSS/JavaScript with ES modules, no UI framework. Vendor the pinned MapLibre GL JS build locally (do not depend on a CDN at runtime).
- Basemap: OpenFreeMap vector styles (for example https://tiles.openfreemap.org/styles/liberty for light). For dark mode use a dark OpenFreeMap style if available, otherwise adjust the style's paint properties at runtime. Show required attributions (OpenStreetMap contributors, OpenFreeMap, MapLibre).
- Do NOT use Google Maps APIs, Mapbox, or any paid or keyed service in the default path. Do NOT use OpenStreetMap's own tile servers or the public Nominatim instance for autocomplete (their policies forbid it).
- Build-time tooling only: Python 3 scripts in tools/ for data preparation. Nothing in Python runs in production.
- Packaging: multi-stage Dockerfile.

## 4. REPOSITORY LAYOUT
smartcity/
  docs/ (SPEC.md, PROGRESS.md, ARCHITECTURE.md, ALGORITHMS.md, API.md, DEPLOYMENT.md, PRIVACY.md, TESTING.md, TRACEABILITY.md, report_assets/)
  backend/ (CMakeLists.txt, src/{graph, routing, sim, traffic, geo, search, api, util}, tests/, bench/)
  frontend/ (index.html, evaluation.html, privacy.html, css/, js/ modules, vendor/, sw.js, manifest.webmanifest, icons/)
  tools/ (build_graph.py, build_places.py, evaluate_cli, screenshots helper)
  data/ (graph file, places.json; keep the committed total under about 25 MB)
  Dockerfile, render.yaml, docker-compose.yml, .env.example, .github/workflows/ci.yml, README.md

## 5. DATA PIPELINE (build time)
1. Region is configurable via a bbox setting. Default: Noida plus Greater Noida, roughly west 77.30, south 28.38, east 77.60, north 28.65. Tune the bbox so the simplified graph has at most about 150k nodes. Free hosting has very small CPU and RAM, so size is a hard constraint.
2. Fetch drivable roads from OSM via Overpass (be polite: one query, cache the result, respect fair use). Include motorway, trunk, primary, secondary, tertiary, unclassified, residential, living_street and their _link types. Exclude footway, cycleway, path, private access and similar.
3. Handle oneway (yes, no, -1, roundabouts imply oneway), maxspeed parsing (kph and mph), default speeds by road class (India-appropriate), default lane counts by class, names and refs.
4. Build the graph: nodes at intersections and dead ends. Contract degree-2 nodes while keeping the geometry polyline on the edge. Keep only the largest strongly connected component. Optionally add a small intersection delay for traffic signals.
5. Output a compact graph file: nodes (id, lat, lon), edges (from, to, length_m, free_flow_speed, lanes, class, oneway, name, ref, geometry). Write a stats report (nodes, edges, average degree, total km, SCC coverage) into docs/.
6. Build a local places index (data/places.json) from named roads, sectors/localities, and POIs (education, hospitals, malls, metro/rail stations, parks, offices). Include JIIT as a test landmark.
7. Runtime geo helpers: spatial grid index; snap a lat/lon to the nearest EDGE with a projected offset (not just the nearest node), respecting one-way direction.

## 6. BACKEND ENGINE (C++)

### 6.1 Graph and Dijkstra
- CSR adjacency. Edge weights are seconds, stored in an immutable snapshot that readers use; the simulation thread publishes a new snapshot each tick (double-buffering or atomic shared_ptr swap) so route queries never block.
- Dijkstra with an indexed or binary heap, early exit at target, non-negative weight enforcement (clamp to a small epsilon, assert in debug builds). Return path, total weight, and stats: nodes settled, edges relaxed, microseconds.
- Optionally return the settled-node order (capped around 5000) for the UI visualizer.
- Weight model: t(e,time) = length / v(e,time) + intersection_delay, where v comes from a Greenshields-style speed-density relation (BPR-style as an alternative, selectable by config). Document the formulas in ALGORITHMS.md.
- Alternatives: the penalty method (repeatedly inflate the weights of edges on already-found routes, rerun Dijkstra) with a dissimilarity filter (reject if more than about 70% shared length) and a stretch limit (at most about 1.5x the best time). Return up to 3 alternatives. For evaluation only, also implement Yen's K-shortest paths to compare quality.
- Also expose: free-flow route (static weights), shortest-distance route, and traffic-aware route, so the UI can show "x min slower than usual".

### 6.2 Traffic simulation and prediction
- Vehicles: default 500, configurable up to 2000. Origins and destinations are sampled from hotspots in the places index (weighted by category) with random fallback. A configurable share of drivers (default 30%) re-route using current weights; the rest follow their initial route. Deterministic with a seed option.
- Movement: each tick advances vehicles along edges at speeds from the density model, with capacity limits and queueing at edge ends. Support a sim-time multiplier (1x to 10x).
- Demand profile by time of day (IST): night low, morning and evening peaks. Modes: "Follow clock" and fixed scenarios (Light, Moderate, Heavy, Rush hour).
- Incidents: block or slow an edge (or small cluster) for a duration with auto-expiry. Vehicles and route queries respond.
- Weight updates: exponential smoothing (alpha about 0.3) to avoid oscillation. Publish only deltas to clients.
- PREDICTION (synopsis objective 3): for horizons of 5, 10, 15 and 30 minutes, project each vehicle forward along its route using current position, speed and direction, count projected occupancy per edge, and convert to predicted edge travel times. Expose predicted weight snapshots per horizon. Route queries accept horizon_min and run Dijkstra on those weights.
- Log predictions and compare against what actually happens later; report mean absolute error per horizon on the evaluation page.
- Performance guard: if a tick overruns its budget, automatically reduce vehicle count or tick rate and surface a "reduced simulation fidelity" indicator.

### 6.3 Live user locations (opt-in)
- WebSocket message share_location {lat, lon, speed, heading, ts}, accepted only after the client has sent an explicit consent flag.
- Anonymous random session ID, no accounts. Match each sample to an edge (use heading to disambiguate), blend speed into that edge's estimate with a decay window, require a minimum number of recent samples before it affects weights.
- Keep raw samples in memory only, discard after about 2 minutes. Never persist or log raw coordinates. Never show other users' positions to anyone; show only an aggregate count ("Live participants: n").
- Rate limit, validate ranges, drop out-of-region samples.

### 6.4 HTTP and WebSocket API (document in docs/API.md)
- GET /healthz
- GET /api/meta (bbox, counts, data-source flags, sim status)
- GET /api/search?q=&lat=&lon=&limit= (local index first, typo-tolerant prefix match, biased to map centre; optional Photon fallback only if enabled, debounced, cached, with attribution)
- GET /api/reverse?lat=&lon=
- POST /api/route {from, to, alternatives (0-3), horizon_min, mode ("traffic" or "free_flow"), want_explored (bool)} returning routes[] each with id, label, geometry (polyline6 or GeoJSON), distance_m, duration_s, free_flow_duration_s, delay_s, steps[] (maneuver, road name, distance, duration, location), congestion segments for coloring, and stats {nodes_settled, edges_relaxed, dijkstra_ms}.
- GET /api/traffic?bbox=&zoom= (edges in viewport with level, speed, weight; filter by road class by zoom; keep payloads small, no full-graph downloads)
- POST /api/sim/control {action: play | pause | speed | reset | set_vehicles | set_scenario}
- POST /api/sim/incident {edge_id or lat/lon, type: block | slow, factor, duration_min}
- POST /api/eval/run {seed, pairs, scenarios} (used by the evaluation page)
- WS /ws: client to server: subscribe {bbox, zoom}, share_location, watch_route {request}, unwatch_route, ping. Server to client: traffic_delta {tick, edges[]}, vehicles {tick, positions[]}, sim_state, incident, reroute_suggestion {route_id, new_route, saving_s}, participants {n}.
- ROUTE WATCH: while a user is navigating, re-run their route every ~10 seconds on current weights and send reroute_suggestion only when the saving exceeds max(2 minutes, 10%). Hysteresis prevents flip-flopping. Never switch routes automatically.

### 6.5 Non-functional
- Thread safety (shared_mutex or snapshot swapping), input validation, request size limits, per-IP rate limiting, WebSocket heartbeat (ping/pong about every 25 seconds) and reconnect-friendly design, graceful shutdown, structured logging with no PII, security headers including a CSP.
- Resource budget: assume the free host has very little CPU and about 512 MB RAM (verify). Target under 400 MB resident. Keep the graph file memory-mapped or compact.
- Performance targets (measure and report, do not guess): p95 route computation across the region, tick duration, payload sizes.

## 7. FRONTEND — GOOGLE-MAPS-LIKE EXPERIENCE
The map fills the screen. Desktop: a floating left panel (about 400 px). Mobile: a draggable bottom sheet with peek, half and full snap points, safe-area insets, 48 px touch targets, one-handed reach. Required features:

1. Search: floating search bar, debounced autocomplete (places and roads), recent searches (localStorage), "Your location", "Choose on map", clear button, keyboard navigation.
2. Place card: tap any result or map point for a card with name, address/road, coordinates, and Directions / Share / Save buttons.
3. Directions: A/B fields with swap, drag the pins to adjust, right-click or long-press context menu ("Directions from here", "Directions to here", "What's here?", "Add incident here" for demo use). Optional stretch: intermediate stops.
4. Routes on map: selected route in blue with a white casing and congestion-colored segments, alternatives in grey, tap to select, floating ETA bubbles at route midpoints, "Fastest" badge, distance, duration, arrival time, and "+N min vs usual" delay text.
5. Leave-time prediction: a "Leave now / +10 / +20 / +30 min" selector that recomputes with predicted weights, plus a small ETA comparison chart.
6. Turn-by-turn list: generated from bearing changes (turn left/right, slight, sharp, U-turn, roundabout exit n), road names and refs, distances. Tapping a step flies the camera to it.
7. Navigation mode: Start button, geolocation watchPosition, snap to route, heading-up camera with pitch, next-maneuver banner, remaining distance/time/ETA bar, off-route detection (over about 50 m for several seconds) with automatic reroute, Recenter button, and a Stop button.
8. "Faster route available" prompt from the server: toast/banner showing minutes saved with Switch and Keep buttons (the user decides).
9. SIMULATED DRIVE: "Simulate driving this route" at 10x to 60x so everything can be demoed from a laptop without GPS.
10. Traffic layer: toggle, colors green / orange / red / dark red plus dash patterns for color-blind users, legend, road-class visibility by zoom, tap a road for a tooltip (name, current speed, free-flow speed, delay, weight w(e) in seconds, level). Update via WebSocket deltas using feature-state or equivalent. Never load the whole graph as one GeoJSON.
11. Vehicles layer: toggle, rendered as a map layer (not DOM markers), interpolated between ticks, capped per viewport.
12. GRAPH THEORY MODE: toggle that shows vertices and edges at high zoom, edge weight labels, node degree and adjacency on tap, the chosen path P* with the sum of w(e) written out, and a DIJKSTRA VISUALIZER that replays the settled-node frontier as an animation with speed control and shows "explored N nodes in X ms".
13. Demo/Simulation panel: play/pause, speed, vehicle count, scenario select, incident tool (tap a road, choose block or slow, duration), reset, plus three one-click demo scenarios ("Rush hour on a main arterial", "Accident blocks the fastest route", "Rain slowdown") that set up an origin/destination pair and trigger a visible reroute.
14. Status and honesty UI: data-source badge (Simulated / Live users: n / Real data: on or off / Weather-adjusted), connection state with latency and tick counter, "reduced simulation fidelity" indicator when active.
15. Layers and settings: light/dark/system theme, units (km/mi), toggles for traffic, vehicles, graph mode.
16. Share: URL encodes origin, destination, horizon and selected route; copy link; Web Share API on mobile.
17. Map controls: zoom, compass/reset north, "my location", scale bar.
18. PWA: manifest, icons (including maskable), service worker precaching the app shell and vendored libraries, offline page, install prompt.
19. Resilience states: cold-start screen for the free server ("Waking up the free server, this can take about a minute", progress, auto-retry), WebSocket reconnect with backoff, geolocation denied fallback, no-route-found message, out-of-region message that shows the supported bounds, loading skeletons.
20. Accessibility: ARIA labels, logical focus order, keyboard operable, WCAG AA contrast, respects prefers-reduced-motion, screen-reader-friendly route summary.
21. Consent and privacy: a clear consent dialog before location sharing, a visible "sharing" indicator with a one-tap stop, and a link to the privacy page.
22. Evaluation page (/evaluation.html): see Section 9.
Performance targets: first usable map under 3 s on a mid-range phone over 4G (excluding server cold start), smooth interaction with 1000 vehicles shown, WebSocket updates no more than about 1 Hz, payload per update under about 300 KB.

## 8. SECURITY, PRIVACY, LEGAL
Consent-gated location sharing, anonymous in-memory-only samples, no PII logging, strict input validation, CORS limited to the deployed origin, optional ADMIN_PIN env var protecting sim control and incident endpoints, per-IP rate limiting, WebSocket message size limits, CSP and security headers, no secrets in client code. Respect and document the terms and attribution requirements of OpenStreetMap, Overpass, OpenFreeMap, MapLibre, Open-Meteo, TomTom (if enabled) and Photon (if enabled).

## 9. TESTING AND EVALUATION (synopsis Step 7)
- Unit tests: Dijkstra versus a reference (Bellman-Ford) on at least 1000 random graphs, including disconnected cases and equal-weight ties; non-negative weight guard; alternatives diversity and stretch limits; snapping; polyline and maneuver generation; simulation conservation (vehicles are not created or lost); prediction determinism under a fixed seed.
- Integration tests: every API endpoint and WebSocket flow, including consent gating and rate limiting.
- UI end-to-end via the browser subagent: search, route, alternative selection, traffic toggle, incident, reroute prompt, navigation simulation, graph mode, PWA install, on desktop and mobile viewports.
- Evaluation page and CLI (seeded and reproducible): N random source-destination pairs (default 200) across traffic levels {light, moderate, heavy, rush hour, incident}. Compare the static free-flow route versus the adaptive Dijkstra route, both measured under the current congested weights. Report mean travel time, percentage improvement, p50/p95 compute time, nodes settled, and prediction MAE at each horizon. Show tables and charts, export CSV and JSON, and save results under docs/report_assets/.
- Lighthouse mobile audit (performance, accessibility, PWA) with results saved.

## 10. FREE DEPLOYMENT (works across devices over HTTPS)
Primary plan: ONE Docker image serving API, WebSocket and static frontend on the same origin, deployed as a free web service on Render from a GitHub repo, defined by render.yaml (docker runtime, free plan, health check /healthz, environment variables, listen on $PORT, bind 0.0.0.0).
- The graph and places files are baked into the image. The service keeps no durable state on disk (its filesystem is ephemeral); simulation state lives in memory and restarts cleanly.
- HTTPS is automatic and is required for geolocation, service workers and PWA install. Use wss:// for WebSockets.
- Cold-start handling: free services sleep after a period without inbound traffic and take roughly a minute to wake (verify current behavior). The frontend must detect this, show the waking screen, and retry. Open WebSocket connections with heartbeats keep the service awake while anyone is using it.
- Optional keep-alive: document how an external free pinger could hit /healthz every ~10 minutes, including the trade-off against the monthly free instance hours and any provider policy. Make it opt-in and explain it, do not silently rely on it.
- Fallback hosts if the primary's free tier has changed: Google Cloud Run (free tier, needs a billing account, so ask me first; max-instances 1, min-instances 0, long request timeout, handle WebSocket reconnects) or another genuinely free container host. Verify whether Hugging Face Docker Spaces are free before considering them; it may now require a paid plan.
- Optional: mirror the static frontend to Cloudflare Pages or Netlify (configurable API base URL, CORS and WebSocket origin handled) so the UI loads instantly while the backend wakes.
- CI: GitHub Actions to build, run unit tests and build the Docker image; deploy via the host's GitHub integration.
- Post-deploy verification (record results in docs/DEPLOYMENT.md): curl /healthz, WebSocket connect test, load the site on a phone viewport and a desktop viewport, run two clients at once and confirm both see the same simulation and incident updates, confirm live-location consent flow works, and confirm geolocation works over HTTPS.
- Provide docs/DEPLOYMENT.md with step-by-step instructions I can follow myself, plus a "free-tier budget" table with the limits you verified and the date you checked them.

## 11. DOCUMENTATION DELIVERABLES
README (what it is, screenshots, local run, deploy), ARCHITECTURE.md (Mermaid system diagram, data-flow, sequence diagrams for route request, traffic update, reroute suggestion), ALGORITHMS.md (graph model, Dijkstra with O((V+E) log V) complexity and a correctness sketch, weight model equations, penalty alternatives, prediction method and its limitations), API.md, PRIVACY.md, TESTING.md with real measured results, TRACEABILITY.md mapping each synopsis objective and expected outcome to a feature, file and test, and report_assets/ containing desktop and mobile screenshots (light and dark), a flowchart matching the synopsis Figure 1, the evaluation CSV, and a short 3 to 5 minute demo script.

## 12. PHASES AND ACCEPTANCE CRITERIA
Phase 0 — Plan and scaffold: SPEC.md saved, Implementation Plan approved by me, repo skeleton, CI stub, Docker skeleton.
Phase 1 — Data: graph and places files built, stats report written, largest SCC verified, size within budget.
Phase 2 — C++ core: graph, Dijkstra, snapping, unit tests passing (including random-graph equivalence), benchmark numbers recorded.
Phase 3 — API plus basic map: search, route, reverse working; map renders; a route can be drawn between two searched places.
Phase 4 — Simulation and live traffic: vehicles moving, weights updating, WebSocket deltas, traffic layer and vehicle layer visible and smooth.
Phase 5 — Prediction, alternatives, reroute: horizons working, alternatives with filters, route watching and the "faster route" prompt with hysteresis.
Phase 6 — Full UI: Google-Maps-like desktop and mobile layouts, turn-by-turn, navigation and simulated drive, graph theory mode with Dijkstra visualizer, demo panel and scenarios, theming, PWA, accessibility, resilience states.
Phase 7 — Live users and optional real data: consent flow, edge speed blending, privacy guarantees tested; TomTom and weather adapters behind flags with strict quota budgeting and graceful disabling.
Phase 8 — Evaluation, tests, docs: evaluation page and CLI, full test suite green, Lighthouse run, all docs and report assets generated.
Phase 9 — Deploy and verify: Docker image, free hosting live, post-deploy checks passed on two devices at once, live URL recorded in README.
Phase 10 (stretch, only after everything above): Emscripten/WASM build of the routing core as a clearly labelled degraded offline mode when the backend is unreachable, voice guidance via the Web Speech API, intermediate stops.
After each phase: summarize what works, what was verified (with artifacts), and what is left.

## 13. DEFINITION OF DONE
- All synopsis features work end to end: weighted-graph road network, Dijkstra in C++, simulated vehicles and optional consented live locations, traffic estimation, dynamic weight updates, recalculation, route plus alternatives on an interactive map, test scenarios with measured results.
- The app is reachable at a public HTTPS URL, works on phone and desktop, installs as a PWA, and live updates reach multiple devices at once.
- Honest data-source labelling everywhere. No secrets in the repo. All tests pass.
- Documentation and report assets exist.

## 14. NON-GOALS
No Google Maps or other paid map services in the default path. No claim of real citywide traffic. No machine learning (mention as future scope). No turn restrictions or public transport (document as limitations). No user accounts.

## 15. FINAL OUTPUT
When finished, give me: the live URL, how to run locally with one command, a list of verified free-tier limits with dates, known limitations, and the exact manual steps (if any) I still need to do.
