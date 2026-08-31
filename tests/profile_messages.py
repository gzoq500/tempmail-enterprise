#!/usr/bin/env python3
"""Isolate /api/messages cost on the crypto build: auth vs decrypt vs query.

Runs against a staging instance with a known alias holding K emails of S bytes.
Measures: /api/messages (full), /api/health (baseline), and repeated reads with
a second alias holding 0 emails (auth-only cost).
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
import urllib.request

BIN = "/opt/tempmail/backend/build/tempmail-server"
WORK = "/tmp/tm-profile"
N = 200


def main() -> int:
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(f"{WORK}/data", exist_ok=True)
    os.makedirs(f"{WORK}/keys", exist_ok=True)
    env = dict(os.environ)
    env["TEMPMAIL_DB"] = f"{WORK}/data/tempmail.db"
    env["TEMPMAIL_KEY_DIR"] = f"{WORK}/keys"
    proc = subprocess.Popen([BIN, "--port", "3131", "--domain", "routerssh.web.id"],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = "http://127.0.0.1:3131"
    try:
        for _ in range(200):
            try:
                urllib.request.urlopen(base + "/api/health", timeout=0.5)
                break
            except Exception:
                time.sleep(0.05)

        def create():
            req = urllib.request.Request(base + "/api/alias", data=b'{"duration":"1h"}',
                                         headers={"Content-Type": "application/json"}, method="POST")
            with urllib.request.urlopen(req, timeout=5) as r:
                d = json.loads(r.read())
            return d["email"], d["api_key"]

        def send(email, i):
            payload = {"from": "p@example.com", "to": email, "subject": f"P{i}",
                       "body": "Your code is 1 " + "y" * 6000, "html": ""}
            req = urllib.request.Request(base + "/api/incoming", data=json.dumps(payload).encode(),
                                         headers={"Content-Type": "application/json"}, method="POST")
            with urllib.request.urlopen(req, timeout=5) as r:
                r.read()

        # alias A: 100 emails of ~6KB ; alias B: 0 emails (auth-only)
        email_a, key_a = create()
        for i in range(100):
            send(email_a, i)
        _, key_b = create()

        def timed(path, key, n=N):
            lat = []
            for _ in range(n):
                req = urllib.request.Request(base + path, headers={"X-API-Key": key})
                t0 = time.perf_counter()
                with urllib.request.urlopen(req, timeout=10) as r:
                    r.read()
                lat.append((time.perf_counter() - t0) * 1000)
            lat.sort()
            return {"p50": round(lat[len(lat) // 2], 3), "p99": round(lat[int(len(lat) * .99)], 3)}

        result = {
            "health_baseline": timed("/api/health", key_b),
            "auth_only_0_emails": timed("/api/messages", key_b),
            "full_100_emails_6kb": timed("/api/messages", key_a),
        }
        print(json.dumps(result, indent=2))
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
