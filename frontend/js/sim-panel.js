// js/sim-panel.js — Simulation control panel (Phase 3/4)

let _onControl  = null;
let _onIncident = null;

export function initSimPanel({ onControl, onIncident }) {
    _onControl  = onControl;
    _onIncident = onIncident;
    _wire();
}

function _wire() {
    // Play / Pause
    document.getElementById('btn-sim-play')?.addEventListener('click', () =>
        _onControl?.('play'));
    document.getElementById('btn-sim-pause')?.addEventListener('click', () =>
        _onControl?.('pause'));
    document.getElementById('btn-sim-reset')?.addEventListener('click', () =>
        _onControl?.('reset'));

    // Speed slider
    const spdInput = document.getElementById('sim-speed');
    spdInput?.addEventListener('input', () => {
        const v = parseFloat(spdInput.value);
        document.getElementById('sim-speed-label').textContent = `${v}×`;
        _onControl?.('speed', { multiplier: v });
    });

    // Vehicle count
    const vehInput = document.getElementById('sim-vehicles');
    vehInput?.addEventListener('change', () => {
        const n = parseInt(vehInput.value, 10);
        _onControl?.('set_vehicles', { n });
    });

    // Scenario
    const scSelect = document.getElementById('sim-scenario');
    scSelect?.addEventListener('change', () => {
        _onControl?.('set_scenario', { scenario: scSelect.value });
    });

    // Quick demo buttons
    document.querySelectorAll('[data-demo]').forEach(btn => {
        btn.addEventListener('click', () => {
            const demo = btn.dataset.demo;
            if (demo === 'rush_hour') {
                _onControl?.('set_scenario', { scenario: 'rush_hour' });
                _onControl?.('set_vehicles',  { n: 1500 });
            } else if (demo === 'accident') {
                // Create a random incident (lat/lon in Noida bbox)
                _onIncident?.({
                    lat: 28.55 + Math.random() * 0.06,
                    lon: 77.34 + Math.random() * 0.12,
                    type: 'block', factor: 0.05, duration_min: 20,
                });
            } else if (demo === 'rain') {
                _onControl?.('set_scenario', { scenario: 'moderate' });
            }
        });
    });
}
