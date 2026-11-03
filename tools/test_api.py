import urllib.request
import json

def test():
    # 1. Health
    with urllib.request.urlopen('http://localhost:8080/healthz') as r:
        print('Health:', r.read().decode())

    # 2. Vehicles live (horizon 0)
    with urllib.request.urlopen('http://localhost:8080/api/vehicles?horizon=0') as r:
        d = json.loads(r.read().decode())
        print(f"Vehicles horizon 0: count={d['count']}, first={d['positions'][0]}")

    # 3. Vehicles predicted (horizon 25 min)
    with urllib.request.urlopen('http://localhost:8080/api/vehicles?horizon=25') as r:
        d2 = json.loads(r.read().decode())
        print(f"Vehicles horizon 25: count={d2['count']}, first={d2['positions'][0]}")

    # 4. Traffic horizon 25 min
    with urllib.request.urlopen('http://localhost:8080/api/traffic?bbox=77.30,28.50,77.45,28.65&zoom=14&horizon=25') as r:
        tr = json.loads(r.read().decode())
        print(f"Traffic horizon 25: edges={len(tr['edges'])}")

    # 5. Route test
    req = urllib.request.Request(
        'http://localhost:8080/api/route',
        data=json.dumps({
            'from': {'lat': 28.5672, 'lon': 77.3211},
            'to': {'lat': 28.6297, 'lon': 77.3721},
            'mode': 'traffic',
            'alternatives': 2,
            'want_explored': True,
            'horizon_min': 20
        }).encode(),
        headers={'Content-Type': 'application/json'}
    )
    with urllib.request.urlopen(req) as r:
        rt = json.loads(r.read().decode())
        routes = rt.get('routes', [])
        print(f"Routes count at +20 min: {len(routes)}")
        for i, r in enumerate(routes):
            print(f"  Route {i}: {r['label']} - {r['duration_s']:.1f}s, {r['distance_m']:.1f}m")
        print(f"Explored nodes count: {len(rt.get('explored', []))}")

if __name__ == '__main__':
    test()

