// js/app.js — SmartCity main application (Phase 3)
// ES module entry point. Wires API, map, WebSocket, and UI.

import * as API  from './api.js';
import * as Map  from './map.js';
import * as WS   from './ws.js';
import { initSearch, setSearchBias } from './search.js';
import { initSimPanel } from './sim-panel.js';
import { initTheme, isDark } from './theme.js';

// ── State ──────────────────────────────────────────────────────────────────

let meta         = null;    // /api/meta response
let currentRoute = null;    // last route response
let navActive    = false;
let simDriveActive = false;

// ── Startup ────────────────────────────────────────────────────────────────

async function main() {
    // 1. Theme
    const theme = initTheme();

    // 2. Cold-start overlay
    const overlay   = document.getElementById('cold-start-overlay');
    const statusEl  = document.getElementById('cold-start-status');
    const progressEl = document.getElementById('cold-start-progress');

    const onAttempt = (i, max) => {
        if (statusEl)  statusEl.textContent = `Attempt ${i} of ${max}…`;
        if (progressEl) progressEl.style.width = `${(i / max) * 100}%`;
    };

    // 3. Initialise map immediately (renders while server wakes)
    const mapInst = Map.initMap('map', theme);

    // 4. Wire search once map is loaded (needs centre for bias)
    mapInst.on('load', () => {
        const centre = mapInst.getCenter();
        setSearchBias(centre.lat, centre.lng);
        initSearch({
            onSelect: handlePlaceSelect,
        });
    });

    // 5. Wait for server
    const ok = await API.waitForServer(onAttempt);
    if (!ok) {
        if (statusEl) statusEl.textContent = 'Server unavailable — try refreshing.';
        return;
    }

    // 6. Hide cold-start overlay
    if (overlay) {
        overlay.classList.add('hidden');
        setTimeout(() => overlay.remove(), 500);
    }

    // 7. Load meta
    try {
        meta = await API.getMeta();
        _updateDataSourceBadge(meta.data_source);
        _updateSimStatus(meta.sim);
    } catch (e) {
        console.warn('[app] getMeta failed:', e);
    }

    // 8. Connect WebSocket
    WS.connect();
    WS.on('_connected',    () => _setConnectionStatus('connected'));
    WS.on('_disconnected', () => _setConnectionStatus('disconnected'));
    WS.on('sim_state',     _onSimState);
    WS.on('traffic_delta', Map.applyTrafficDelta);
    WS.on('vehicles',      msg => Map.updateVehicles(msg.positions || []));
    WS.on('reroute_suggestion', _onRerouteSuggestion);

    // 9. Wire UI controls
    _wireControls();

    // 10. Sim panel
    initSimPanel({ onControl: _onSimControl, onIncident: _onSimIncident });
}

// ── Place selection ────────────────────────────────────────────────────────

function handlePlaceSelect(place, role) {
    // role: 'from' | 'to' | 'explore'
    Map.flyTo(place.lon, place.lat, 16);
    if (role === 'from' || role === 'to') {
        _updateRouteField(role, place);
        _tryComputeRoute();
    }
}

// From/to slots
const routeSlots = { from: null, to: null };

function _updateRouteField(role, place) {
    routeSlots[role] = place;
    const el = document.getElementById(role === 'from' ? 'search-from' : 'search-to');
    if (el) el.value = place.name;
}

async function _tryComputeRoute() {
    const { from, to } = routeSlots;
    if (!from || !to) return;

    _setRouteLoading(true);
    try {
        const wantExplored = document.getElementById('btn-graph-mode')?.classList.contains('active');
        const data = await API.route(
            { lat: from.lat, lon: from.lon },
            { lat: to.lat,   lon: to.lon   },
            { mode: 'traffic', alternatives: 0, wantExplored, horizonMin }
        );
        currentRoute = data;
        Map.showRoute(data);
        _showRoutePanel(data);

        if (wantExplored && data.explored?.length > 0) {
            Map.animateExplored(data.explored, 3);
        }
    } catch (e) {
        console.error('[app] Route error:', e);
        _showRouteError(e.message || 'Route computation failed');
    } finally {
        _setRouteLoading(false);
    }
}

