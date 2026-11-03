/**
 * SmartCity Service Worker
 * Network-first strategy with cache fallback.
 */

const CACHE_NAME = 'smartcity-v2';

const APP_SHELL = [
  '/',
  '/index.html',
  '/evaluation.html',
  '/privacy.html',
  '/css/tokens.css',
  '/css/app.css',
  '/css/panel.css',
  '/css/sheet.css',
  '/css/search.css',
  '/css/route.css',
  '/css/traffic.css',
  '/css/graph-mode.css',
  '/css/nav.css',
  '/js/app.js',
  '/js/map.js',
  '/js/ws.js',
  '/js/search.js',
  '/js/route.js',
  '/js/sim-panel.js',
  '/js/theme.js',
  '/js/sheet.js',
  '/vendor/maplibre-gl.js',
  '/vendor/maplibre-gl.css',
  '/manifest.webmanifest',
  '/offline.html',
];

self.addEventListener('install', event => {
  self.skipWaiting();
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(keys =>
      Promise.all(keys.map(k => caches.delete(k)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', event => {
  const { request } = event;
  // Always fetch network first so browser immediately receives updated assets
  event.respondWith(
    fetch(request)
      .then(response => {
        if (request.method === 'GET' && response.status === 200) {
          const clone = response.clone();
          caches.open(CACHE_NAME).then(cache => cache.put(request, clone));
        }
        return response;
      })
      .catch(() => caches.match(request).then(cached => cached || caches.match('/offline.html')))
  );
});
