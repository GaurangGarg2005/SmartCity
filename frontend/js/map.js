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

let trafficData      = null;   // latest /api/traffic response
let routeGeoJSON     = null;   // current route geometry

// ── Init ───────────────────────────────────────────────────────────────────

/**
 * Initialise the map into a container element.
 * @param {string} containerId
 * @param {'dark'|'light'} theme
 * @returns {maplibregl.Map}
 */
export function initMap(containerId, theme = 'dark') {
    currentTheme = theme;
    const styleUrl = theme === 'dark' ? STYLE_DARK : STYLE_LIGHT;

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

    // Attribution (required by OSM, OpenFreeMap, MapLibre)
    map.addControl(new maplibregl.AttributionControl({
        customAttribution: '© <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors | <a href="https://openfreemap.org">OpenFreeMap</a>',
    }), 'bottom-right');

    map.addControl(new maplibregl.NavigationControl({ showCompass: false }), 'bottom-right');
    map.addControl(new maplibregl.GeolocateControl({
        positionOptions: { enableHighAccuracy: true },
        trackUserLocation: true,
        showUserHeading: true,
    }), 'bottom-right');

    map.on('load', () => {
        _addRouteLayer();
        _addExploredLayer();
        _addTrafficLayer();
        _addVehicleLayer();
        _addGraphLayer();
    });

    return map;
}

/** Return the live maplibregl.Map instance */
export function getMap() { return map; }

// ── Theme toggle ───────────────────────────────────────────────────────────

export function setTheme(theme) {
    currentTheme = theme;
    const styleUrl = theme === 'dark' ? STYLE_DARK : STYLE_LIGHT;
    if (map) {
        map.setStyle(styleUrl);
        map.once('styledata', () => {
            _addRouteLayer();
            _addExploredLayer();
            _addTrafficLayer();
            _addVehicleLayer();
            _addGraphLayer();
            if (routeGeoJSON) showRoute(routeGeoJSON);
        });
    }
}

// ── Source + layer helpers ─────────────────────────────────────────────────

function safeAddSource(id, spec) {
    if (!map.getSource(id)) map.addSource(id, spec);
}

function safeAddLayer(spec, before) {
    if (!map.getLayer(spec.id)) {
        try { map.addLayer(spec, before); } catch (_) {}
    }
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
            'circle-radius': 3,
            'circle-color': '#7c3aed',   // purple for Dijkstra frontier
            'circle-opacity': 0.6,
        },
    }, 'route-casing');
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
                10, 1.5,
                14, 4,
                17, 8,
            ],
            'line-opacity': 0.8,
        },
    }, 'route-casing');
}

function _addVehicleLayer() {
    safeAddSource('vehicles', { type: 'geojson', data: _emptyFC() });
    safeAddLayer({
        id: 'vehicle-dots',
        type: 'circle',
        source: 'vehicles',
        layout: { visibility: 'none' },
        paint: {
            'circle-radius': 4,
            'circle-color': '#facc15',
            'circle-stroke-width': 1,
            'circle-stroke-color': '#000',
            'circle-opacity': 0.85,
        },
    });
}

function _addGraphLayer() {
    safeAddSource('graph-nodes', { type: 'geojson', data: _emptyFC() });
    safeAddSource('graph-edges', { type: 'geojson', data: _emptyFC() });
    safeAddLayer({
        id: 'graph-edge-lines',
        type: 'line',
        source: 'graph-edges',
        layout: { visibility: 'none' },
        paint: {
            'line-color': '#64748b',
            'line-width': 1,
            'line-opacity': 0.7,
        },
    });
    safeAddLayer({
        id: 'graph-node-dots',
        type: 'circle',
        source: 'graph-nodes',
        layout: { visibility: 'none' },
        paint: {
            'circle-radius': 4,
            'circle-color': '#38bdf8',
            'circle-stroke-width': 1,
            'circle-stroke-color': '#0f172a',
        },
    });
}

// ── Route display ──────────────────────────────────────────────────────────

/**
 * Show a route from POST /api/route response.
 * @param {{ routes: Array, explored?: Array }} data
 * @param {number} selectedIdx which route to highlight
 */
