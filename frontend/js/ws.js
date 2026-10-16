// js/ws.js — WebSocket client (Phase 3)
// Handles reconnect with exponential backoff, heartbeat, message dispatch.

const WS_BASE = (() => {
    const { protocol, hostname, port } = location;
    const wsProto = protocol === 'https:' ? 'wss:' : 'ws:';
    return `${wsProto}//${hostname}:${port}`;
})();

const PING_INTERVAL_MS     = 20_000;  // client-side ping every 20 s
const RECONNECT_BASE_MS    = 1_500;
const RECONNECT_MAX_MS     = 30_000;
const MAX_RECONNECT_SILENT = 30;      // give up showing errors after this many

/** @type {Map<string, Array<Function>>} */
const listeners = new Map();

let socket    = null;
let pingTimer = null;
let reconnectTimer   = null;
let reconnectDelay   = RECONNECT_BASE_MS;
let reconnectCount   = 0;
let intentionalClose = false;

// ── Public API ─────────────────────────────────────────────────────────────

export function connect() {
    intentionalClose = false;
    _open();
}

export function disconnect() {
    intentionalClose = true;
    clearTimeout(reconnectTimer);
    clearInterval(pingTimer);
    if (socket) socket.close(1000, 'client disconnect');
    socket = null;
}

/** Subscribe to a message type. Multiple handlers allowed. */
export function on(type, fn) {
    if (!listeners.has(type)) listeners.set(type, []);
    listeners.get(type).push(fn);
}

export function off(type, fn) {
    if (!listeners.has(type)) return;
    const arr = listeners.get(type).filter(f => f !== fn);
    listeners.set(type, arr);
}

/** Send a message. Queues until connected. */
export function send(msg) {
    const payload = typeof msg === 'string' ? msg : JSON.stringify(msg);
    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(payload);
    } else {
        // Queue for next connection
        _pending.push(payload);
    }
}

export function isConnected() {
    return socket && socket.readyState === WebSocket.OPEN;
}

// Pending queue for messages sent before socket is open
const _pending = [];

// ── Internal ───────────────────────────────────────────────────────────────

function _open() {
    if (socket && socket.readyState !== WebSocket.CLOSED) return;
    try {
        socket = new WebSocket(`${WS_BASE}/ws`);
    } catch (e) {
        _scheduleReconnect();
        return;
    }

    socket.onopen = () => {
        reconnectDelay = RECONNECT_BASE_MS;
        reconnectCount = 0;
        _emit('_connected', {});

        // Flush pending
        while (_pending.length > 0) {
            const msg = _pending.shift();
            socket.send(msg);
        }

        // Heartbeat
        clearInterval(pingTimer);
        pingTimer = setInterval(() => {
            if (socket?.readyState === WebSocket.OPEN) {
                socket.send(JSON.stringify({ type: 'ping', ts: Date.now() }));
            }
        }, PING_INTERVAL_MS);
    };

    socket.onmessage = ({ data }) => {
        let msg;
        try { msg = JSON.parse(data); } catch (_) { return; }
        _emit(msg.type, msg);
        _emit('*', msg);
    };

    socket.onclose = (e) => {
        clearInterval(pingTimer);
        _emit('_disconnected', { code: e.code, reason: e.reason });
        if (!intentionalClose) _scheduleReconnect();
    };

    socket.onerror = () => {
        // onclose fires next; just emit
        _emit('_error', {});
    };
}

function _scheduleReconnect() {
    if (intentionalClose) return;
    reconnectCount++;
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(() => {
        _open();
    }, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 1.5, RECONNECT_MAX_MS);
}

function _emit(type, msg) {
    const fns = listeners.get(type) || [];
    for (const fn of fns) {
        try { fn(msg); } catch (e) { console.error('[ws] handler error', e); }
    }
}
