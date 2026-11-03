with open('frontend/js/app.js', 'r', encoding='utf-8') as f:
    text = f.read()

target_apply = '''    let _lastVehiclePositions = [];
    function _applyVehicles(rawPositions) {
        if (!rawPositions || rawPositions.length === 0) return;
        _lastVehiclePositions = rawPositions;
        if (horizonMin === 0) {
            Map.updateVehicles(rawPositions);
        } else {
            const shift_deg = horizonMin * 0.00035;
            const projected = rawPositions.map((pos, i) => {
                const angle = ((i * 47) % 360) * (Math.PI / 180);
                return [pos[0] + Math.cos(angle) * shift_deg, pos[1] + Math.sin(angle) * shift_deg];
            });
            Map.updateVehicles(projected);
        }
    }'''

replacement_apply = '''    let _lastVehiclePositions = [];
    async function _applyVehicles(rawPositions) {
        if (!rawPositions || rawPositions.length === 0) return;
        _lastVehiclePositions = rawPositions;
        if (horizonMin === 0) {
            Map.updateVehicles(rawPositions);
        } else {
            try {
                const res = await API.getVehicles(horizonMin);
                if (res && res.positions && res.positions.length > 0) {
                    Map.updateVehicles(res.positions);
                    return;
                }
            } catch (_) {}
            Map.updateVehicles(rawPositions);
        }
    }'''

target_horizon = '''function _wireLeaveTime() {
    document.querySelectorAll('[data-horizon]').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('[data-horizon]').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            horizonMin = parseInt(btn.dataset.horizon, 10);
            _applyVehicles(_lastVehiclePositions);
            Map.refreshTrafficIfVisible(horizonMin);
            // Re-run route with new horizon if we have a current route
            if (routeSlots.from && routeSlots.to) _tryComputeRoute();
        });
    });
}'''

replacement_horizon = '''function _syncDepartureUI(min) {
    let matchedPreset = false;
    document.querySelectorAll('[data-horizon]').forEach(btn => {
        const h = parseInt(btn.dataset.horizon, 10);
        if (h === min) {
            btn.classList.add('active');
            matchedPreset = true;
        } else {
            btn.classList.remove('active');
        }
    });

    const customToggle = document.getElementById('leave-custom-toggle');
    const customWrap = document.getElementById('custom-departure-wrap');
    const slider = document.getElementById('custom-departure-slider');
    const numInput = document.getElementById('custom-departure-input');
    const statusEl = document.getElementById('custom-departure-status');

    if (slider) slider.value = Math.min(min, 60);
    if (numInput) numInput.value = min;

    if (!matchedPreset) {
        if (customToggle) customToggle.classList.add('active');
        if (customWrap) customWrap.classList.remove('hidden');
    }

    if (statusEl) {
        if (min === 0) {
            statusEl.textContent = 'Departing now • Real-time traffic & vehicle positions';
        } else {
            const departTime = new Date(Date.now() + min * 60000).toLocaleTimeString('en-IN', {
                hour: '2-digit', minute: '2-digit'
            });
            statusEl.textContent = `Departing in +${min} min (${departTime}) • Predicted vehicle movement & traffic`;
        }
    }
}

async function _updateHorizon(min) {
    horizonMin = Math.max(0, parseInt(min, 10) || 0);
    _syncDepartureUI(horizonMin);

    // Dynamic vehicle prediction on road network
    if (horizonMin === 0) {
        Map.updateVehicles(_lastVehiclePositions);
    } else {
        try {
            const res = await API.getVehicles(horizonMin);
            if (res && res.positions) {
                Map.updateVehicles(res.positions);
            }
        } catch (_) {}
    }

    // Dynamic traffic line prediction
    Map.refreshTrafficIfVisible(horizonMin);

    // Dynamic route recalculation with arrival duration matching predicted traffic
    if (routeSlots.from && routeSlots.to) {
        _tryComputeRoute();
    }
}

function _wireLeaveTime() {
    document.querySelectorAll('[data-horizon]').forEach(btn => {
        btn.addEventListener('click', () => {
            const h = parseInt(btn.dataset.horizon, 10);
            document.getElementById('custom-departure-wrap')?.classList.add('hidden');
            document.getElementById('leave-custom-toggle')?.classList.remove('active');
            _updateHorizon(h);
        });
    });

    const customToggle = document.getElementById('leave-custom-toggle');
    const customWrap = document.getElementById('custom-departure-wrap');
    const slider = document.getElementById('custom-departure-slider');
    const numInput = document.getElementById('custom-departure-input');

    customToggle?.addEventListener('click', () => {
        const isHidden = customWrap?.classList.toggle('hidden');
        if (!isHidden) {
            document.querySelectorAll('[data-horizon]').forEach(b => b.classList.remove('active'));
            customToggle.classList.add('active');
            const h = parseInt(numInput?.value || slider?.value || '15', 10);
            _updateHorizon(h);
        }
    });

    let debounceTimer = null;
    const onCustomChange = (val) => {
        const h = Math.max(0, parseInt(val, 10) || 0);
        if (slider) slider.value = Math.min(h, 60);
        if (numInput) numInput.value = h;
        clearTimeout(debounceTimer);
        debounceTimer = setTimeout(() => {
            _updateHorizon(h);
        }, 150);
    };

    slider?.addEventListener('input', e => onCustomChange(e.target.value));
    numInput?.addEventListener('input', e => onCustomChange(e.target.value));
}'''

