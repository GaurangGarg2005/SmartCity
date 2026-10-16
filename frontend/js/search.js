// js/search.js — Search bar + autocomplete (Phase 3)
import * as API from './api.js';

let _onSelect  = null;
let _biasLat   = 28.5355;
let _biasLon   = 77.3910;

const DEBOUNCE_MS = 280;
const RECENT_KEY  = 'sc_recent_searches';
const MAX_RECENT  = 6;

// Current active field: 'from' | 'to'
let _activeField = 'to';

// ── Init ───────────────────────────────────────────────────────────────────

export function initSearch({ onSelect }) {
    _onSelect = onSelect;
    _wireField('search-from', 'from');
    _wireField('search-to',   'to');
}

export function setSearchBias(lat, lon) {
    _biasLat = lat;
    _biasLon = lon;
}

// ── Wire a search input ────────────────────────────────────────────────────

function _wireField(inputId, role) {
    const input = document.getElementById(inputId);
    const list  = document.getElementById(inputId + '-results');
    if (!input) return;

    let debounceTimer = null;

    input.addEventListener('focus', () => {
        _activeField = role;
        if (input.value.trim().length === 0) _showRecent(list, role);
    });

    input.addEventListener('input', () => {
        clearTimeout(debounceTimer);
        const q = input.value.trim();
        if (q.length === 0) { _showRecent(list, role); return; }
        debounceTimer = setTimeout(() => _runSearch(q, list, role), DEBOUNCE_MS);
    });

    input.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') { _closeList(list); input.blur(); }
        if (e.key === 'ArrowDown') { e.preventDefault(); _focusItem(list, 0); }
    });

    // Clear button
    const clearBtn = document.getElementById(inputId + '-clear');
    if (clearBtn) {
        clearBtn.addEventListener('click', () => {
            input.value = '';
            _closeList(list);
        });
    }

    // Close on outside click
    document.addEventListener('click', (e) => {
        if (!input.contains(e.target) && !list?.contains(e.target)) _closeList(list);
    }, { passive: true });
}

// ── Search ─────────────────────────────────────────────────────────────────

async function _runSearch(q, list, role) {
    try {
        const results = await API.search(q, _biasLat, _biasLon, 8);
        _renderResults(results, list, role);
    } catch (e) {
        console.warn('[search] error:', e);
    }
}

function _renderResults(results, list, role) {
    if (!list) return;
    if (results.length === 0) { _closeList(list); return; }

    list.innerHTML = results.map((r, i) => `
        <li class="search-result-item"
            role="option"
            tabindex="0"
            data-idx="${i}"
            data-lat="${r.lat}"
            data-lon="${r.lon}"
            data-name="${_esc(r.name)}">
            <span class="result-icon">${_typeIcon(r.type)}</span>
            <span class="result-body">
                <span class="result-name">${_esc(r.name)}</span>
                ${r.address ? `<span class="result-addr">${_esc(r.address)}</span>` : ''}
            </span>
        </li>
    `).join('');

    list.querySelectorAll('.search-result-item').forEach((li, i) => {
        li.addEventListener('click',   () => _pick(results[i], role, list));
        li.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') _pick(results[i], role, list);
            if (e.key === 'ArrowDown') _focusItem(list, i + 1);
            if (e.key === 'ArrowUp')   _focusItem(list, i - 1);
        });
    });

    list.classList.remove('hidden');
}

function _pick(place, role, list) {
    _closeList(list);
    const inputId = role === 'from' ? 'search-from' : 'search-to';
    const input   = document.getElementById(inputId);
    if (input) input.value = place.name;
    _saveRecent(place);
    _onSelect?.(place, role);
}

// ── Recent searches ────────────────────────────────────────────────────────

function _showRecent(list, role) {
    if (!list) return;
    const recent = _loadRecent();
    if (recent.length === 0) { list.innerHTML = ''; list.classList.add('hidden'); return; }
    _renderResults(recent, list, role);
}

function _saveRecent(place) {
    const arr = _loadRecent().filter(p => p.id !== place.id);
    arr.unshift(place);
    if (arr.length > MAX_RECENT) arr.length = MAX_RECENT;
    try { localStorage.setItem(RECENT_KEY, JSON.stringify(arr)); } catch (_) {}
}

function _loadRecent() {
    try { return JSON.parse(localStorage.getItem(RECENT_KEY) || '[]'); } catch (_) { return []; }
}

// ── Helpers ────────────────────────────────────────────────────────────────

function _closeList(list) { if (list) list.classList.add('hidden'); }

function _focusItem(list, idx) {
    const items = list?.querySelectorAll('.search-result-item');
    if (!items) return;
    const clamped = Math.max(0, Math.min(items.length - 1, idx));
    items[clamped]?.focus();
}

function _typeIcon(type) {
    const icons = {
        transit: '🚇', education: '🎓', hospital: '🏥',
        mall: '🏬', park: '🌳', road: '🛣️', landmark: '📍',
        office: '🏢',
    };
    return icons[type] || '📍';
}

function _esc(s) {
    return String(s)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}
