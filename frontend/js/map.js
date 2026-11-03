// js/map.js — MapLibre GL JS map initialisation and layer management (Phase 3)
// Uses OpenFreeMap vector tiles (MIT license) — no API key required.

import { getTraffic, dataSourceLabel } from './api.js';

// ── Constants ──────────────────────────────────────────────────────────────

/** Noida city centre — initial map centre */
const NOIDA_CENTER = [77.3910, 28.5355];
const NOIDA_BBOX   = [77.32, 28.42, 77.57, 28.63];

// OpenFreeMap style URLs (no key required, MIT license)
const STYLE_DARK  = 'https://tiles.openfreemap.org/styles/dark';
const STYLE_LIGHT = 'https://tiles.openfreemap.org/styles/liberty';

// Traffic level → line color
const TRAFFIC_COLORS = {
    free:     '#22c55e',   // green
    slow:     '#eab308',   // yellow
    moderate: '#f97316',   // orange
    heavy:    '#ef4444',   // red
};

// ── Module state ───────────────────────────────────────────────────────────

/** @type {maplibregl.Map} */
let map = null;

/** @type {'dark'|'light'} */
let currentTheme = 'dark';

let trafficEnabled   = false;
let vehiclesEnabled  = false;
let graphModeEnabled = false;

let trafficData             = null;   // latest /api/traffic response
let routeGeoJSON            = null;   // current route geometry
let selectedRouteIdx        = 0;
let _activeHorizon          = 0;
let _latestVehiclePositions = [];     // cached for theme switches
let _lastExploredPoints     = null;   // cached Dijkstra settled nodes
let _exploredTimer          = null;   // animation handle
let _routeEventsBound       = false;

// ── Init ───────────────────────────────────────────────────────────────────

/**
 * Initialise the map into a container element.
 * @param {string} containerId
 * @param {'dark'|'light'} theme
 * @returns {maplibregl.Map}
 */
export function initMap(containerId, theme = 'light') {
    currentTheme = 'light';
    const styleUrl = STYLE_LIGHT;

    map = new maplibregl.Map({
        container: containerId,
        style: styleUrl,
        center: NOIDA_CENTER,
        zoom: 13,
        maxBounds: [
            [NOIDA_BBOX[0] - 0.1, NOIDA_BBOX[1] - 0.1],
            [NOIDA_BBOX[2] + 0.1, NOIDA_BBOX[3] + 0.1],
        ],
        attributionControl: false,
    });

    // Attribution in bottom-left so bottom-right layer toggles and controls remain clean
    map.addControl(new maplibregl.AttributionControl({
        customAttribution: '© <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors | <a href="https://openfreemap.org">OpenFreeMap</a>',
    }), 'bottom-left');

    map.on('style.load', () => {
        _restoreLayersAndVisuals();
    });

    map.on('styledata', () => {
        if (map && map.style && map.style._loaded) {
            if (!map.getSource('route') || !map.getSource('traffic')) {
                _restoreLayersAndVisuals();
            }
        }
    });

    map.on('load', () => {
        _restoreLayersAndVisuals();
    });

    return map;
}

/** Return the live maplibregl.Map instance */
export function getMap() { return map; }

// ── Theme toggle ───────────────────────────────────────────────────────────

export function setSelectedRoute(idx) {
    selectedRouteIdx = idx;
    if (routeGeoJSON) {
        showRoute(routeGeoJSON, idx);
    }
}

export function setTheme(_theme) {
    // Permanent light theme: map style is stable and never reloaded, preventing crashes
}

// ── Source + layer helpers ─────────────────────────────────────────────────

function safeAddSource(id, spec) {
    if (!map || !map.style || !map.style._loaded) return;
    try {
        if (!map.getSource(id)) map.addSource(id, spec);
    } catch (_) {}
}

function safeAddLayer(spec, before) {
    if (!map || !map.style || !map.style._loaded) return;
    try {
        if (!map.getLayer(spec.id)) {
            if (before && map.getLayer(before)) {
                map.addLayer(spec, before);
            } else {
                map.addLayer(spec);
            }
        }
    } catch (_) {}
}

function _ensureAllLayers() {
    if (!map || !map.style || !map.style._loaded) return false;
    _addTrafficLayer();
    _addGraphLayer();
    _addRouteLayer();
    _addExploredLayer();
    _addVehicleLayer();
    _bindRouteEvents();
    return true;
}