export function showRoute(data, selectedIdx = 0) {
    routeGeoJSON = data;
    if (!map || !map.getSource('route')) return;

    const features = [];

    data.routes.forEach((r, i) => {
        if (!r.geometry_geojson || r.geometry_geojson.length < 2) return;
        features.push({
            type: 'Feature',
            geometry: { type: 'LineString', coordinates: r.geometry_geojson },
            properties: {
                segment_type: i === selectedIdx ? 'route' : 'alt',
                label:        r.label,
                duration_s:   r.duration_s,
            },
        });
    });

    map.getSource('route').setData({ type: 'FeatureCollection', features });

    // Fit map to route bounds
    const coords = data.routes[selectedIdx]?.geometry_geojson;
    if (coords && coords.length > 0) {
        const bounds = coords.reduce(
            (b, c) => b.extend(c),
            new maplibregl.LngLatBounds(coords[0], coords[0])
        );
        map.fitBounds(bounds, { padding: 80 });
    }

    // Markers
    if (data.from && data.to) {
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

export function clearRoute() {
    routeGeoJSON = null;
    if (!map) return;
    ['route', 'route-markers', 'explored'].forEach(id => {
        if (map.getSource(id)) map.getSource(id).setData(_emptyFC());
    });
}

// ── Dijkstra visualizer ────────────────────────────────────────────────────

/**
 * Animate explored nodes (Dijkstra frontier visualizer).
 * @param {Array} points [[lon, lat], ...]
 * @param {number} speed ms per batch
 */
export function animateExplored(points, speed = 2) {
    if (!map || !map.getSource('explored')) return;
    const BATCH = 50;
    let i = 0;
    const features = [];

    const step = () => {
        const end = Math.min(i + BATCH, points.length);
        for (; i < end; i++) {
            features.push({
                type: 'Feature',
                geometry: { type: 'Point', coordinates: points[i] },
                properties: { order: i },
            });
        }
        map.getSource('explored').setData({ type: 'FeatureCollection', features: [...features] });
        if (i < points.length) setTimeout(step, speed);
    };
    step();
}

// ── Traffic layer ──────────────────────────────────────────────────────────

export function setTrafficVisible(visible) {
    trafficEnabled = visible;
    if (!map) return;
    const viz = visible ? 'visible' : 'none';
    if (map.getLayer('traffic-lines')) map.setLayoutProperty('traffic-lines', 'visibility', viz);
    if (visible) _refreshTraffic();
}

async function _refreshTraffic() {
    if (!trafficEnabled || !map) return;
    try {
        const bounds = map.getBounds();
        const bbox = [
            bounds.getWest(), bounds.getSouth(),
            bounds.getEast(), bounds.getNorth(),
        ];
        const zoom  = Math.round(map.getZoom());
        const data  = await getTraffic(bbox, zoom);
        trafficData = data;
        _renderTraffic(data);
    } catch (e) {
        console.warn('[map] Traffic fetch failed:', e);
    }
}

function _renderTraffic(data) {
    if (!map || !map.getSource('traffic')) return;
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

/** Called by WsHub when a traffic_delta arrives — updates only changed edges */
export function applyTrafficDelta(delta) {
    if (!trafficEnabled || !map || !map.getSource('traffic')) return;
    // Rebuild from full response or merge delta (Phase 4: incremental merge)
    if (delta.edges) _renderTraffic(delta);
}

// ── Vehicle layer ──────────────────────────────────────────────────────────

export function setVehiclesVisible(visible) {
    vehiclesEnabled = visible;
    if (!map) return;
    const viz = visible ? 'visible' : 'none';
    if (map.getLayer('vehicle-dots')) map.setLayoutProperty('vehicle-dots', 'visibility', viz);
}

/** @param {Array} positions [[lon, lat], ...] */
export function updateVehicles(positions) {
    if (!map || !map.getSource('vehicles')) return;
    const features = positions.map(pos => ({
        type: 'Feature',
        geometry: { type: 'Point', coordinates: pos },
        properties: {},
    }));
    map.getSource('vehicles').setData({ type: 'FeatureCollection', features });
}

// ── Graph theory mode ──────────────────────────────────────────────────────

export function setGraphModeVisible(visible) {
    graphModeEnabled = visible;
    if (!map) return;
    const viz = visible ? 'visible' : 'none';
    ['graph-edge-lines', 'graph-node-dots'].forEach(id => {
        if (map.getLayer(id)) map.setLayoutProperty(id, 'visibility', viz);
    });
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

export function setUserMarker(lat, lon) {
    if (!map) return;
    if (!_userMarker) {
        const el = document.createElement('div');
        el.className = 'user-location-dot';
        el.title = 'Your location';
        _userMarker = new maplibregl.Marker({ element: el, anchor: 'center' });
        _userMarker.addTo(map);
    }
    _userMarker.setLngLat([lon, lat]);
}

export function removeUserMarker() {
    if (_userMarker) { _userMarker.remove(); _userMarker = null; }
}

// Start periodic traffic refresh when visible (every 30 s)
setInterval(() => { if (trafficEnabled) _refreshTraffic(); }, 30_000);
