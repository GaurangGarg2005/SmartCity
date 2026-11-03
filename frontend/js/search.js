// js/search.js — Search bar + autocomplete with keyboard navigation (Phase 3)
import * as API from './api.js';

let _onSelect  = null;
let _biasLat   = 28.5355;
let _biasLon   = 77.3910;

const DEBOUNCE_MS = 280;
const RECENT_KEY  = 'sc_recent_searches';
const MAX_RECENT  = 5;

// Popular locations in Noida shown as initial dropdown options
const POPULAR_NOIDA_PLACES = [
    { id: 'sec-18',   name: 'Sector 18 Noida (Atta Market)',    lat: 28.5708, lon: 77.3260, type: 'mall',     address: 'Commercial Hub, Noida' },
    { id: 'sec-62',   name: 'Sector 62 Noida (Electronic City)',lat: 28.6280, lon: 77.3649, type: 'office',   address: 'IT & Institutional Area, Noida' },
    { id: 'bot-gar',  name: 'Botanical Garden Metro Station',   lat: 28.5642, lon: 77.3342, type: 'transit',  address: 'Sector 38, Noida' },
    { id: 'sec-16',   name: 'Sector 16 Noida (Film City)',      lat: 28.5779, lon: 77.3150, type: 'office',   address: 'Media & Institutional Area, Noida' },
    { id: 'city-ctr', name: 'Noida City Centre',                lat: 28.5747, lon: 77.3560, type: 'transit',  address: 'Sector 39, Noida' },
    { id: 'sec-137',  name: 'Sector 137 Noida (Expressway)',    lat: 28.5132, lon: 77.4080, type: 'landmark', address: 'Noida Expressway' },
];

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
    if (!input || !list) return;

    let debounceTimer = null;

    input.addEventListener('focus', () => {
        _activeField = role;
        if (input.value.trim().length === 0) {
            _showSuggestions(list, role, inputId);
        }
    });

    input.addEventListener('input', () => {
        clearTimeout(debounceTimer);
        const q = input.value.trim();
        if (q.length === 0) {
            _showSuggestions(list, role, inputId);
            return;
        }
        debounceTimer = setTimeout(() => _runSearch(q, list, role, inputId), DEBOUNCE_MS);
    });

    input.addEventListener('keydown', async (e) => {
        if (e.key === 'Escape') {
            _closeList(list);
            input.blur();
        } else if (e.key === 'ArrowDown') {
            e.preventDefault();
            if (list.classList.contains('hidden') || list.children.length === 0) {
                _showSuggestions(list, role, inputId);
            }
            _focusItem(list, 0, inputId);
        } else if (e.key === 'Enter') {
            e.preventDefault();
            const first = list.querySelector('.search-result-item');
            if (first && !list.classList.contains('hidden')) {
                first.click();
            } else {
                const q = input.value.trim();
                if (q) {
                    try {
                        const results = await API.search(q, _biasLat, _biasLon, 1);
                        if (results.length > 0) _pick(results[0], role, list);
                    } catch (_) {}
                }
            }
        }
    });

    // Clear button
    const clearBtn = document.getElementById(inputId + '-clear');
    if (clearBtn) {
        clearBtn.addEventListener('click', () => {
            input.value = "";
            _closeList(list);
            const errEl = document.getElementById("route-error"); if (errEl) { errEl.textContent = ""; errEl.classList.add("hidden"); }
            input.focus();
        });
    }

    // Close on outside click
    document.addEventListener('click', (e) => {
        if (!input.contains(e.target) && !list.contains(e.target)) {
            _closeList(list);
        }
    }, { passive: true });
}

// ── Search ─────────────────────────────────────────────────────────────────

async function _runSearch(q, list, role, inputId) {
    try {
        const results = await API.search(q, _biasLat, _biasLon, 8);
        _renderResults(results, list, role, inputId);
    } catch (e) {
        console.warn('[search] error:', e);
    }
}