function _restoreLayersAndVisuals() {
    if (!_ensureAllLayers()) return;

    // 1. Re-render active route
    if (routeGeoJSON) {
        _renderRoute(routeGeoJSON, selectedRouteIdx);
    }

    // 2. Re-render Dijkstra explored nodes
    if (_lastExploredPoints && _lastExploredPoints.length > 0) {
        _renderExploredPoints(_lastExploredPoints);
    }

    // 3. Restore traffic layer
    if (trafficEnabled) {
        if (map.getLayer('traffic-lines')) {
            map.setLayoutProperty('traffic-lines', 'visibility', 'visible');
        }
        if (trafficData) {
            _renderTraffic(trafficData);
        } else {
            _refreshTraffic(_activeHorizon);
        }
    }

    // 4. Restore vehicles layer
    if (vehiclesEnabled) {
        if (map.getLayer('vehicle-dots')) {
            map.setLayoutProperty('vehicle-dots', 'visibility', 'visible');
        }
        if (_latestVehiclePositions && _latestVehiclePositions.length > 0) {
            _renderVehicles(_latestVehiclePositions);
        }
    }

    // 5. Restore graph mode
    if (graphModeEnabled) {
        ['graph-edge-lines', 'graph-node-dots'].forEach(id => {
            if (map.getLayer(id)) map.setLayoutProperty(id, 'visibility', 'visible');
        });
        _populateGraphOverlay();
    }
}

function _bindRouteEvents() {
    if (!map || _routeEventsBound) return;
    _routeEventsBound = true;
    map.on('mouseenter', 'route-alt', () => { if (map) map.getCanvas().style.cursor = 'pointer'; });
    map.on('mouseleave', 'route-alt', () => { if (map) map.getCanvas().style.cursor = ''; });
    map.on('click', 'route-alt', (e) => {
        const feat = e.features?.[0];
        if (feat && feat.properties && feat.properties.idx !== undefined) {
            const idx = parseInt(feat.properties.idx, 10);
            window.dispatchEvent(new CustomEvent('sc:select_route', { detail: { idx } }));
        }
    });
}

function _addTrafficLayer() {
    safeAddSource('traffic', { type: 'geojson', data: _emptyFC() });
    safeAddLayer({
        id: 'traffic-lines',
        type: 'line',
        source: 'traffic',
        layout: { 'line-join': 'round', 'line-cap': 'round', visibility: 'none' },
        paint: {
            'line-color': [
                'match', ['get', 'level'],
                'free',     TRAFFIC_COLORS.free,
                'slow',     TRAFFIC_COLORS.slow,
                'moderate', TRAFFIC_COLORS.moderate,
                'heavy',    TRAFFIC_COLORS.heavy,
                '#94a3b8',
            ],
            'line-width': [
                'interpolate', ['linear'], ['zoom'],
                10, 2,
                14, 4.5,
                17, 8,
            ],
            'line-opacity': 0.85,
        },
    });
}

function _addGraphLayer() {
    safeAddSource('graph-edges', { type: 'geojson', data: _emptyFC() });
    safeAddSource('graph-nodes', { type: 'geojson', data: _emptyFC() });
    safeAddLayer({
        id: 'graph-edge-lines',
        type: 'line',
        source: 'graph-edges',
        layout: { visibility: 'none' },
        paint: {
            'line-color': '#0284c7',
            'line-width': 2,
            'line-opacity': 0.8,
            'line-dasharray': [3, 2],
        },
    });
    safeAddLayer({
        id: 'graph-node-dots',
        type: 'circle',
        source: 'graph-nodes',
        layout: { visibility: 'none' },
        paint: {
            'circle-radius': 5,
            'circle-color': '#38bdf8',
            'circle-stroke-width': 1.5,
            'circle-stroke-color': '#ffffff',
            'circle-opacity': 0.95,
        },
    });
}

function _addRouteLayer() {
    safeAddSource('route', { type: 'geojson', data: _emptyFC() });
    // White casing
    safeAddLayer({
        id: 'route-casing',
        type: 'line',
        source: 'route',
        filter: ['==', ['get', 'segment_type'], 'route'],
        layout: { 'line-join': 'round', 'line-cap': 'round' },
        paint: {
            'line-color': '#ffffff',
            'line-width': 10,
            'line-opacity': 0.9,
        },
    });
    // Alternative routes (grey, thinner)
    safeAddLayer({
        id: 'route-alt',
        type: 'line',
        source: 'route',
        filter: ['==', ['get', 'segment_type'], 'alt'],
        layout: { 'line-join': 'round', 'line-cap': 'round' },
        paint: {
            'line-color': '#6b7280',
            'line-width': 4,
            'line-opacity': 0.7,
        },
    });
    // Coloured route line
    safeAddLayer({
        id: 'route-line',
        type: 'line',
        source: 'route',
        filter: ['==', ['get', 'segment_type'], 'route'],
        layout: { 'line-join': 'round', 'line-cap': 'round' },
        paint: {
            'line-color': '#3b82f6',   // blue
            'line-width': 6,
        },
    });
    // Origin/destination markers
    safeAddSource('route-markers', { type: 'geojson', data: _emptyFC() });
    safeAddLayer({
        id:     'route-markers',
        type:   'circle',
        source: 'route-markers',
        paint: {
            'circle-radius': 8,
            'circle-color': ['get', 'color'],
            'circle-stroke-width': 2,
            'circle-stroke-color': '#ffffff',
        },
    });
}

