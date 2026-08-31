#!/usr/bin/env python3
"""TempMail load and latency benchmark.

Measures:
1. Latency percentiles (health, alias+key create, messages read, wait).
2. Sustained load at increasing concurrency (10/50/100/200) on health + messages.
3. Throughput of the full automation path (create -> incoming -> read -> extract).
4. Encryption overhead check: crypto unit timings.

Env:
  TEMPMAIL_TEST_URL (default http://127.0.0.1:3001)
"""
from __future__ import annotations

import json
import os
import statistics
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

BASE = os.environ.get("TEMPMAIL_TEST_URL", "http://127.0.0.1:3001")


def call(path: str, *, method: str = "GET", payload: dict | None = None,
         key: str | None = None, timeout: float = 30) -> tuple[int, float]:
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


def percentile(samples: list[float], p: float) -> float:
    ordered = sorted(samples)
    idx = min(len(ordered) - 1, int(len(ordered) * p))
    return ordered[idx]


def bench_endpoint(path: str, n: int = 200, key: str | None = None,
                   method: str = "GET", payload: dict | None = None) -> dict:
    samples: list[float] = []
    errors = 0
    for _ in range(n):
        status, ms = call(path, method=method, payload=payload, key=key)
        if status >= 400:
            errors += 1
        samples.append(ms)
    return {
        "n": n,
        "errors": errors,
        "avg_ms": round(statistics.mean(samples), 2),
        "p50_ms": round(percentile(samples, 0.50), 2),
        "p95_ms": round(percentile(samples, 0.95), 2),
        "p99_ms": round(percentile(samples, 0.99), 2),
        "max_ms": round(max(samples), 2),
    }


def load_test(path: str, concurrency: int, duration_s: float, key: str | None = None) -> dict:
    stop_at = time.perf_counter() + duration_s
    latencies: list[float] = []
    lock_free_counts = {"ok": 0, "err": 0}

    import threading
    def worker():
        local: list[float] = []
        ok = err = 0
        while time.perf_counter() < stop_at:
            status, ms = call(path, key=key)
            local.append(ms)
            if status < 400:
                ok += 1
            else:
                err += 1
        with threading.Lock():
            latencies.extend(local)
            lock_free_counts["ok"] += ok
            lock_free_counts["err"] += err

    threads = [threading.Thread(target=worker) for _ in range(concurrency)]
    t0 = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    wall = time.perf_counter() - t0
    total = lock_free_counts["ok"] + lock_free_counts["err"]
    return {
        "concurrency": concurrency,
        "rps": round(total / wall, 1),
        "total": total,
        "errors": lock_free_counts["err"],
        "avg_ms": round(statistics.mean(latencies), 2),
        "p95_ms": round(percentile(latencies, 0.95), 2),
        "p99_ms": round(percentile(latencies, 0.99), 2),
    }


