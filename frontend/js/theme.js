// js/theme.js — Permanent clean light theme
const THEME_KEY = 'sc_theme';
const LIGHT_CLASS = 'light-theme';

export function initTheme() {
    _apply();
    return 'light';
}

export function setTheme(_t) {
    _apply();
}

export function isDark() { return false; }

function _apply() {
    document.documentElement.setAttribute('data-theme', 'light');
    document.body.setAttribute('data-theme', 'light');
    document.body.classList.remove('dark-theme');
    document.body.classList.add(LIGHT_CLASS);
    document.documentElement.classList.remove('dark-theme');
    document.documentElement.classList.add(LIGHT_CLASS);
    const metaTheme = document.querySelector('meta[name="theme-color"]');
    if (metaTheme) metaTheme.setAttribute('content', '#ffffff');
}