function _addExploredLayer() {
    safeAddSource('explored', { type: 'geojson', data: _emptyFC() });
    safeAddLayer({
        id: 'explored-nodes',
        type: 'circle',
        source: 'explored',
        paint: {
            'circle-radius': 4.5,
            'circle-color': '#a855f7',  // vibrant purple explored dots
            'circle-opacity': 0.85,
            'circle-stroke-width': 1,
            'circle-stroke-color': '#ffffff',
        },
    });
}

function _addVehicleLayer() {
    safeAddSource('vehicles', { type: 'geojson', data: _emptyFC() });
    safeAddLayer({
        id: 'vehicle-dots',
        type: 'circle',
        source: 'vehicles',
        layout: { visibility: 'none' },
        paint: {
            'circle-radius': 4.5,
            'circle-color': '#facc15',
            'circle-stroke-width': 1.5,
            'circle-stroke-color': '#000000',
            'circle-opacity': 0.9,
        },
    });
}

// ── Route display ──────────────────────────────────────────────────────────

function _renderRoute(data, selectedIdx = 0) {
    if (!map || !map.getSource('route')) return;

    const features = [];
    (data.routes || []).forEach((r, i) => {
        if (!r.geometry_geojson || r.geometry_geojson.length < 2) return;
        features.push({
            type: 'Feature',
            geometry: { type: 'LineString', coordinates: r.geometry_geojson },
            properties: {
                segment_type: i === selectedIdx ? 'route' : 'alt',
                label:        r.label,
                duration_s:   r.duration_s,
                idx:          i,
            },
        });
    });

    map.getSource('route').setData({ type: 'FeatureCollection', features });

    // Markers
    if (data.from && data.to && map.getSource('route-markers')) {
        const firstCoord = data.routes[selectedIdx]?.geometry_geojson?.[0];
        const lastCoords = data.routes[selectedIdx]?.geometry_geojson;
        const lastCoord  = lastCoords?.[lastCoords.length - 1];
        const markerFeats = [];
        if (firstCoord) markerFeats.push({
            type: 'Feature',
            geometry: { type: 'Point', coordinates: firstCoord },
            properties: { color: '#22c55e', label: 'A' },
        });
        if (lastCoord) markerFeats.push({
            type: 'Feature',
            geometry: { type: 'Point', coordinates: lastCoord },
            properties: { color: '#ef4444', label: 'B' },
        });
        map.getSource('route-markers').setData({
            type: 'FeatureCollection', features: markerFeats,
        });
    }
}

export function showRoute(data, selectedIdx = 0) {
    routeGeoJSON = data;
    selectedRouteIdx = selectedIdx;
    if (!map) return;
    _ensureAllLayers();
    _renderRoute(data, selectedIdx);

    // Fit map to route bounds
    const coords = data.routes[selectedIdx]?.geometry_geojson;
    if (coords && coords.length > 0) {
        const bounds = coords.reduce(
            (b, c) => b.extend(c),
            new maplibregl.LngLatBounds(coords[0], coords[0])
        );
        map.fitBounds(bounds, { padding: 80 });
    }
}

export function clearRoute() {
    routeGeoJSON = null;
    _lastExploredPoints = null;
    if (_exploredTimer) {
        clearInterval(_exploredTimer);
        _exploredTimer = null;
    }
    if (!map) return;
    ['route', 'route-markers', 'explored'].forEach(id => {
        if (map.getSource(id)) map.getSource(id).setData(_emptyFC());
    });
}

// ── Dijkstra visualizer ────────────────────────────────────────────────────

/**
 * Animate explored nodes (Dijkstra frontier visualizer).
 * @param {Array} points [[lon, lat], ...]
 * @param {number} speed ms multiplier
 */
