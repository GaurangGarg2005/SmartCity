// js/theme.js — Dark/light theme management (Phase 3)

const THEME_KEY   = 'sc_theme';
const DARK_CLASS  = 'dark-theme';
const LIGHT_CLASS = 'light-theme';

let _current = 'dark';

export function initTheme() {
    const saved = localStorage.getItem(THEME_KEY);
    const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    _current = saved || (prefersDark ? 'dark' : 'light');
    _apply(_current);
    return _current;
}

export function setTheme(t) {
    _current = t;
    _apply(t);
    localStorage.setItem(THEME_KEY, t);
}

export function isDark() { return _current === 'dark'; }

function _apply(t) {
    document.body.classList.toggle(DARK_CLASS,  t === 'dark');
    document.body.classList.toggle(LIGHT_CLASS, t === 'light');
    document.querySelector('meta[name="theme-color"]')
        ?.setAttribute('content', t === 'dark' ? '#0f172a' : '#ffffff');
}
