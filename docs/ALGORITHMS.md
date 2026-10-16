# SmartCity — Algorithms

> Phase 0 skeleton. Full content added in Phase 2.

## Graph Model

The road network is modelled as a weighted directed graph **G = (V, E)** where:
- **V** = road intersections (at-grade junctions and dead ends)
- **E** = directed road segments (edges carry geometry polylines)
- **w(e, t)** = estimated travel time of edge `e` at time `t` (seconds)

The graph is stored as a Compressed Sparse Row (CSR) adjacency for O(1) neighbour lookup.

## Dijkstra's Algorithm

_Formal description, O((V+E) log V) complexity proof, and correctness sketch added in Phase 2._

## Weight Model

### Greenshields Speed-Density (default)

```
ρ_e  = n_e / C_e              # occupancy ratio (vehicles / capacity)
v(ρ) = v_f × (1 − ρ)         # Greenshields linear model
t(e) = length_m / max(v(ρ), v_min) + δ_i
```

Where:
- `v_f` = free-flow speed (from OSM maxspeed or road-class default)
- `ρ_jam` = 1.0 (normalised)
- `δ_i` = intersection delay (≈ 8 s for signalised, 2 s for unsignalised)
- `v_min` = 1 m/s (non-zero floor, enforces non-negative weights)

### BPR (Bureau of Public Roads) — alternative

```
t(e) = t_0 × (1 + 0.15 × (q/C)^4)
```

Select with env var `WEIGHT_MODEL=bpr`.

## Alternative Routes — Penalty Method

_Full description added in Phase 5._

## Traffic Prediction

_Full description added in Phase 5._

## India-Appropriate Default Speeds

| Road class       | Free-flow speed (km/h) | Default lanes |
|------------------|------------------------|---------------|
| motorway         | 100                    | 3             |
| trunk            | 80                     | 2             |
| primary          | 60                     | 2             |
| secondary        | 50                     | 2             |
| tertiary         | 40                     | 1             |
| unclassified     | 30                     | 1             |
| residential      | 20                     | 1             |
| living_street    | 10                     | 1             |
