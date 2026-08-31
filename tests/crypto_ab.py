#!/usr/bin/env python3
"""Apples-to-apples: pre-crypto build vs crypto build, identical load."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import time
import urllib.request

CRYPTO_BIN = "/opt/tempmail/backend/build/tempmail-server"
OLD_BIN = "/tmp/tm-pre-crypto/backend/build/tempmail-server"
WORK = "/tmp/tm-ab"
N = 300


def start(binary: str, port: int, name: str) -> subprocess.Popen:
    root = f"{WORK}/{name}"
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(f"{root}/data", exist_ok=True)
    os.makedirs(f"{root}/keys", exist_ok=True)
    env = dict(os.environ)
    env["TEMPMAIL_DB"] = f"{root}/data/tempmail.db"
    env["TEMPMAIL_KEY_DIR"] = f"{root}/keys"  # ignored by the old build
    proc = subprocess.Popen([binary, "--port", str(port), "--domain", "routerssh.web.id"],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(200):
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{port}/api/health", timeout=0.5)
            return proc
        except Exception:
            time.sleep(0.05)
    raise RuntimeError(f"{name} failed to start")


def bench(port: int) -> dict:
    base = f"http://127.0.0.1:{port}"
    req = urllib.request.Request(base + "/api/alias", data=b'{"duration":"1h"}',
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=5) as r:
        data = json.loads(r.read())
    email, key = data["email"], data.get("api_key", "")
    payload = {"from": "ab@example.com", "to": email, "subject": "AB cmp",
               "body": "Your code is 424242 " + "x" * 6000, "html": ""}

    def timed(path, n, method="GET", body=None, hdr=None):
        lat = []
        for _ in range(n):
            r2 = urllib.request.Request(base + path, data=body, headers=hdr or {}, method=method)
            t0 = time.perf_counter()
            with urllib.request.urlopen(r2, timeout=10) as resp:
                resp.read()
            lat.append((time.perf_counter() - t0) * 1000)
        lat.sort()
        return {"p50": round(lat[len(lat) // 2], 3), "p99": round(lat[int(len(lat) * .99)], 3)}

    json_h = {"Content-Type": "application/json"}
    auth_h = {"X-API-Key": key} if key else {}
    out = {}
    out["alias_create"] = timed("/api/alias", N, "POST", b'{"duration":"1h"}', json_h)
    out["incoming_store_6kb"] = timed("/api/incoming", 100, "POST", json.dumps(payload).encode(), json_h)
    out["messages_read"] = timed("/api/messages", N, "GET", None, auth_h)
    out["health"] = timed("/api/health", N)
    return out


def main() -> None:
    os.makedirs(WORK, exist_ok=True)
    procs, res = [], {}
    try:
        p = start(OLD_BIN, 3121, "old"); procs.append(p)
        res["pre_crypto_26ab847"] = bench(3121)
        p = start(CRYPTO_BIN, 3122, "new"); procs.append(p)
        res["crypto_kyber_aes_HEAD"] = bench(3122)
    finally:
        for p in procs:
            p.terminate()
            try: p.wait(timeout=5)
            except Exception: p.kill()
        shutil.rmtree(WORK, ignore_errors=True)
    print(json.dumps(res, indent=2))


if __name__ == "__main__":
    main()
