with open('frontend/js/app.js', 'r', encoding='utf-8') as f:
    text = f.read()

target = '''    document.getElementById('btn-my-loc')?.addEventListener('click', () => {
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

replacement = '''    const _addAndShowMyVehicle = async (lat, lon) => {
        Map.flyTo(lon, lat, 15);
        Map.setUserMarker(lat, lon, 'YOU');
        try {
            const res = await API.addMyVehicle(lat, lon);
            if (res && res.road_name) {
                console.log(`[sim] Your vehicle added on ${res.road_name}`);
            }
        } catch (_) {}
    };

    document.getElementById('btn-my-loc')?.addEventListener('click', () => {
        if (!navigator.geolocation) {
            _addAndShowMyVehicle(28.5355, 77.3910);
            return;
        }
        navigator.geolocation.getCurrentPosition(
            pos => {
                _addAndShowMyVehicle(pos.coords.latitude, pos.coords.longitude);
            },
            err => {
                console.warn('[app] Geolocation denied/unavailable in browser:', err);
                _addAndShowMyVehicle(28.5355, 77.3910);
            },
            { enableHighAccuracy: true, timeout: 4000 }
        );
    });'''

target2 = '''async function _wireLocationSharing() {
    const btn = document.getElementById('btn-share-location');
    if (!btn) return;

    btn.addEventListener('click', async () => {
        if (!navigator.geolocation) {
            Map.flyTo(77.3910, 28.5355, 15);
            Map.setUserMarker(28.5355, 77.3910);
            btn.classList.add('sharing');
            btn.title = 'Location pinned';
            return;
        }
        navigator.geolocation.getCurrentPosition(
            pos => {
                const { latitude, longitude } = pos.coords;
                Map.flyTo(longitude, latitude, 15);
                Map.setUserMarker(latitude, longitude);
                btn.classList.add('sharing');
                btn.title = 'Location pinned';
            },
            err => {
                console.warn('[app] geolocation fallback to Noida center:', err);
                Map.flyTo(77.3910, 28.5355, 14);
                Map.setUserMarker(28.5355, 77.3910);
                btn.classList.add('sharing');
                btn.title = 'Location pinned';
            },
            { enableHighAccuracy: true, timeout: 5000 }
        );
    });
}'''

replacement2 = '''async function _wireLocationSharing() {
    const btn = document.getElementById('btn-share-location');
    if (!btn) return;

    btn.addEventListener('click', async () => {
        const _handleShare = async (lat, lon) => {
            Map.flyTo(lon, lat, 15);
            Map.setUserMarker(lat, lon, 'YOU');
            btn.classList.add('sharing');
            btn.title = 'Your vehicle added to live traffic';
            try {
                await API.addMyVehicle(lat, lon);
            } catch (_) {}
        };

        if (!navigator.geolocation) {
            _handleShare(28.5355, 77.3910);
            return;
        }
        navigator.geolocation.getCurrentPosition(
            pos => {
                _handleShare(pos.coords.latitude, pos.coords.longitude);
            },
            err => {
                console.warn('[app] Geolocation denied/unavailable:', err);
                _handleShare(28.5355, 77.3910);
            },
            { enableHighAccuracy: true, timeout: 4000 }
        );
    });
}'''

def norm(s): return s.replace('\r\n', '\n')
assert norm(target) in norm(text), 'target not found'
assert norm(target2) in norm(text), 'target2 not found'

new_text = norm(text).replace(norm(target), norm(replacement)).replace(norm(target2), norm(replacement2))

with open('frontend/js/app.js', 'w', encoding='utf-8') as f:
    f.write(new_text)
print('Updated app.js with live user vehicle registration!')