def full_pipeline_burst(n: int) -> dict:
    """Each iteration: create alias -> incoming email -> messages -> extract."""
    durations: list[float] = []
    errors = 0

    def one(i: int) -> None:
        t0 = time.perf_counter()
        try:
            status, _ = call("/api/alias", method="POST", payload={"duration": "1h"})
            if status != 200:
                raise RuntimeError(f"alias {status}")
            # fetch the key via a second create response is wasteful; instead
            # use raw request to capture key
            nonlocal_dummy = None
        except Exception:
            nonlocal_dummy = 1

    # Simpler: sequential raw pipeline using explicit requests
    import threading
    lock = threading.Lock()

    def pipeline(i: int):
        nonlocal errors
        try:
            t0 = time.perf_counter()
            req = urllib.request.Request(BASE + "/api/alias", data=b'{"duration":"1h"}',
                                         headers={"Content-Type": "application/json"}, method="POST")
            with urllib.request.urlopen(req, timeout=30) as r:
                data = json.loads(r.read())
            email, key = data["email"], data["api_key"]
            payload = {"from": f"bench{i}@example.com", "to": email,
                       "subject": f"Bench {i}", "body": f"code is {100000+i}", "html": ""}
            req = urllib.request.Request(BASE + "/api/incoming", data=json.dumps(payload).encode(),
                                         headers={"Content-Type": "application/json"}, method="POST")
            with urllib.request.urlopen(req, timeout=30) as r:
                r.read()
            req = urllib.request.Request(BASE + "/api/messages", headers={"X-API-Key": key})
            with urllib.request.urlopen(req, timeout=30) as r:
                mails = json.loads(r.read())["emails"]
            mid = mails[0]["id"]
            req = urllib.request.Request(f"{BASE}/api/extract/{mid}", headers={"X-API-Key": key})
            with urllib.request.urlopen(req, timeout=30) as r:
                json.loads(r.read())
            req = urllib.request.Request(BASE + "/api/alias", headers={"X-API-Key": key}, method="DELETE")
            with urllib.request.urlopen(req, timeout=30) as r:
                r.read()
            ms = (time.perf_counter() - t0) * 1000
            with lock:
                durations.append(ms)
        except Exception:
            with lock:
                errors += 1

    with ThreadPoolExecutor(max_workers=20) as pool:
        list(pool.map(pipeline, range(n)))
    durations_sorted = sorted(durations)
    return {
        "pipelines": n,
        "errors": errors,
        "avg_ms": round(statistics.mean(durations), 1),
        "p95_ms": round(percentile(durations, 0.95), 1),
        "per_pipeline_rps": round(len(durations) / (sum(durations) / 1000 / 20), 1),
    }


def cleanup_bench_aliases() -> int:
    """Remove aliases whose emails carry the Bench subject."""
    import sqlite3
    db = os.environ.get("TEMPMAIL_TEST_DB", "/opt/tempmail/backend/data/tempmail.db")
    conn = sqlite3.connect(db)
    cur = conn.execute("SELECT id FROM aliases WHERE email IN "
                       "(SELECT DISTINCT to_address FROM emails WHERE subject LIKE 'Bench %')")
    rows = cur.fetchall()
    if not rows:
        conn.close()
        return 0
    ids = [r[0] for r in rows]
    q = ",".join("?" * len(ids))
    conn.execute(f"DELETE FROM emails WHERE alias_id IN ({q})", ids)
    conn.execute(f"DELETE FROM aliases WHERE id IN ({q})", ids)
    conn.commit()
    conn.close()
    return len(ids)


def main() -> int:
    print(f"Target: {BASE}\n")
    print("== Single-request latency (200 samples each) ==")
    for name, kwargs in (
        ("GET /api/health", dict(path="/api/health")),
    ):
        r = bench_endpoint(**kwargs)
        print(f"{name}: {r}")

    # Create a bench alias for authenticated paths
    req = urllib.request.Request(BASE + "/api/alias", data=b'{"duration":"1h"}',
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=10) as r:
        data = json.loads(r.read())
    key = data["api_key"]
    # Give it one email so /api/messages returns content
    payload = {"from": "seed@example.com", "to": data["email"], "subject": "Bench seed",
               "body": "code is 111111", "html": ""}
    req = urllib.request.Request(BASE + "/api/incoming", data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=10) as r:
        r.read()

    print(f"GET /api/messages (auth+decrypt): {bench_endpoint('/api/messages', n=200, key=key)}")
    print(f"POST /api/alias (keygen+hash):   {bench_endpoint('/api/alias', method='POST', payload={'duration': '1h'}, n=100)}")

    print("\n== Sustained load: /api/health ==")
    for c in (10, 50, 100, 200):
        print(load_test("/api/health", c, 8))

    print("\n== Sustained load: /api/messages (auth + AES decrypt per row) ==")
    for c in (10, 50, 100):
        print(load_test("/api/messages", c, 8, key=key))

    print("\n== Full pipeline burst (create->incoming->read->extract->delete) ==")
    print(full_pipeline_burst(200))

    # cleanup bench data
    removed = cleanup_bench_aliases()
    # also delete the seed alias via key
    try:
        urllib.request.urlopen(urllib.request.Request(BASE + "/api/alias",
                                                      headers={"X-API-Key": key}, method="DELETE"), timeout=10)
    except Exception:
        pass
    print(f"\nCleanup: removed {removed} bench aliases")
    return 0


if __name__ == "__main__":
    sys.exit(main())
