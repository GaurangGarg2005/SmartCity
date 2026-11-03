with open('frontend/css/app.css', 'r', encoding='utf-8') as f:
    text = f.read()

target = '''/* ── User location dot (Phase 7) ─────────────────────────────────── */
.user-location-dot {
  width: 18px;
  height: 18px;
  background: hsl(220, 90%, 55%);
  border: 3px solid #fff;
  border-radius: 50%;
  box-shadow: 0 0 0 4px hsl(220 90% 55% / 0.25);
  animation: user-dot-pulse 2s ease-out infinite;
}
@keyframes user-dot-pulse {
  0%   { box-shadow: 0 0 0 0   hsl(220 90% 55% / 0.4); }
  70%  { box-shadow: 0 0 0 12px hsl(220 90% 55% / 0); }
  100% { box-shadow: 0 0 0 0   hsl(220 90% 55% / 0); }
}

#btn-share-location.sharing { background: hsl(220 90% 55% / 0.15); border-color: var(--clr-brand); }'''

replacement = '''/* ── User location & vehicle marker (Phase 7) ───────────────────── */
.user-location-dot {
  width: 18px;
  height: 18px;
  background: hsl(220, 90%, 55%);
  border: 3px solid #fff;
  border-radius: 50%;
  box-shadow: 0 0 0 4px hsl(220 90% 55% / 0.25);
  animation: user-dot-pulse 2s ease-out infinite;
}

.user-vehicle-marker {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  cursor: pointer;
  z-index: 60;
}
.user-vehicle-bubble {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  background: #1d4ed8;
  color: #ffffff;
  padding: 3px 8px;
  border-radius: 14px;
  font-weight: 800;
  font-size: 11px;
  letter-spacing: 0.5px;
  box-shadow: 0 4px 14px rgba(29, 78, 216, 0.5);
  border: 2px solid #ffffff;
  animation: vehicle-bounce 1.2s ease-in-out infinite alternate;
  white-space: nowrap;
}
.user-vehicle-icon {
  font-size: 14px;
}
.user-vehicle-pulse {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: rgba(37, 99, 235, 0.4);
  margin-top: -6px;
  animation: user-dot-pulse 2s ease-out infinite;
}
@keyframes vehicle-bounce {
  0%   { transform: translateY(0); }
  100% { transform: translateY(-5px); }
}

@keyframes user-dot-pulse {
  0%   { box-shadow: 0 0 0 0   hsl(220 90% 55% / 0.5); }
  70%  { box-shadow: 0 0 0 14px hsl(220 90% 55% / 0); }
  100% { box-shadow: 0 0 0 0   hsl(220 90% 55% / 0); }
}

#btn-share-location.sharing { background: hsl(220 90% 55% / 0.15); border-color: var(--clr-brand); }'''

def norm(s): return s.replace('\r\n', '\n')
assert norm(target) in norm(text), 'target not found'
new_text = norm(text).replace(norm(target), norm(replacement))
with open('frontend/css/app.css', 'w', encoding='utf-8') as f:
    f.write(new_text)
print('Updated app.css successfully!')