export function animateExplored(points, speed = 2) {
    if (!points || !points.length) return;
    _lastExploredPoints = points;
    if (_exploredTimer) {
        clearInterval(_exploredTimer);
        _exploredTimer = null;
    }
    _ensureAllLayers();
    if (!map || !map.getSource('explored')) return;

    // Adapt batch size to total points so all points animate smoothly all the way in ~1.5s
    const BATCH = Math.max(80, Math.ceil(points.length / 75));
    let i = 0;

    _exploredTimer = setInterval(() => {
        i += BATCH;
        const slice = points.slice(0, i);
        _renderExploredPoints(slice);
        if (i >= points.length) {
            clearInterval(_exploredTimer);
            _exploredTimer = null;
        }
    }, Math.max(12, speed * 8));
}

function _renderExploredPoints(pts) {
    if (!map || !map.getSource('explored')) return;
    const features = (pts || []).map(pt => ({
        type: 'Feature',
        geometry: { type: 'Point', coordinates: pt },
        properties: {},
    }));
    map.getSource('explored').setData({
        type: 'FeatureCollection', features,
    });
}

// ── Traffic layer ──────────────────────────────────────────────────────────

export function setTrafficVisible(visible) {
    trafficEnabled = visible;
    _ensureAllLayers();
    if (!map) return;
    const viz = visible ? 'visible' : 'none';
    if (map.getLayer('traffic-lines')) map.setLayoutProperty('traffic-lines', 'visibility', viz);
    const legend = document.getElementById('traffic-legend');
    if (legend) legend.classList.toggle('hidden', !visible);
    if (visible) {
        if (trafficData) _renderTraffic(trafficData);
        else _refreshTraffic(_activeHorizon);
    }
}

/** Public: refresh traffic data if the traffic layer is currently enabled */
export function refreshTrafficIfVisible(horizon = _activeHorizon) {
    _activeHorizon = horizon;
    if (trafficEnabled) _refreshTraffic(horizon);
}

async function _refreshTraffic(horizon = _activeHorizon) {
    if (!trafficEnabled || !map) return;
    _ensureAllLayers();
    try {
        const bounds = map.getBounds();
        const bbox = [
            bounds.getWest(), bounds.getSouth(),
            bounds.getEast(), bounds.getNorth(),
        ];
        const zoom  = Math.round(map.getZoom());
        const data  = await getTraffic(bbox, zoom, horizon);
        trafficData = data;
        _renderTraffic(data);
    } catch (e) {
        console.warn('[map] Traffic fetch failed:', e);
    }
}

function _renderTraffic(data) {
    if (!map) return;
    _ensureAllLayers();
    if (!map.getSource('traffic')) return;
    const features = (data.edges || []).map(e => ({
        type: 'Feature',
        geometry: {
            type: 'LineString',
            coordinates: [e.from, e.to],
        },
        properties: {
            id:       e.id,
            level:    e.level,
            speed_kmh: e.speed_kmh,
            weight_s:  e.weight_s,
            name:      e.name,
        },
    }));
    map.getSource('traffic').setData({ type: 'FeatureCollection', features });
}

export function applyTrafficDelta(delta) {
    if (!trafficEnabled || !map || !map.getSource('traffic')) return;
    if (delta.edges) _renderTraffic(delta);
}

// ── Vehicle layer ──────────────────────────────────────────────────────────

export function setVehiclesVisible(visible) {
    vehiclesEnabled = visible;
    _ensureAllLayers();
    if (!map) return;
    const viz = visible ? 'visible' : 'none';
    if (map.getLayer('vehicle-dots')) map.setLayoutProperty('vehicle-dots', 'visibility', viz);
    if (visible && _latestVehiclePositions && _latestVehiclePositions.length > 0) {
        _renderVehicles(_latestVehiclePositions);
    }
}

/** @param {Array} positions [[lon, lat], ...] */
export function updateVehicles(positions) {
    _latestVehiclePositions = positions;
    _ensureAllLayers();
    if (!map) return;
    if (map.getLayer('vehicle-dots')) {
        map.setLayoutProperty('vehicle-dots', 'visibility', vehiclesEnabled ? 'visible' : 'none');
    }
    _renderVehicles(positions);
}

function _renderVehicles(positions) {
    if (!map || !map.getSource('vehicles')) return;
    const features = (positions || []).map(pos => ({
        type: 'Feature',
        geometry: { type: 'Point', coordinates: pos },
        properties: {},
    }));
    map.getSource('vehicles').setData({ type: 'FeatureCollection', features });
}

