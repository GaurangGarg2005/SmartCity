# SmartCity — Traceability Matrix

> Maps each synopsis objective and expected outcome to a feature, source file, and test.
> Phase 0 skeleton; rows completed each phase.

| Synopsis Objective | Feature | Source files | Test |
|---|---|---|---|
| Weighted graph G=(V,E) for road network | CSR graph, binary loader | `backend/src/graph/graph.*` | `test_dijkstra.cpp` |
| Dijkstra routing (argmin Σw(e)) | DijkstraRouter | `backend/src/routing/dijkstra.*` | `test_dijkstra.cpp` |
| Edge weight updates from traffic | WeightStore snapshot swap | `backend/src/traffic/weight_updater.*` | `test_simulation.cpp` |
| Simulated vehicles | Simulator tick loop | `backend/src/sim/simulator.*` | `test_simulation.cpp` |
| Live crowd-sourced locations (opt-in) | WS share_location + consent | `backend/src/api/server.*` + `frontend/js/ws.js` | Integration test Phase 7 |
| Traffic prediction (5/10/15/30 min) | Predictor | `backend/src/sim/predictor.*` | `test_simulation.cpp` |
| Alternative routes | Penalty method | `backend/src/routing/alternatives.*` | `test_dijkstra.cpp` |
| Interactive map with route display | MapLibre + route.js | `frontend/js/map.js`, `route.js` | Browser subagent Phase 3 |
| Graph theory visible in UI | Graph Theory Mode + Dijkstra Visualizer | `frontend/js/graph-mode.js` | Browser subagent Phase 6 |
| Evaluation with test scenarios | Eval page + CLI | `frontend/evaluation.html`, `tools/evaluate_cli` | Phase 8 |
| SDG 11 / 9 / 13 | App purpose + doc | README, ALGORITHMS.md | — |
