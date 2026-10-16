/**
 * SmartCity Service Worker (Phase 0 skeleton)
 * Precaches the app shell and vendored libraries.
 * Full offline page and cache strategies added in Phase 6.
 */

const CACHE_NAME = 'smartcity-v1';

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
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then(cache => cache.addAll(APP_SHELL))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(keys =>
      Promise.all(keys.filter(k => k !== CACHE_NAME).map(k => caches.delete(k)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', event => {
  const { request } = event;
  // Network-first for API and WS; cache-first for app shell
  if (request.url.includes('/api/') || request.url.includes('/healthz')) {
    event.respondWith(fetch(request).catch(() => new Response('{}', { status: 503 })));
    return;
  }
  event.respondWith(
    caches.match(request).then(cached =>
      cached || fetch(request).catch(() => caches.match('/offline.html'))
    )
  );
});
