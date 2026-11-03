with open('frontend/index.html', 'r', encoding='utf-8') as f:
    text = f.read()

target1 = '''        <!-- Leave-time selector (Phase 5: horizon prediction) -->
        <div class="leave-time" id="leave-time" aria-label="Leave time">
          <button class="leave-btn active" data-horizon="0" id="leave-now">Now</button>
          <button class="leave-btn" data-horizon="5"  id="leave-5">+5 min</button>
          <button class="leave-btn" data-horizon="10" id="leave-10">+10 min</button>
          <button class="leave-btn" data-horizon="15" id="leave-15">+15 min</button>
          <button class="leave-btn" data-horizon="30" id="leave-30">+30 min</button>
        </div>'''

replacement1 = '''        <!-- Leave-time selector (Real-time dynamic future departure) -->
        <div class="leave-time" id="leave-time" aria-label="Leave time">
          <button class="leave-btn active" data-horizon="0" id="leave-now">Now</button>
          <button class="leave-btn" data-horizon="5"  id="leave-5">+5m</button>
          <button class="leave-btn" data-horizon="10" id="leave-10">+10m</button>
          <button class="leave-btn" data-horizon="15" id="leave-15">+15m</button>
          <button class="leave-btn" data-horizon="30" id="leave-30">+30m</button>
          <button class="leave-btn" id="leave-custom-toggle" title="Choose custom departure time">Custom ⏱</button>
        </div>

        <!-- Custom departure time slider & live projection -->
        <div class="custom-departure hidden" id="custom-departure-wrap">
          <div class="custom-departure__row">
            <label for="custom-departure-slider" class="custom-departure__label">Depart in:</label>
            <input type="range" id="custom-departure-slider" min="0" max="60" value="0" step="1" class="custom-departure__slider" />
            <div class="custom-departure__val-box">
              <input type="number" id="custom-departure-input" min="0" max="120" value="0" class="custom-departure__num" />
              <span class="custom-departure__unit">min</span>
            </div>
          </div>
          <div class="custom-departure__status" id="custom-departure-status">
            Departing now • Live road traffic & vehicle positions
          </div>
        </div>'''

target2 = '''  <!-- Service Worker registration -->
  <script>
    if ('serviceWorker' in navigator) {
      window.addEventListener('load', () => {
        navigator.serviceWorker.register('/sw.js')
          .then(r => console.log('[SW] registered', r.scope))
          .catch(e => console.warn('[SW] registration failed', e));
      });
    }
  </script>'''

replacement2 = '''  <!-- Service Worker registration & localhost cache cleanup -->
  <script>
    if ('serviceWorker' in navigator) {
      if (location.hostname === 'localhost' || location.hostname === '127.0.0.1') {
        navigator.serviceWorker.getRegistrations().then(regs => {
          for (const reg of regs) reg.unregister();
        });
        if ('caches' in window) {
          caches.keys().then(keys => keys.forEach(k => caches.delete(k)));
        }
      } else {
        window.addEventListener('load', () => {
          navigator.serviceWorker.register('/sw.js')
            .catch(e => console.warn('[SW] registration failed', e));
        });
      }
    }
  </script>'''

def norm(s): return s.replace('\r\n', '\n')

text_norm = norm(text)
target1_norm = norm(target1)
target2_norm = norm(target2)

assert target1_norm in text_norm, 'target1 not found'
assert target2_norm in text_norm, 'target2 not found'

text_norm = text_norm.replace(target1_norm, norm(replacement1)).replace(target2_norm, norm(replacement2))

with open('frontend/index.html', 'w', encoding='utf-8') as f:
    f.write(text_norm)
print('Updated frontend/index.html successfully!')