function _showSuggestions(list, role, inputId) {
    const recent = _loadRecent();
    const seenIds = new Set(recent.map(r => r.name.toLowerCase()));
    const popularFiltered = POPULAR_NOIDA_PLACES.filter(p => !seenIds.has(p.name.toLowerCase()));
    const items = [...recent, ...popularFiltered].slice(0, 7);
    _renderResults(items, list, role, inputId);
}

function _renderResults(results, list, role, inputId) {
    if (!list) return;
    if (results.length === 0) {
        _closeList(list);
        return;
    }

    list.innerHTML = results.map((r, i) => `
        <li class="search-result-item"
            role="option"
            tabindex="0"
            data-idx="${i}"
            data-lat="${r.lat}"
            data-lon="${r.lon}"
            data-name="${_esc(r.name)}"
            aria-selected="${i === 0 ? 'true' : 'false'}">
            <span class="result-icon">${r.is_recent ? '🕐' : _typeIcon(r.type)}</span>
            <span class="result-body">
                <span class="result-name">${_esc(r.name)}</span>
                ${r.address ? `<span class="result-addr">${_esc(r.address)}</span>` : ''}
            </span>
        </li>
    `).join('');

    const items = list.querySelectorAll('.search-result-item');
    items.forEach((li, i) => {
        li.addEventListener('click', () => _pick(results[i], role, list));
        li.addEventListener('mouseenter', () => {
            items.forEach(el => el.classList.remove('active'));
            li.classList.add('active');
        });
        li.addEventListener('keydown', (e) => {
            if (e.key === 'Enter' || e.key === ' ') {
                e.preventDefault();
                _pick(results[i], role, list);
            } else if (e.key === 'ArrowDown') {
                e.preventDefault();
                if (i + 1 < items.length) {
                    _focusItem(list, i + 1, inputId);
                }
            } else if (e.key === 'ArrowUp') {
                e.preventDefault();
                if (i > 0) {
                    _focusItem(list, i - 1, inputId);
                } else {
                    document.getElementById(inputId)?.focus();
                }
            } else if (e.key === 'Escape') {
                _closeList(list);
                document.getElementById(inputId)?.focus();
            }
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
    window.dispatchEvent(new CustomEvent('sc:recent_updated', { detail: place }));
    const errEl = document.getElementById("route-error"); if (errEl) { errEl.textContent = ""; errEl.classList.add("hidden"); }
    _onSelect?.(place, role);
}

// ── Recent searches ────────────────────────────────────────────────────────

function _saveRecent(place) {
    const toSave = {
        id: place.id || `loc-${place.lat}-${place.lon}`,
        name: place.name,
        lat: place.lat,
        lon: place.lon,
        type: place.type || 'landmark',
        address: place.address || '',
        is_recent: true,
    };
    const arr = _loadRecent().filter(p => p.name !== place.name && p.id !== toSave.id);
    arr.unshift(toSave);
    if (arr.length > MAX_RECENT) arr.length = MAX_RECENT;
    try { localStorage.setItem(RECENT_KEY, JSON.stringify(arr)); } catch (_) {}
}

export function getRecentSearches() {
    return _loadRecent();
}

function _loadRecent() {
    try {
        const arr = JSON.parse(localStorage.getItem(RECENT_KEY) || '[]');
        return arr.map(p => ({ ...p, is_recent: true }));
    } catch (_) {
        return [];
    }
}

// ── Helpers ────────────────────────────────────────────────────────────────

function _closeList(list) {
    if (list) list.classList.add('hidden');
}

function _focusItem(list, idx, inputId) {
    const items = list?.querySelectorAll('.search-result-item');
    if (!items || items.length === 0) return;
    const clamped = Math.max(0, Math.min(items.length - 1, idx));
    items.forEach((it, i) => {
        if (i === clamped) {
            it.classList.add('active');
            it.setAttribute('aria-selected', 'true');
            it.focus();
        } else {
            it.classList.remove('active');
            it.setAttribute('aria-selected', 'false');
        }
    });
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
    return String(s || '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}
