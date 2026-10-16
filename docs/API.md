# SmartCity — API Reference

## Base URL

Local: `http://localhost:8080`  
Production: `https://smartcity.onrender.com` _(filled after Phase 9)_

## Data-Source Label

Every traffic-related response includes `"data_source": "SIMULATED LIVE"` (or `"CROWD-SOURCED"` / `"EXTERNAL REAL"` when those layers are active). **The UI must always display this label.** Never show simulated data as real traffic (SPEC §0.6).

---

## REST Endpoints

### GET /healthz
Returns `200 ok` — used by Render's health check and the frontend cold-start poller.

---

### GET /api/meta
Returns server metadata.

**Response:**
```json
{
  "bbox":   [77.32, 28.42, 77.57, 28.63],
  "graph":  {"nodes": 110999, "edges": 246606},
  "places": 758,
  "sim":    {"running": true, "vehicle_count": 500, "speed_mult": 1.0, "tick_id": 123},
  "ws_clients": 4,
  "features": {
    "live_user_locations": true,
    "tomtom_traffic": false,
    "open_meteo": false
  },
  "data_source": "SIMULATED LIVE"
}
```

---

### GET /api/search
Typo-tolerant prefix search over the local places index.

**Parameters:**
| Name | Type | Required | Description |
|------|------|----------|-------------|
| `q` | string | ✅ | Search query (min 1 char) |
| `lat` | float | — | Bias latitude (map centre) |
| `lon` | float | — | Bias longitude |
| `limit` | int | — | Max results (default 8, max 20) |

**Response:**
```json
{
  "results": [
    {
      "id":      "jiit_noida",
      "name":    "JIIT Noida",
      "type":    "education",
      "address": "Sector 62, Noida, UP 201309",
      "lat":     28.5705,
      "lon":     77.3219,
      "score":   1.15
    }
  ],
  "source": "local_index"
}
```

---

### GET /api/reverse
Project a lat/lon point onto the nearest drivable road edge.

**Parameters:** `lat`, `lon` (required, float)

**Response:**
```json
{
  "lat": 28.5705, "lon": 77.3219,
  "dist_m": 12.4,
  "edge_idx": 58302,
  "road_name": "Sector 62 Road",
  "road_ref": "",
  "offset_m": 34.1
}
```

---

### POST /api/route
Compute a Dijkstra route between two points. Backbone of the application.

**Request body:**
```json
{
  "from": {"lat": 28.5705, "lon": 77.3219},
  "to":   {"lat": 28.5634, "lon": 77.3382},
  "mode": "traffic",
  "alternatives": 0,
  "horizon_min": 0,
  "want_explored": false
}
```

`from` / `to` can be:
- `{"lat": ..., "lon": ...}` — snapped to nearest edge
- `12345` — direct internal node ID

`mode`: `"traffic"` (live weights) | `"free_flow"` (static)  
`alternatives`: 0–3 (Phase 5)  
`horizon_min`: 0/5/10/15/30 (Phase 5 prediction)  
`want_explored`: if true, returns settled node coords for Dijkstra visualizer (capped at 5000)

**Response:**
```json
{
  "routes": [
    {
      "label":       "Fastest",
      "distance_m":  4231.5,
      "duration_s":  612.0,
      "free_flow_s": 508.0,
      "delay_s":     104.0,
      "geometry":    "encoded_polyline6_string",
      "geometry_geojson": [[77.3219, 28.5705], ...],
      "steps": [
        {
          "maneuver":   "depart",
          "road_name":  "Sector 62 Road",
          "distance_m": 240,
          "duration_s": 34,
          "location":   [77.3219, 28.5705]
        }
      ],
      "congestion": [
        {"edge_idx": 12, "level": "heavy", "seg": 0}
      ],
      "stats": {
        "nodes_settled": 1820,
        "edges_relaxed": 4203,
        "dijkstra_ms": 3.4
      }
    }
  ],
  "explored": [[77.32, 28.57], ...],
  "from": {"node": 4521},
  "to":   {"node": 8831}
}
```

---

### GET /api/traffic
Edges within a bounding box with current traffic levels.  
Road class filtered by zoom (keep payloads small; max 2000 edges per response).

**Parameters:** `bbox=west,south,east,north`, `zoom` (integer)

**Response:**
```json
{
  "edges": [
    {
      "id": 5812,
      "from": [77.32, 28.57], "to": [77.321, 28.571],
      "level": "moderate",
      "speed_kmh": 28.4,
      "weight_s": 14.2,
      "ff_s": 11.8,
      "road_class": 3,
      "name": "Sector 18 Road"
    }
  ],
  "tick": 142,
  "data_source": "SIMULATED LIVE"
}
```

Levels: `"free"` | `"slow"` | `"moderate"` | `"heavy"`

---

### GET /api/graph/stats
Graph topology stats for the info panel and Graph Theory Mode.

```json
{
  "nodes": 110999,
  "edges": 246606,
  "avg_out_degree": 2.22,
  "total_km": 9311.1,
  "bbox": [77.32, 28.42, 77.57, 28.63]
}
```

---

### POST /api/sim/control
Control the traffic simulation.

**Body:**
```json
{"action": "play"}
{"action": "pause"}
{"action": "reset"}
{"action": "speed",         "multiplier": 3.0}
{"action": "set_vehicles",  "n": 800}
{"action": "set_scenario",  "scenario": "rush_hour"}
```

Scenarios: `"light"` | `"moderate"` | `"heavy"` | `"rush_hour"` | `"follow_clock"`

---

### POST /api/sim/incident
Create a traffic incident on an edge.

```json
{
  "lat": 28.5705, "lon": 77.3219,
  "type": "slow",
  "factor": 0.3,
  "duration_min": 15
}
```

Or `"edge_id": 12345` instead of lat/lon.

---

## WebSocket: /ws

All messages are JSON text frames.

### Client → Server

| Type | Fields | Description |
|------|--------|-------------|
| `ping` | `ts` (ms epoch) | Keepalive |
| `subscribe` | `bbox`, `zoom` | Subscribe to traffic deltas in viewport |
| `share_location` | `lat`,`lon`,`speed`,`heading`,`ts`,`consent:true` | Opt-in crowd-sourced location |
| `watch_route` | `from`,`to` | Start reroute monitoring |
| `unwatch_route` | — | Stop reroute monitoring |

### Server → Client

| Type | Description |
|------|-------------|
| `hello` | Sent on connect; includes `data_source` label |
| `pong` | Response to `ping` |
| `subscribed` | Confirms subscription |
| `sim_state` | Each tick: vehicle_count, tick_id, tick_ms, reduced_fidelity |
| `traffic_delta` | Edge weight deltas for subscribed bbox (Phase 4) |
| `vehicles` | Vehicle positions in subscribed viewport (Phase 4) |
| `incident` | New or expired incident |
| `reroute_suggestion` | `{route_id, new_route, saving_s}` — user decides |
| `participants` | `{n}` — live user count (no positions) |
| `location_ack` | Confirms a `share_location` was processed |

**Heartbeat:** Server pings every 25 s. Clients should reconnect with exponential backoff if no message for 60 s.
