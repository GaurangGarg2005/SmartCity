// js/app.js — SmartCity main application (Phase 3)
// ES module entry point. Wires API, map, WebSocket, and UI.

import * as API  from './api.js';
import * as Map  from './map.js';
import * as WS   from './ws.js';
import { initSearch, setSearchBias } from './search.js';
import { initSimPanel } from './sim-panel.js';
import { initTheme, setTheme, isDark } from './theme.js';

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
    WS.on('traffic_delta', (delta) => {
        Map.applyTrafficDelta(delta);
        // Also trigger a full refresh to pick up new edge weights from sim
        Map.refreshTrafficIfVisible();
    });
    let _lastVehiclePositions = [];
    async function _applyVehicles(rawPositions) {
        if (!rawPositions || rawPositions.length === 0) return;
        _lastVehiclePositions = rawPositions;
        if (horizonMin === 0) {
            Map.updateVehicles(rawPositions);
        } else {
            try {
                const res = await API.getVehicles(horizonMin);
                if (res && res.positions && res.positions.length > 0) {
                    Map.updateVehicles(res.positions);
                    return;
                }
            } catch (_) {}
            Map.updateVehicles(rawPositions);
        }
    }
    WS.on('vehicles',      msg => _applyVehicles(msg.positions || []));
    WS.on('reroute_suggestion', _onRerouteSuggestion);

    // 9. Wire UI controls
    _wireControls();

    // 10. Sim panel
    initSimPanel({ onControl: _onSimControl, onIncident: _onSimIncident });

    // 11. Initialize layers to match HTML button state (both Traffic + Vehicles start active)
    //     Guard against race: if map already loaded, call directly; otherwise wait for 'load'
    const _initLayers = () => {
        Map.setTrafficVisible(true);
        Map.setVehiclesVisible(true);
    };
    if (Map.getMap().loaded()) {
        _initLayers();
    } else {
        Map.getMap().once('load', _initLayers);
    }

    // 12. Show last 5 recent searches in the welcome panel
    _renderRecentPanel();

    // 13. Clear error message whenever the user modifies search inputs
    ['search-from', 'search-to'].forEach(id => {
        document.getElementById(id)?.addEventListener('input', () => {
            const errEl = document.getElementById('route-error');
            if (errEl) { errEl.textContent = ''; errEl.classList.add('hidden'); }
        });
    });

    // 14. Dynamic traffic: refresh every 5 s when traffic layer is on
    //     (also handled by WS traffic_delta, this is a fallback for reliability)
    setInterval(() => {
        if (document.getElementById('btn-traffic')?.classList.contains('active')) {
            Map.refreshTrafficIfVisible();
        }
    }, 5000);
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

async function _resolveSlotIfEmpty(role) {
    if (routeSlots[role]) return;
    const input = document.getElementById(role === 'from' ? 'search-from' : 'search-to');
    if (!input) return;
    const q = input.value.trim();
    if (!q) return;
    try {
        const res = await API.search(q, 28.5355, 77.3910, 1);
        if (res && res.length > 0) {
            routeSlots[role] = res[0];
            input.value = res[0].name;
        }
    } catch (_) {}
}

