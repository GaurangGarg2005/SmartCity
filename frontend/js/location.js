// location.js — Opt-in GPS location sharing (Phase 7)
//
// When the user explicitly clicks "Share my location", this module:
//   1. Asks for browser Geolocation permission.
//   2. Watches position and sends share_location WS messages at most
//      once per 5 s (rate-limited to comply with server RATE_LIMIT_WS=10).
//   3. Validates the position is within the service area (Noida/Greater Noida bbox).
//   4. Shows a blinking blue dot on the map at the user's current position.
//   5. Provides a "stop sharing" function.
//
// Privacy notice is shown before first share; consent flag is stored in
// sessionStorage only (never persisted across sessions per SPEC privacy rules).

import { sendRaw } from './ws.js';
import * as Map from './map.js';

const SERVICE_BBOX = { minLat: 28.3, maxLat: 28.7, minLon: 77.2, maxLon: 77.7 };
const SHARE_INTERVAL_MS = 5000;    // minimum interval between WS messages
const ACCURACY_THRESHOLD_M = 100; // ignore readings worse than 100m

let _watchId   = null;
let _lastSent  = 0;
let _sharing   = false;
let _userMarker = null;

// ── Public API ─────────────────────────────────────────────────────────────

/** Returns true if currently sharing location. */
export function isSharing() { return _sharing; }

/**
 * Prompt the user, get permission, and start sharing.
 * Returns a Promise that resolves when sharing starts, or rejects on denial.
 */
export async function startSharing() {
    if (_sharing) return;

    // Show privacy notice (inline consent — no cookie)
    const ok = confirm(
        'SmartCity would like to use your current location to improve ' +
        'real-time traffic estimates for Noida/Greater Noida.\n\n' +
        '• Your location is sent only while sharing is active.\n' +
        '• It is never stored permanently.\n' +
        '• You can stop at any time.\n\n' +
        'Allow location sharing?'
    );
    if (!ok) throw new Error('User declined location sharing');

    // Check Geolocation API
    if (!navigator.geolocation) throw new Error('Geolocation not supported');

    return new Promise((resolve, reject) => {
        _watchId = navigator.geolocation.watchPosition(
            pos => {
                _sharing = true;
                _onPosition(pos);
                resolve();
            },
            err => {
                _sharing = false;
                reject(err);
            },
            { enableHighAccuracy: true, maximumAge: 10000, timeout: 15000 }
        );
    });
}

/** Stop sharing. */
export function stopSharing() {
    if (_watchId !== null) {
        navigator.geolocation.clearWatch(_watchId);
        _watchId = null;
    }
    _sharing = false;
    if (_userMarker) {
        Map.removeUserMarker();
        _userMarker = null;
    }
}

// ── Internal ───────────────────────────────────────────────────────────────

function _onPosition(pos) {
    const { latitude: lat, longitude: lon, accuracy, speed } = pos.coords;

    // Ignore poor readings
    if (accuracy > ACCURACY_THRESHOLD_M) return;

    // Service area check
    if (lat < SERVICE_BBOX.minLat || lat > SERVICE_BBOX.maxLat ||
        lon < SERVICE_BBOX.minLon || lon > SERVICE_BBOX.maxLon) return;

    // Rate limiting
    const now = Date.now();
    if (now - _lastSent < SHARE_INTERVAL_MS) return;
    _lastSent = now;

    // Show on map
    Map.setUserMarker(lat, lon);

    // Send via WS
    sendRaw(JSON.stringify({
        type:    'share_location',
        consent: true,
        lat,
        lon,
        accuracy,
        speed: speed ?? 0,
        ts:   now,
    }));
}
