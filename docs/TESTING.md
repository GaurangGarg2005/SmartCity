# SmartCity — Testing Results

> Phase 0 skeleton. Real measured results filled in each phase.

## Unit Tests (Phase 2+)

| Suite | Cases | Pass | Fail | Notes |
|---|---|---|---|---|
| Dijkstra vs Bellman-Ford | 1000 | — | — | Phase 2 |
| Non-negative weight guard | — | — | — | Phase 2 |
| Snapping accuracy | — | — | — | Phase 2 |
| Polyline roundtrip | — | — | — | Phase 2 |
| Alternatives diversity | — | — | — | Phase 5 |
| Simulation conservation | — | — | — | Phase 4 |
| Prediction determinism | — | — | — | Phase 5 |

## Integration Tests (Phase 8+)

_All endpoints and WebSocket flows — added in Phase 8._

## Performance Benchmarks (Phase 2+)

| Metric | Target | Measured | Notes |
|---|---|---|---|
| p95 route time | — | — | Phase 2 |
| p50 route time | — | — | Phase 2 |
| Tick duration | ≤ 1000 ms | — | Phase 4 |
| WS payload per update | ≤ 300 KB | — | Phase 4 |
| First usable map (4G) | ≤ 3 s | — | Phase 6 |

## Lighthouse Audit (Phase 6+)

| Category | Score | Notes |
|---|---|---|
| Performance | — | Phase 6 |
| Accessibility | — | Phase 6 |
| Best Practices | — | Phase 6 |
| PWA | — | Phase 6 |
