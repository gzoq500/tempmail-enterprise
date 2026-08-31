#!/usr/bin/env python3
"""Flood test: 1000+ incoming emails against a staging instance.

Phases:
1. Serial flood: 1000 emails, one at a time (delivery-agent style).
2. Concurrent flood: 500 emails from 50 threads (blast style).
3. Read the 1500-email inbox: /api/messages and /api/wait behavior.
4. Report DB size, row count, and server RSS.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

BIN = os.environ.get("TEMPMAIL_BIN", "/opt/tempmail/backend/build/tempmail-server")
PORT = int(os.environ.get("TEMPMAIL_FLOOD_PORT", "3141"))
WORK = "/tmp/tm-flood"
BASE = f"http://127.0.0.1:{PORT}"


def call(path: str, *, method: str = "GET", payload: dict | None = None,
         key: str | None = None, timeout: float = 30):
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


def main() -> int:
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(f"{WORK}/data", exist_ok=True)
    os.makedirs(f"{WORK}/keys", exist_ok=True)
    env = dict(os.environ)
    env["TEMPMAIL_DB"] = f"{WORK}/data/tempmail.db"
    env["TEMPMAIL_KEY_DIR"] = f"{WORK}/keys"
    proc = subprocess.Popen([BIN, "--port", str(PORT), "--domain", "routerssh.web.id"],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(200):
            try:
                urllib.request.urlopen(BASE + "/api/health", timeout=0.5)
                break
            except Exception:
                time.sleep(0.05)

        # Setup alias
        status, ms = call("/api/alias", method="POST", payload={"duration": "1h"})
        assert status == 200
        req = urllib.request.Request(BASE + "/api/alias", data=b'{"duration":"1h"}',
                                     headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=5) as r:
            d = json.loads(r.read())
        email, key = d["email"], d["api_key"]
        print(f"alias: {email}")

        body = ("Your verification code is 998877\n" + "f" * 6000)  # ~6KB like real HTML mail

        # Phase 1: serial 1000
        lat = []
        errors = 0
        t0 = time.perf_counter()
        for i in range(1000):
            s, m = call("/api/incoming", method="POST", payload={
                "from": f"flood{i}@example.com", "to": email,
                "subject": f"Flood serial {i}", "body": body, "html": ""})
            if s != 200:
                errors += 1
            lat.append(m)
        serial_s = time.perf_counter() - t0
        lat.sort()
        print(f"serial 1000: {serial_s:.1f}s = {1000/serial_s:.0f} email/s, errors={errors}, "
              f"p50={lat[500]:.2f}ms p99={lat[990]:.2f}ms max={lat[-1]:.2f}ms")

        # Phase 2: concurrent 500 from 50 threads
        lat2 = []
        errs2 = [0]
        lock = threading.Lock()
        counter = [0]

        def worker():
            local = []
            while True:
                with lock:
                    i = counter[0]
                    counter[0] += 1
                    if i >= 500:
                        break
                s, m = call("/api/incoming", method="POST", payload={
                    "from": f"burst{i}@example.com", "to": email,
                    "subject": f"Flood burst {i}", "body": body, "html": ""})
                if s != 200:
                    with lock:
                        errs2[0] += 1
                local.append(m)
            with lock:
                lat2.extend(local)

        threads = [threading.Thread(target=worker) for _ in range(50)]
        t0 = time.perf_counter()
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        burst_s = time.perf_counter() - t0
        lat2.sort()
        print(f"burst 500 (50 threads): {burst_s:.1f}s = {500/burst_s:.0f} email/s, errors={errs2[0]}, "
              f"p50={lat2[len(lat2)//2]:.2f}ms p99={lat2[int(len(lat2)*.99)]:.2f}ms max={lat2[-1]:.2f}ms")

        # Phase 3: read the 1500-email inbox
        s, m_read = call("/api/messages", key=key, timeout=120)
        print(f"read 1500-email inbox: status={s} latency={m_read:.0f}ms")

        # long-poll wait under loaded DB (after=last → expects timeout)
        s, m_wait = call(f"/api/wait?after=0&timeout=2", key=key, timeout=30)
        print(f"wait on loaded DB: status={s} latency={m_wait:.0f}ms")

        # DB stats
        import sqlite3
        conn = sqlite3.connect(f"{WORK}/data/tempmail.db")
        n = conn.execute("SELECT count(*) FROM emails").fetchone()[0]
        page = conn.execute("PRAGMA page_count").fetchone()[0]
        ps = conn.execute("PRAGMA page_size").fetchone()[0]
        wal = os.path.getsize(f"{WORK}/data/tempmail.db-wal") if os.path.exists(f"{WORK}/data/tempmail.db-wal") else 0
        print(f"db: emails={n} size={page*ps/1024/1024:.1f}MB wal={wal/1024/1024:.1f}MB")

        rss = int(open(f"/proc/{proc.pid}/status").read().split("VmRSS:")[1].split()[0])
        print(f"server RSS: {rss/1024:.1f}MB")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        shutil.rmtree(WORK, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
