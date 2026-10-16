// js/api.js — SmartCity API client module (Phase 3)
// All fetch calls go through this module for consistent error handling.

const BASE = (() => {
    // Auto-detect: local dev vs production
    const { protocol, hostname, port } = location;
    if (hostname === 'localhost' || hostname === '127.0.0.1') {
        return `${protocol}//${hostname}:${port}`;
    }
    return ''; // same origin in production
})();

/** @type {string} Current data-source label from the server */
export let dataSourceLabel = 'SIMULATED LIVE';

// ── Generic helpers ────────────────────────────────────────────────────────

async function apiFetch(path, opts = {}) {
    const url = BASE + path;
    const res = await fetch(url, {
        ...opts,
        headers: { 'Content-Type': 'application/json', ...(opts.headers || {}) },
    });
    if (!res.ok) {
        const err = await res.json().catch(() => ({ error: res.statusText }));
        throw Object.assign(new Error(err.error || res.statusText), { status: res.status });
    }
    return res.json();
}

// ── Health ─────────────────────────────────────────────────────────────────

/** Poll /healthz until 200, exponential backoff, max 30 attempts */
export async function waitForServer(onAttempt = () => {}) {
    let delay = 2000;
    for (let i = 1; i <= 30; i++) {
        try {
            const r = await fetch(BASE + '/healthz');
            if (r.ok) return true;
        } catch (_) {}
        onAttempt(i, 30);
        await new Promise(r => setTimeout(r, delay));
        delay = Math.min(delay * 1.4, 15000);
    }
    return false;
}

// ── Meta ───────────────────────────────────────────────────────────────────

export async function getMeta() {
    const data = await apiFetch('/api/meta');
    if (data.data_source) dataSourceLabel = data.data_source;
    return data;
}

export async function getGraphStats() {
    return apiFetch('/api/graph/stats');
}

// ── Search ─────────────────────────────────────────────────────────────────

/**
 * @param {string} q
 * @param {number} lat bias latitude
 * @param {number} lon bias longitude
 * @param {number} limit max results
 * @returns {Promise<Array>}
 */
export async function search(q, lat, lon, limit = 8) {
    const params = new URLSearchParams({ q, lat, lon, limit });
    const data = await apiFetch(`/api/search?${params}`);
    return data.results || [];
}

// ── Reverse geocode ────────────────────────────────────────────────────────

export async function reverse(lat, lon) {
    const params = new URLSearchParams({ lat, lon });
    return apiFetch(`/api/reverse?${params}`);
}

// ── Route ──────────────────────────────────────────────────────────────────

/**
 * @param {{lat:number,lon:number}|number} from
 * @param {{lat:number,lon:number}|number} to
 * @param {{ mode?, alternatives?, wantExplored?, horizonMin? }} opts
 */
export async function route(from, to, opts = {}) {
    return apiFetch('/api/route', {
        method: 'POST',
        body: JSON.stringify({
            from,
            to,
            mode:          opts.mode          ?? 'traffic',
            alternatives:  opts.alternatives  ?? 0,
            want_explored: opts.wantExplored  ?? false,
            horizon_min:   opts.horizonMin    ?? 0,
        }),
    });
}

// ── Traffic ────────────────────────────────────────────────────────────────

/** @param {[number,number,number,number]} bbox [west,south,east,north] */
export async function getTraffic(bbox, zoom = 14) {
    const params = new URLSearchParams({ bbox: bbox.join(','), zoom });
    return apiFetch(`/api/traffic?${params}`);
}

// ── Sim control ────────────────────────────────────────────────────────────

export async function simControl(action, extra = {}) {
    return apiFetch('/api/sim/control', {
        method: 'POST',
        body: JSON.stringify({ action, ...extra }),
    });
}

export async function simIncident(opts) {
    return apiFetch('/api/sim/incident', {
        method: 'POST',
        body: JSON.stringify(opts),
    });
}