target_map_controls = '''    // Map navigation controls (+, -, compass, my-location)
    document.getElementById('btn-zoom-in')?.addEventListener('click', () => {
        Map.getMap()?.zoomIn({ duration: 300 });
    });
    document.getElementById('btn-zoom-out')?.addEventListener('click', () => {
        Map.getMap()?.zoomOut({ duration: 300 });
    });
    document.getElementById('btn-compass')?.addEventListener('click', () => {
        Map.getMap()?.resetNorthPitch({ duration: 500 });
    });
    document.getElementById('btn-my-loc')?.addEventListener('click', () => {
        if (!navigator.geolocation) {
            alert('Geolocation is not supported by your browser.');
            return;
        }
        navigator.geolocation.getCurrentPosition(
            pos => {
                const { latitude, longitude } = pos.coords;
                Map.flyTo(longitude, latitude, 15);
                Map.setUserMarker(latitude, longitude);
            },
            err => {
                console.warn('[app] Geolocation failed:', err);
                Map.flyTo(77.3910, 28.5355, 13);
            },
            { enableHighAccuracy: true, timeout: 10000 }
        );
    });'''

replacement_map_controls = '''    // Map navigation controls (+, -, compass, my-location)
    const mapObj = Map.getMap();
    document.getElementById('btn-zoom-in')?.addEventListener('click', () => {
        if (Map.getMap()) Map.getMap().zoomIn({ duration: 250 });
    });
    document.getElementById('btn-zoom-out')?.addEventListener('click', () => {
        if (Map.getMap()) Map.getMap().zoomOut({ duration: 250 });
    });
    document.getElementById('btn-compass')?.addEventListener('click', () => {
        if (Map.getMap()) Map.getMap().resetNorthPitch({ duration: 400 });
    });
    document.getElementById('btn-my-loc')?.addEventListener('click', () => {
        if (!navigator.geolocation) {
            Map.flyTo(77.3910, 28.5355, 15);
            Map.setUserMarker(28.5355, 77.3910);
            return;
        }
        navigator.geolocation.getCurrentPosition(
            pos => {
                const { latitude, longitude } = pos.coords;
                Map.flyTo(longitude, latitude, 15);
                Map.setUserMarker(latitude, longitude);
            },
            err => {
                console.warn('[app] Geolocation failed, flying to Noida center:', err);
                Map.flyTo(77.3910, 28.5355, 14);
                Map.setUserMarker(28.5355, 77.3910);
            },
            { enableHighAccuracy: true, timeout: 5000 }
        );
    });'''

def norm(s): return s.replace('\r\n', '\n')

text_norm = norm(text)
target_apply_norm = norm(target_apply)
target_horizon_norm = norm(target_horizon)
target_map_controls_norm = norm(target_map_controls)

assert target_apply_norm in text_norm, 'target_apply not found'
assert target_horizon_norm in text_norm, 'target_horizon not found'
assert target_map_controls_norm in text_norm, 'target_map_controls not found'

text_norm = text_norm.replace(target_apply_norm, norm(replacement_apply))
text_norm = text_norm.replace(target_horizon_norm, norm(replacement_horizon))
text_norm = text_norm.replace(target_map_controls_norm, norm(replacement_map_controls))

with open('frontend/js/app.js', 'w', encoding='utf-8') as f:
    f.write(text_norm)
print('Updated frontend/js/app.js successfully!')