// ── Horizon (leave-time) ────────────────────────────────────────────────────

let horizonMin = 0;

function _wireLeaveTime() {
    document.querySelectorAll('[data-horizon]').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('[data-horizon]').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            horizonMin = parseInt(btn.dataset.horizon, 10);
            // Re-run route with new horizon if we have a current route
            if (routeSlots.from && routeSlots.to) _tryComputeRoute();
        });
    });
}

// ── Route panel ────────────────────────────────────────────────────────────

function _showRoutePanel(data) {
    const panel = document.getElementById('route-panel');
    if (!panel) return;

    const r = data.routes[0];
    if (!r) return;

    const distKm  = (r.distance_m / 1000).toFixed(1);
    const durMin  = Math.round(r.duration_s / 60);
    const delayMin = Math.round((r.delay_s || 0) / 60);
    const eta = new Date(Date.now() + r.duration_s * 1000).toLocaleTimeString('en-IN', {
        hour: '2-digit', minute: '2-digit',
    });

    const distEl    = document.getElementById('route-distance');
    const durEl     = document.getElementById('route-duration');
    const etaEl     = document.getElementById('route-eta');
    const delayEl   = document.getElementById('route-delay');
    const statsEl   = document.getElementById('route-stats');
    const stepsEl   = document.getElementById('route-steps');

    if (distEl)  distEl.textContent  = `${distKm} km`;
    if (durEl)   durEl.textContent   = `${durMin} min`;
    if (etaEl)   etaEl.textContent   = eta;
    if (delayEl && r.delay_s > 30) {
        delayEl.textContent = `+${delayMin} min vs usual`;
        delayEl.classList.remove('hidden');
    } else if (delayEl) {
        delayEl.classList.add('hidden');
    }

    // Algo stats (graph theory visibility)
    if (statsEl) {
        const s = r.stats;
        statsEl.innerHTML =
            `<span title="Dijkstra settled">🔵 ${s.nodes_settled.toLocaleString()} nodes</span> · ` +
            `<span title="Edges relaxed">↗️ ${s.edges_relaxed.toLocaleString()} edges</span> · ` +
            `<span title="Compute time">${s.dijkstra_ms.toFixed(1)} ms</span>`;
    }

    // Turn-by-turn steps
    if (stepsEl) {
        stepsEl.innerHTML = r.steps.map(s =>
            `<li class="step-item">
                <span class="step-maneuver step-${s.maneuver.replace(/[^a-z]/g,'-')}">${_maneuverIcon(s.maneuver)}</span>
                <span class="step-road">${s.road_name || 'Unnamed road'}</span>
                <span class="step-dist">${_fmtDist(s.distance_m)}</span>
            </li>`
        ).join('');
    }

    // Show prediction label when horizon > 0
    if (horizonMin > 0 && durEl) {
        durEl.innerHTML = `${durMin} min <span class="route-pred-tag">📅 +${horizonMin}min predicted</span>`;
    }

    // Update data-source badge from server response
    if (data.data_source) _updateDataSourceBadge(data.data_source);

    panel.classList.remove('hidden');
}

function _maneuverIcon(m) {
    const icons = {
        'depart': '🚦', 'arrive': '🏁',
        'straight': '⬆️', 'keep-right': '↗️', 'keep-left': '↖️',
        'turn-right': '➡️', 'turn-left': '⬅️',
        'turn-right-sharp': '↪️', 'turn-left-sharp': '↩️',
    };
    return icons[m] || '•';
}

function _fmtDist(m) {
    if (m >= 1000) return `${(m/1000).toFixed(1)} km`;
    return `${Math.round(m)} m`;
}

function _setRouteLoading(on) {
    const btn = document.getElementById('btn-get-route');
    if (btn) {
        btn.disabled = on;
        btn.textContent = on ? 'Computing…' : 'Get Route';
    }
}

