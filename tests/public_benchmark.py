#!/usr/bin/env python3
"""Public-path latency benchmark (light, respects single-origin limits)."""
from __future__ import annotations

import json
import os
import statistics
import time
import urllib.error
import urllib.request

BASE = os.environ.get("TEMPMAIL_TEST_URL", "https://tempmail.routerssh.web.id")


def call(path: str, *, method: str = "GET", payload: dict | None = None,
         key: str | None = None, timeout: float = 15) -> tuple[int, float]:
    data = json.dumps(payload).encode() if payload is not None else None
    headers = {"Content-Type": "application/json"} if payload is not None else {}
    if key:
        headers["X-API-Key"] = key
    req = urllib.request.Request(BASE + path, data=data, headers=headers, method=method)
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            r.read()
            return r.status, (time.perf_counter() - t0) * 1000
    except urllib.error.HTTPError as e:
        e.read()
        return e.code, (time.perf_counter() - t0) * 1000


def pct(samples: list[float], p: float) -> float:
    ordered = sorted(samples)
    return ordered[min(len(ordered) - 1, int(len(ordered) * p))]


def bench(name: str, path: str, n: int, **kw) -> None:
    samples = []
    errors = 0
    for _ in range(n):
        status, ms = call(path, **kw)
        if status >= 400:
            errors += 1
        samples.append(ms)
    print(f"{name}: n={n} errors={errors} "
          f"avg={statistics.mean(samples):.1f}ms p50={pct(samples,0.5):.1f}ms "
          f"p95={pct(samples,0.95):.1f}ms p99={pct(samples,0.99):.1f}ms max={max(samples):.1f}ms")


def main() -> None:
    print(f"Public target: {BASE}")
    bench("GET /api/health", "/api/health", 30)

    req = urllib.request.Request(BASE + "/api/alias", data=b'{"duration":"1h"}',
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=15) as r:
        data = json.loads(r.read())
    email, key = data["email"], data["api_key"]
    payload = {"from": "pub@example.com", "to": email, "subject": "Pub bench",
               "body": "code is 998877", "html": ""}
    req = urllib.request.Request(BASE + "/api/incoming", data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=15) as r:
        r.read()

    bench("GET /api/messages (key auth)", "/api/messages", 30, key=key)
    bench("POST /api/alias (keygen)", "/api/alias", 20, method="POST", payload={"duration": "1h"})
    bench("POST /api/incoming (AES encrypt)", "/api/incoming", 20, method="POST",
          payload={"from": "x@example.com", "to": email, "subject": "Pub bench",
                   "body": "b", "html": ""})

    # small concurrent burst (10 x 2s) to see public behavior without DoS-ing ourselves
    import threading
    lat: list[float] = []
    stop = time.perf_counter() + 4

    def worker() -> None:
        local: list[float] = []
        while time.perf_counter() < stop:
            _, ms = call("/api/health")
            local.append(ms)
        lat.extend(local)

    threads = [threading.Thread(target=worker) for _ in range(10)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print(f"Burst 10-conn /api/health: n={len(lat)} rps={len(lat)/4:.1f} "
          f"p50={pct(lat,0.5):.1f}ms p95={pct(lat,0.95):.1f}ms p99={pct(lat,0.99):.1f}ms")

    urllib.request.urlopen(urllib.request.Request(
        BASE + "/api/alias", headers={"X-API-Key": key}, method="DELETE"), timeout=15)


if __name__ == "__main__":
    main()
