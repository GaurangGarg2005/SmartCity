# SmartCity — Deployment Guide

> Phase 0 skeleton. Step-by-step instructions and free-tier budget table filled after Phase 9.

## Free-Tier Budget (verified 2026-10-04)

| Provider | Resource | Limit | Notes |
|---|---|---|---|
| Render free web service | RAM | 512 MB | Hard limit |
| Render free web service | CPU | 0.1 vCPU | Shared |
| Render free web service | Sleep | After 15 min inactivity | ~1 min to wake |
| Render free web service | Instance hours | 750 hrs/month/workspace | Shared across all free services |
| Render free web service | Disk | Ephemeral | No persistence across restarts |
| Open-Meteo | API calls | No limit / no key | CC BY 4.0; polling every 15 min → ~96 req/day |
| OpenFreeMap | Tiles | No documented hard limit | MIT; self-hosted tiles; be polite |
| Overpass API | Queries | No hard limit; fair use | Cache raw XML; run once at build time |
| TomTom Traffic Flow | Free tier | To be verified in Phase 7 | Key required; terms re: non-TomTom basemap TBD |

## Step-by-Step Deploy Instructions

> **Prerequisites**: Docker installed locally. Render account (free). GitHub account. `git` CLI.

### 1. Prepare the repository
```bash
git init
git remote add origin https://github.com/YOUR_USER/SmartCity.git
git add .
git commit -m "feat: SmartCity initial release"
git push -u origin main
```

### 2. Build the data pipeline (run once)
```bash
pip install osmium-tool requests tqdm
python tools/build_graph.py          # → data/graph.bin
python tools/build_places.py         # → data/places.json
git add data/graph.bin data/places.json
git commit -m "data: add compiled graph and places"
git push
```

### 3. Create the Render service
1. Go to [dashboard.render.com](https://dashboard.render.com) → **New → Web Service**
2. Connect your GitHub repo.
3. Render auto-detects the `render.yaml`. Confirm settings:
   - **Runtime**: Docker
   - **Health Check Path**: `/healthz`
   - **Plan**: Free
4. In **Environment** tab, set secrets (do NOT put in `render.yaml`):
   - `ADMIN_PIN` = any PIN you choose (or leave blank for open demo)
   - `TOMTOM_API_KEY` = if using TomTom (optional, leave blank by default)
5. Click **Create Web Service**.

### 4. Wait for build (~5–10 min first deploy)
Watch logs in Render dashboard. Successful start shows:
```
[INFO] Graph loaded: NNN nodes, EEE edges
[INFO] SmartCity server listening on port 10000
```

### 5. Update CORS_ORIGIN
After first deploy, copy the `.onrender.com` URL and update it:
- Render Dashboard → Environment → `CORS_ORIGIN` = `https://smartcity.onrender.com`
- **Manual deploy** to apply.

---

## Post-Deploy Verification Checklist


```
GET  https://smartcity.onrender.com/healthz            → 200 "ok"
GET  https://smartcity.onrender.com/api/meta           → JSON with nodes/edges/sim
GET  https://smartcity.onrender.com/api/search?q=noida → JSON results array
POST https://smartcity.onrender.com/api/route          → JSON with routes array
WS   wss://smartcity.onrender.com/ws                   → {"type":"hello",...}
```

After confirming, the front end should auto-connect via the cold-start overlay.

## Keep-Alive Strategy

Render's free services sleep after 15 minutes of inactivity. Options:

1. **WebSocket heartbeats** (built-in): as long as a browser has the app open,
   ping/pong every 25 seconds keeps the service awake.
2. **External pinger** (opt-in, not enabled by default): any free uptime monitor
   (e.g., UptimeRobot free tier — 50 monitors, 5-minute interval) can hit
   `/healthz` every ~10 minutes. Trade-off: consuming instance hours even when
   nobody is using the app may exhaust the 750 hr/month budget if you also run
   other free services. Document and make it opt-in; do not silently rely on it.

## Fallback Hosts

- **Google Cloud Run** free tier: needs a billing account (ask the user first).
  Configure max-instances=1, min-instances=0, long timeout for WebSocket.
- **Hugging Face Docker Spaces**: as of 2026-10-04, free CPU Spaces exist but
  have limited RAM and may require a verified account. Verify before using.