function _showRouteError(msg) {
    const el = document.getElementById('route-error');
    if (el) { el.textContent = msg; el.classList.remove('hidden'); }
}

// ── Reroute suggestion ─────────────────────────────────────────────────────

function _onRerouteSuggestion(msg) {
    const savMin = Math.round((msg.saving_s || 0) / 60);
    if (savMin < 2) return;  // ignore small savings
    const banner = document.getElementById('reroute-banner');
    const text   = document.getElementById('reroute-text');
    if (!banner || !text) return;
    text.textContent = `Faster route saves ${savMin} min`;
    banner.classList.remove('hidden');

    document.getElementById('btn-reroute-switch')?.addEventListener('click', () => {
        currentRoute = { routes: [msg.new_route] };
        Map.showRoute(currentRoute);
        _showRoutePanel(currentRoute);
        banner.classList.add('hidden');
    }, { once: true });

    document.getElementById('btn-reroute-keep')?.addEventListener('click', () => {
        banner.classList.add('hidden');
    }, { once: true });
}

// ── WS handlers ────────────────────────────────────────────────────────────

function _onSimState(msg) {
    _updateSimStatus({
        running:       msg.vehicle_count > 0,
        vehicle_count: msg.vehicle_count,
        speed_mult:    msg.speed_mult,
        tick_id:       msg.tick,
    });
    const rdEl = document.getElementById('sim-reduced-fidelity');
    if (rdEl) rdEl.classList.toggle('hidden', !msg.reduced_fidelity);
}

function _updateSimStatus(st) {
    const cntEl = document.getElementById('sim-vehicle-count');
    const spdEl = document.getElementById('sim-speed-mult');
    if (cntEl && st) cntEl.textContent = st.vehicle_count?.toLocaleString() ?? '—';
    if (spdEl && st) spdEl.textContent = `${st.speed_mult ?? 1}×`;
}

// ── UI controls ────────────────────────────────────────────────────────────