// ── Graph theory mode ──────────────────────────────────────────────────────

export function setGraphModeVisible(visible) {
    graphModeEnabled = visible;
    _ensureAllLayers();
    if (!map) return;
    const viz = visible ? 'visible' : 'none';
    ['graph-edge-lines', 'graph-node-dots', 'explored-nodes'].forEach(id => {
        if (map.getLayer(id)) map.setLayoutProperty(id, 'visibility', viz);
    });
    if (visible) {
        _populateGraphOverlay();
        if (_lastExploredPoints && _lastExploredPoints.length > 0) {
            _renderExploredPoints(_lastExploredPoints);
        }
    } else {
        if (_exploredTimer) {
            clearInterval(_exploredTimer);
            _exploredTimer = null;
        }
        _lastExploredPoints = null;
        if (map.getSource('explored')) {
            map.getSource('explored').setData(_emptyFC());
        }
    }
}

async function _populateGraphOverlay() {
    if (!map) return;
    _ensureAllLayers();
    if (!trafficData) {
        try {
            const bounds = map.getBounds();
            const bbox = [
                bounds.getWest(), bounds.getSouth(),
                bounds.getEast(), bounds.getNorth(),
            ];
            trafficData = await getTraffic(bbox, Math.round(map.getZoom()));
        } catch (_) {}
    }
    if (!trafficData || !trafficData.edges) return;
    const edgeFeatures = [];
    const nodeFeatures = [];
    const seenNodes = new Set();

    trafficData.edges.forEach(e => {
        if (e.from && e.to) {
            edgeFeatures.push({
                type: 'Feature',
                geometry: { type: 'LineString', coordinates: [e.from, e.to] },
                properties: { id: e.id, weight_s: e.weight_s }
            });
            const k1 = `${e.from[0]},${e.from[1]}`;
            if (!seenNodes.has(k1)) {
                seenNodes.add(k1);
                nodeFeatures.push({
                    type: 'Feature',
                    geometry: { type: 'Point', coordinates: e.from },
                    properties: { id: `n-${e.from[0]}` }
                });
            }
            const k2 = `${e.to[0]},${e.to[1]}`;
            if (!seenNodes.has(k2)) {
                seenNodes.add(k2);
                nodeFeatures.push({
                    type: 'Feature',
                    geometry: { type: 'Point', coordinates: e.to },
                    properties: { id: `n-${e.to[0]}` }
                });
            }
        }
    });

    if (map.getSource('graph-edges')) {
        map.getSource('graph-edges').setData({ type: 'FeatureCollection', features: edgeFeatures });
    }
    if (map.getSource('graph-nodes')) {
        map.getSource('graph-nodes').setData({ type: 'FeatureCollection', features: nodeFeatures });
    }
}

// ── Utility ────────────────────────────────────────────────────────────────

function _emptyFC() {
    return { type: 'FeatureCollection', features: [] };
}

export function flyTo(lon, lat, zoom = 16) {
    if (map) map.flyTo({ center: [lon, lat], zoom, speed: 1.4 });
}

export function addClickHandler(fn) {
    if (map) map.on('click', fn);
}

export function addContextMenu(fn) {
    if (map) map.on('contextmenu', fn);
}

export function getBounds() {
    if (!map) return null;
    const b = map.getBounds();
    return [b.getWest(), b.getSouth(), b.getEast(), b.getNorth()];
}

// ── User location marker ────────────────────────────────────────────────────

let _userMarker = null;

export function setUserMarker(lat, lon, label = 'YOU') {
    if (!map) return;
    if (!_userMarker) {
        const el = document.createElement('div');
        el.className = 'user-vehicle-marker';
        el.innerHTML = `
            <div class="user-vehicle-bubble">
                <span class="user-vehicle-icon">🚗</span>
                <span class="user-vehicle-text">${label}</span>
            </div>
            <div class="user-vehicle-pulse"></div>
        `;
        el.title = 'Your Vehicle (Active in simulation)';
        _userMarker = new maplibregl.Marker({ element: el, anchor: 'bottom' });
        _userMarker.addTo(map);
    } else {
        const textEl = _userMarker.getElement().querySelector('.user-vehicle-text');
        if (textEl && label) textEl.textContent = label;
    }
    _userMarker.setLngLat([lon, lat]);
}

export function removeUserMarker() {
    if (_userMarker) { _userMarker.remove(); _userMarker = null; }
}

// Start periodic traffic refresh when visible
setInterval(() => { if (trafficEnabled) _refreshTraffic(_activeHorizon); }, 4_000);