async function _tryComputeRoute() {
    const errEl = document.getElementById('route-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.add('hidden'); }

    await _resolveSlotIfEmpty('from');
    await _resolveSlotIfEmpty('to');

    const { from, to } = routeSlots;
    if (!from || !to) {
        _showRouteError('Please choose both a starting point and destination.');
        return;
    }

    _setRouteLoading(true);
    try {
        const wantExplored = true; // Always fetch explored nodes for Dijkstra stats & frontier
        const data = await API.route(
            { lat: from.lat, lon: from.lon },
            { lat: to.lat,   lon: to.lon   },
            { mode: 'traffic', alternatives: 2, wantExplored, horizonMin }
        );
        currentRoute = data;
        activeRouteIdx = 0;
        Map.showRoute(data, 0);
        _showRoutePanel(data, 0);

        const isGraphMode = document.getElementById('btn-graph-mode')?.classList.contains('active');
        if (isGraphMode && data.explored?.length > 0) {
            Map.animateExplored(data.explored, 3);
        }
        _updateGraphOverlay(data);
    } catch (e) {
        console.error('[app] Route error:', e);
        // Parse common backend error messages into friendly text
        let msg = e.message || 'Route computation failed';
        if (msg.includes('not found') || msg.includes('No node') || msg.includes('not in graph')) {
            msg = 'One or both locations are not on the road network. Try a nearby road or intersection.';
        } else if (msg.includes('No path') || msg.includes('unreachable')) {
            msg = 'No route found between these points. They may be disconnected in the road network.';
        }
        _showRouteError(msg);
    } finally {
        _setRouteLoading(false);
    }
}

// ── Horizon (leave-time) ────────────────────────────────────────────────────

let horizonMin = 0;
let activeRouteIdx = 0;

function _syncDepartureUI(min) {
    let matchedPreset = false;
    document.querySelectorAll('[data-horizon]').forEach(btn => {
        const h = parseInt(btn.dataset.horizon, 10);
        if (h === min) {
            btn.classList.add('active');
            matchedPreset = true;
        } else {
            btn.classList.remove('active');
        }
    });

    const customToggle = document.getElementById('leave-custom-toggle');
    const customWrap = document.getElementById('custom-departure-wrap');
    const slider = document.getElementById('custom-departure-slider');
    const numInput = document.getElementById('custom-departure-input');
    const statusEl = document.getElementById('custom-departure-status');

    if (slider) slider.value = Math.min(min, 60);
    if (numInput) numInput.value = min;

    if (!matchedPreset) {
        if (customToggle) customToggle.classList.add('active');
        if (customWrap) customWrap.classList.remove('hidden');
    }

    if (statusEl) {
        if (min === 0) {
            statusEl.textContent = 'Departing now • Real-time traffic & vehicle positions';
        } else {
            const departTime = new Date(Date.now() + min * 60000).toLocaleTimeString('en-IN', {
                hour: '2-digit', minute: '2-digit'
            });
            statusEl.textContent = `Departing in +${min} min (${departTime}) • Predicted vehicle movement & traffic`;
        }
    }
}

async function _updateHorizon(min) {
    horizonMin = Math.max(0, parseInt(min, 10) || 0);
    _syncDepartureUI(horizonMin);

    // Dynamic vehicle prediction on road network
    if (horizonMin === 0) {
        Map.updateVehicles(_lastVehiclePositions);
    } else {
        try {
            const res = await API.getVehicles(horizonMin);
            if (res && res.positions) {
                Map.updateVehicles(res.positions);
            }
        } catch (_) {}
    }

    // Dynamic traffic line prediction
    Map.refreshTrafficIfVisible(horizonMin);

    // Dynamic route recalculation with arrival duration matching predicted traffic
    if (routeSlots.from && routeSlots.to) {
        _tryComputeRoute();
    }
}

function _wireLeaveTime() {
    document.querySelectorAll('[data-horizon]').forEach(btn => {
        btn.addEventListener('click', () => {
            const h = parseInt(btn.dataset.horizon, 10);
            document.getElementById('custom-departure-wrap')?.classList.add('hidden');
            document.getElementById('leave-custom-toggle')?.classList.remove('active');
            _updateHorizon(h);
        });
    });

    const customToggle = document.getElementById('leave-custom-toggle');
    const customWrap = document.getElementById('custom-departure-wrap');
    const slider = document.getElementById('custom-departure-slider');
    const numInput = document.getElementById('custom-departure-input');

    customToggle?.addEventListener('click', () => {
        const isHidden = customWrap?.classList.toggle('hidden');
        if (!isHidden) {
            document.querySelectorAll('[data-horizon]').forEach(b => b.classList.remove('active'));
            customToggle.classList.add('active');
            const h = parseInt(numInput?.value || slider?.value || '15', 10);
            _updateHorizon(h);
        }
    });

    let debounceTimer = null;
    const onCustomChange = (val) => {
        const h = Math.max(0, parseInt(val, 10) || 0);
        if (slider) slider.value = Math.min(h, 60);
        if (numInput) numInput.value = h;
        clearTimeout(debounceTimer);
        debounceTimer = setTimeout(() => {
            _updateHorizon(h);
        }, 150);
    };

    slider?.addEventListener('input', e => onCustomChange(e.target.value));
    numInput?.addEventListener('input', e => onCustomChange(e.target.value));
}

// ── Route panel ────────────────────────────────────────────────────────────

function _showRoutePanel(data, selectedIdx = 0) {
    const panel = document.getElementById('route-panel');
    if (!panel) return;

    activeRouteIdx = selectedIdx;
    const r = data.routes[selectedIdx] || data.routes[0];
    if (!r) return;

    _renderRouteTabs(data, selectedIdx);

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

    // Show prediction label when horizon > 0 with updated ETA
    if (durEl) {
        const horizonOffset = horizonMin * 60 * 1000; // ms
        const predictedETA = new Date(Date.now() + horizonOffset + r.duration_s * 1000).toLocaleTimeString('en-IN', {
            hour: '2-digit', minute: '2-digit',
        });
        if (horizonMin > 0) {
            durEl.innerHTML = `${durMin} min <span class="route-pred-tag">📅 +${horizonMin}min predicted</span>`;
            if (etaEl) etaEl.textContent = predictedETA;
        } else {
            durEl.textContent = `${durMin} min`;
        }
    }

    // Update data-source badge from server response
    // Hide error banner on success
    const errEl = document.getElementById('route-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.add('hidden'); }

    // Hide welcome panel, show route panel
    document.getElementById('panel-welcome')?.classList.add('hidden');
    panel.classList.remove('hidden');
}

function _renderRouteTabs(data, activeIdx) {
    const tabsContainer = document.getElementById('route-tabs');
    if (!tabsContainer) return;
    if (!data.routes || data.routes.length <= 1) {
        tabsContainer.innerHTML = '';
        tabsContainer.classList.add('hidden');
        return;
    }
    tabsContainer.classList.remove('hidden');
    tabsContainer.innerHTML = data.routes.map((r, i) => {
        const durMin = Math.round(r.duration_s / 60);
        const distKm = (r.distance_m / 1000).toFixed(1);
        const label = r.label || (i === 0 ? 'Fastest' : `Alt ${i}`);
        const isActive = i === activeIdx;
        return `
            <button class="route-tab ${isActive ? 'active' : ''}" data-idx="${i}" role="tab" aria-selected="${isActive}">
                <strong>${durMin} min</strong>
                <span>${label} · ${distKm} km</span>
            </button>
        `;
    }).join('');

    tabsContainer.querySelectorAll('.route-tab').forEach(btn => {
        btn.addEventListener('click', () => {
            const idx = parseInt(btn.dataset.idx, 10);
            _selectRoute(idx);
        });
    });
}

function _selectRoute(idx) {
    if (!currentRoute || !currentRoute.routes[idx]) return;
    activeRouteIdx = idx;
    Map.setSelectedRoute(idx);
    _showRoutePanel(currentRoute, idx);
}

window.addEventListener('sc:select_route', (e) => {
    if (e.detail && e.detail.idx !== undefined) {
        _selectRoute(e.detail.idx);
    }
});

function _updateGraphOverlay(data) {
    const r = data?.routes?.[0];
    if (!r) return;
    const infoEl = document.getElementById('dijkstra-info');
    const statsEl = document.getElementById('dijkstra-stats');
    const ctrlEl = document.getElementById('dijkstra-controls');
    const pathEq = document.getElementById('path-equation');
    const pathFormula = document.getElementById('path-formula');

    if (infoEl) infoEl.textContent = `Dijkstra settled ${r.stats.nodes_settled.toLocaleString()} nodes to find optimal path P*.`;
    if (ctrlEl) ctrlEl.classList.remove('hidden');
    if (statsEl) {
        statsEl.textContent = `|V_settled|: ${r.stats.nodes_settled} · |E_relaxed|: ${r.stats.edges_relaxed} · ${r.stats.dijkstra_ms.toFixed(1)} ms`;
    }
    if (pathEq && pathFormula) {
        pathEq.classList.remove('hidden');
        pathFormula.textContent = `argmin_{P} ∑ w(e) = ${Math.round(r.duration_s)}s across ${(r.distance_m/1000).toFixed(1)} km`;
    }
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

let _lastTrafficRefresh = 0;
function _onSimState(msg) {
    _updateSimStatus({
        running:       msg.vehicle_count > 0,
        vehicle_count: msg.vehicle_count,
        speed_mult:    msg.speed_mult,
        tick_id:       msg.tick,
    });
    const rdEl = document.getElementById('sim-reduced-fidelity');
    if (rdEl) rdEl.classList.toggle('hidden', !msg.reduced_fidelity);

    // Dynamically refresh traffic layer every 2 seconds as vehicles move around
    const now = Date.now();
    if (now - _lastTrafficRefresh > 2000) {
        _lastTrafficRefresh = now;
        Map.refreshTrafficIfVisible(horizonMin);
    }
}

function _updateSimStatus(st) {
    const cntEl = document.getElementById('sim-vehicle-count');
    const spdEl = document.getElementById('sim-speed-mult');
    if (cntEl && st) cntEl.textContent = st.vehicle_count?.toLocaleString() ?? '—';
    if (spdEl && st) spdEl.textContent = `${st.speed_mult ?? 1}×`;
}

// ── UI controls ────────────────────────────────────────────────────────────

function _wireControls() {
    // Clear route error on any search input change
    ['search-from', 'search-to'].forEach(id => {
        document.getElementById(id)?.addEventListener('input', () => {
            const errEl = document.getElementById('route-error');
            if (errEl) { errEl.textContent = ''; errEl.classList.add('hidden'); }
        });
    });

    // Leave-time selector
    _wireLeaveTime();

    // Share-location FAB
    _wireLocationSharing();

    // Get Route button
    document.getElementById('btn-get-route')?.addEventListener('click', () => {
        _tryComputeRoute();
    });

    // Layer toggles
    _bindToggle('btn-traffic',    on => Map.setTrafficVisible(on));
    _bindToggle('btn-vehicles',   on => Map.setVehiclesVisible(on));
    _bindToggle('btn-graph-mode', on => {
        Map.setGraphModeVisible(on);
        const overlay = document.getElementById('graph-overlay');
        if (overlay) overlay.classList.toggle('hidden', !on);
        if (on) {
            if (currentRoute) {
                _updateGraphOverlay(currentRoute);
                if (currentRoute.explored?.length > 0) {
                    Map.animateExplored(currentRoute.explored, 3);
                }
            } else {
                const infoEl = document.getElementById('dijkstra-info');
                if (infoEl) {
                    infoEl.innerHTML = '<strong>Graph Theory Mode Active</strong><br/>Visualizing road network vertices V (cyan dots) and edges E (dashed lines). Choose a start and destination to view Dijkstra shortest-path frontier exploration in real time.';
                }
            }
        }
    });

    document.getElementById('btn-graph-close')?.addEventListener('click', () => {
        const btn = document.getElementById('btn-graph-mode');
        if (btn) {
            btn.classList.remove('active');
            btn.setAttribute('aria-pressed', 'false');
        }
        Map.setGraphModeVisible(false);
        document.getElementById('graph-overlay')?.classList.add('hidden');
    });

    // Dijkstra visualizer replay button
    document.getElementById('btn-viz-play')?.addEventListener('click', () => {
        if (currentRoute?.explored?.length > 0) {
            const speedSlider = document.getElementById('viz-speed');
            const speed = speedSlider ? (11 - parseInt(speedSlider.value, 10)) * 2 : 4;
            Map.animateExplored(currentRoute.explored, speed);
        }
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

    // Map navigation controls (+, -, compass, my-location)
    const mapObj = Map.getMap();
    document.getElementById('btn-zoom-in')?.addEventListener('click', () => {
        if (Map.getMap()) Map.getMap().zoomIn({ duration: 250 });
    });
    document.getElementById('btn-zoom-out')?.addEventListener('click', () => {
        if (Map.getMap()) Map.getMap().zoomOut({ duration: 250 });
    });
    document.getElementById('btn-compass')?.addEventListener('click', () => {
        if (Map.getMap()) Map.getMap().resetNorthPitch({ duration: 400 });
    });
    const _addAndShowMyVehicle = async (lat, lon) => {
        Map.flyTo(lon, lat, 15);
        Map.setUserMarker(lat, lon, 'YOU');
        try {
            const res = await API.addMyVehicle(lat, lon);
            if (res && res.road_name) {
                console.log(`[sim] Your vehicle added on ${res.road_name}`);
            }
        } catch (_) {}
    };

    document.getElementById('btn-my-loc')?.addEventListener('click', () => {
        if (!navigator.geolocation) {
            _addAndShowMyVehicle(28.5355, 77.3910);
            return;
        }
        navigator.geolocation.getCurrentPosition(
            pos => {
                _addAndShowMyVehicle(pos.coords.latitude, pos.coords.longitude);
            },
            err => {
                console.warn('[app] Geolocation denied/unavailable in browser:', err);
                _addAndShowMyVehicle(28.5355, 77.3910);
            },
            { enableHighAccuracy: true, timeout: 4000 }
        );
    });

    // Clear route
    document.getElementById('btn-clear-route')?.addEventListener('click', () => {
        routeSlots.from = routeSlots.to = null;
        currentRoute = null;
        Map.clearRoute();
        document.getElementById('route-panel')?.classList.add('hidden');
        document.getElementById('panel-welcome')?.classList.remove('hidden');
        _renderRecentPanel();
    });
}

function _bindToggle(id, fn) {
    const btn = document.getElementById(id);
    if (!btn) return;
    btn.addEventListener('click', () => {
        const active = btn.classList.toggle('active');
        btn.setAttribute('aria-pressed', active ? 'true' : 'false');
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

    btn.addEventListener('click', async () => {
        const _handleShare = async (lat, lon) => {
            Map.flyTo(lon, lat, 15);
            Map.setUserMarker(lat, lon, 'YOU');
            btn.classList.add('sharing');
            btn.title = 'Your vehicle added to live traffic';
            try {
                await API.addMyVehicle(lat, lon);
            } catch (_) {}
        };

        if (!navigator.geolocation) {
            _handleShare(28.5355, 77.3910);
            return;
        }
        navigator.geolocation.getCurrentPosition(
            pos => {
                _handleShare(pos.coords.latitude, pos.coords.longitude);
            },
            err => {
                console.warn('[app] Geolocation denied/unavailable:', err);
                _handleShare(28.5355, 77.3910);
            },
            { enableHighAccuracy: true, timeout: 4000 }
        );
    });
}

// ── Recent searches welcome panel ─────────────────────────────────────────

function _renderRecentPanel() {
    const RECENT_KEY = 'sc_recent_searches';
    const container = document.getElementById('recent-searches');
    if (!container) return;
    let recent = [];
    try { recent = JSON.parse(localStorage.getItem(RECENT_KEY) || '[]'); } catch (_) {}
    recent = recent.slice(0, 5);
    if (recent.length === 0) {
        container.innerHTML = '';
        return;
    }
    container.innerHTML = `
        <p class="recent-title">Recent searches</p>
        <ul class="recent-list">
          ${recent.map(p => `
            <li class="recent-item" data-lat="${p.lat}" data-lon="${p.lon}" data-name="${_escHtml(p.name)}" tabindex="0">
              <span class="recent-icon">🕐</span>
              <span class="recent-name">${_escHtml(p.name)}</span>
            </li>`).join('')}
        </ul>`;
    container.querySelectorAll('.recent-item').forEach(li => {
        const place = {
            id: `recent-${li.dataset.lat}-${li.dataset.lon}`,
            name: li.dataset.name,
            lat: parseFloat(li.dataset.lat),
            lon: parseFloat(li.dataset.lon),
        };
        const pick = () => {
            _updateRouteField('to', place);
            Map.flyTo(place.lon, place.lat, 16);
            document.getElementById('panel-welcome')?.classList.add('hidden');
            document.getElementById('route-panel')?.classList.remove('hidden');
            if (routeSlots.from) {
                _tryComputeRoute();
            }
        };
        li.addEventListener('click', pick);
        li.addEventListener('keydown', e => { if (e.key === 'Enter') pick(); });
    });
}

window.addEventListener('sc:recent_updated', () => {
    _renderRecentPanel();
});

function _escHtml(s) {
    return String(s)
        .replace(/&/g, '&amp;').replace(/</g, '&lt;')
        .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// ── Boot ───────────────────────────────────────────────────────────────────

main().catch(console.error);