function _wireControls() {
    // Leave-time selector
    _wireLeaveTime();

    // Share-location FAB
    _wireLocationSharing();

    // Layer toggles
    _bindToggle('btn-traffic',    on => Map.setTrafficVisible(on));
    _bindToggle('btn-vehicles',   on => Map.setVehiclesVisible(on));
    _bindToggle('btn-graph-mode', on => {
        Map.setGraphModeVisible(on);
        document.getElementById('graph-mode-panel')?.classList.toggle('hidden', !on);
    });

    // Swap A/B
    document.getElementById('btn-swap')?.addEventListener('click', () => {
        [routeSlots.from, routeSlots.to] = [routeSlots.to, routeSlots.from];
        const a = document.getElementById('search-from');
        const b = document.getElementById('search-to');
        if (a && b) [a.value, b.value] = [b.value, a.value];
        if (routeSlots.from && routeSlots.to) _tryComputeRoute();
    });

    // Map right-click → context menu
    let _ctxLngLat = null;
    Map.addContextMenu(async (e) => {
        _ctxLngLat = e.lngLat;
        const menu = document.getElementById('context-menu');
        if (!menu) return;
        menu.style.left = `${e.originalEvent.clientX}px`;
        menu.style.top  = `${e.originalEvent.clientY}px`;
        menu.classList.remove('hidden');
    });

    // Context menu actions
    document.getElementById('ctx-dir-from')?.addEventListener('click', async () => {
        if (!_ctxLngLat) return;
        document.getElementById('context-menu')?.classList.add('hidden');
        const { lat, lng } = _ctxLngLat;
        try {
            const r = await API.reverse(lat, lng);
            const place = { id: `pin-from-${Date.now()}`, name: r.road_name || `${lat.toFixed(4)},${lng.toFixed(4)}`, lat, lon: lng };
            _updateRouteField('from', place);
            _tryComputeRoute();
        } catch (_) {}
    });

    document.getElementById('ctx-dir-to')?.addEventListener('click', async () => {
        if (!_ctxLngLat) return;
        document.getElementById('context-menu')?.classList.add('hidden');
        const { lat, lng } = _ctxLngLat;
        try {
            const r = await API.reverse(lat, lng);
            const place = { id: `pin-to-${Date.now()}`, name: r.road_name || `${lat.toFixed(4)},${lng.toFixed(4)}`, lat, lon: lng };
            _updateRouteField('to', place);
            _tryComputeRoute();
        } catch (_) {}
    });

    document.getElementById('ctx-incident')?.addEventListener('click', async () => {
        if (!_ctxLngLat) return;
        document.getElementById('context-menu')?.classList.add('hidden');
        const { lat, lng } = _ctxLngLat;
        await _onSimIncident({ lat, lon: lng, type: 'slow', factor: 0.3, duration_min: 15 });
    });

    // Close context menu on outside click
    document.addEventListener('click', () => {
        document.getElementById('context-menu')?.classList.add('hidden');
    }, { passive: true });

    // Map left-click → reverse geocode (log only for now)
    Map.addClickHandler(async (e) => {
        document.getElementById('context-menu')?.classList.add('hidden');
        const { lat, lng } = e.lngLat;
        try {
            const r = await API.reverse(lat, lng);
            const name = r.road_name || `${lat.toFixed(5)}, ${lng.toFixed(5)}`;
            console.log('[app] reverse:', name, r);
        } catch (_) {}
    });

    // Theme toggle — update emoji and map style
    const themeBtn = document.getElementById('btn-theme');
    if (themeBtn) {
        themeBtn.addEventListener('click', () => {
            const newTheme = isDark() ? 'light' : 'dark';
            Map.setTheme(newTheme);
            document.body.classList.toggle('light-theme', newTheme === 'light');
        });
    }

    // Clear route
    document.getElementById('btn-clear-route')?.addEventListener('click', () => {
        routeSlots.from = routeSlots.to = null;
        currentRoute = null;
        Map.clearRoute();
        document.getElementById('route-panel')?.classList.add('hidden');
    });
}

function _bindToggle(id, fn) {
    const btn = document.getElementById(id);
    if (!btn) return;
    btn.addEventListener('click', () => {
        const active = btn.classList.toggle('active');
        fn(active);
    });
}

// ── Sim control forwarding ─────────────────────────────────────────────────

async function _onSimControl(action, extra) {
    try {
        await API.simControl(action, extra);
    } catch (e) {
        console.error('[app] sim control failed:', e);
    }
}

async function _onSimIncident(opts) {
    try {
        await API.simIncident(opts);
    } catch (e) {
        console.error('[app] sim incident failed:', e);
    }
}

// ── Status indicators ──────────────────────────────────────────────────────

function _setConnectionStatus(state) {
    const el = document.getElementById('ws-status');
    if (!el) return;
    el.className = `ws-status ws-${state}`;
    el.title = state;
}

function _updateDataSourceBadge(label) {
    const el = document.getElementById('data-source-badge');
    if (el) el.textContent = label || 'SIMULATED LIVE';
}

// ── Location sharing (Phase 7) ─────────────────────────────────────────────

async function _wireLocationSharing() {
    const btn = document.getElementById('btn-share-location');
    if (!btn) return;

    // Lazy import — only loads location.js when user clicks
    btn.addEventListener('click', async () => {
        const Loc = await import('./location.js');
        if (Loc.isSharing()) {
            Loc.stopSharing();
            btn.classList.remove('sharing');
            btn.title = 'Share location (opt-in)';
        } else {
            try {
                await Loc.startSharing();
                btn.classList.add('sharing');
                btn.title = 'Stop sharing location';
            } catch (e) {
                console.warn('[app] location sharing denied or unavailable:', e.message);
            }
        }
    });
}

// ── Boot ───────────────────────────────────────────────────────────────────

main().catch(console.error);
