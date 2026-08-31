#!/usr/bin/env python3
"""Behavior tests for one API key per generated alias."""
from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

BASE_URL = os.environ.get("TEMPMAIL_TEST_URL", "http://127.0.0.1:3001")


def request(path: str, *, method: str = "GET", payload: dict | None = None,
            api_key: str | None = None, timeout: float = 5) -> tuple[int, str]:
    data = json.dumps(payload).encode() if payload is not None else None
    headers = {"Content-Type": "application/json"} if payload is not None else {}
    if api_key:
        headers["X-API-Key"] = api_key
    req = urllib.request.Request(BASE_URL + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", "replace")


def create_alias() -> tuple[str, str]:
    status, body = request("/api/alias", method="POST", payload={"duration": "1h"})
    assert status == 200, (status, body)
    data = json.loads(body)
    key = data.get("api_key", "")
    # Short base62 format, e.g. temp-2HvyZULQcsSjaDdtjCUxh5wpJo9dRDKWbb
    import re
    assert re.fullmatch(r"temp-[A-Za-z0-9]{34}", key), key
    return data["email"], key


def incoming(alias: str, subject: str, code: str) -> None:
    status, body = request(
        "/api/incoming",
        method="POST",
        payload={
            "from": "sender@example.com",
            "to": alias,
            "subject": subject,
            "body": f"Your verification code is {code}",
            "html": "",
        },
    )
    assert status == 200, (status, body)


def messages(*, key: str | None = None, query_key: str | None = None) -> tuple[int, str]:
    path = "/api/messages"
    if query_key is not None:
        path += "?key=" + urllib.parse.quote(query_key, safe="")
    return request(path, api_key=key)


def test_alias_generation_returns_unique_temp_key() -> None:
    alias_a, key_a = create_alias()
    alias_b, key_b = create_alias()
    assert alias_a != alias_b
    assert key_a != key_b


def test_messages_requires_valid_key() -> None:
    for key in (None, "temp-invalid"):
        status, _ = messages(key=key)
        assert status == 401, (key, status)


def test_key_reads_only_its_own_alias_messages() -> None:
    alias_a, key_a = create_alias()
    alias_b, key_b = create_alias()
    incoming(alias_a, "Key A", "111111")
    incoming(alias_b, "Key B", "222222")

    status, body = messages(key=key_a)
    assert status == 200, (status, body)
    data = json.loads(body)
    assert data["email"] == alias_a
    subjects = [item["subject"] for item in data["emails"]]
    assert "Key A" in subjects and "Key B" not in subjects
    assert key_a not in body and key_b not in body

    status, body = messages(query_key=key_b)
    assert status == 200
    data = json.loads(body)
    assert data["email"] == alias_b
    assert any(item["subject"] == "Key B" for item in data["emails"])


def test_wait_and_extract_are_scoped_by_alias_key() -> None:
    alias_a, key_a = create_alias()
    alias_b, key_b = create_alias()
    incoming(alias_a, "Scoped OTP", "654321")

    status, body = request("/api/wait?after=0&timeout=1", api_key=key_a, timeout=3)
    assert status == 200, (status, body)
    email_id = json.loads(body)["id"]

    status, body = request(f"/api/extract/{email_id}", api_key=key_a)
    assert status == 200 and json.loads(body).get("code") == "654321"
    status, _ = request(f"/api/extract/{email_id}", api_key=key_b)
    assert status == 404

    # Key B has no mail; it must not receive alias A's event.
    status, _ = request("/api/wait?after=0&timeout=1", api_key=key_b, timeout=3)
    assert status == 408


def test_delete_by_key_does_not_accept_another_alias_key() -> None:
    alias_a, key_a = create_alias()
    alias_b, key_b = create_alias()

    status, _ = request("/api/alias", method="DELETE", api_key=key_a)
    assert status == 200
    status, _ = messages(key=key_a)
    assert status == 401

    status, body = messages(key=key_b)
    assert status == 200 and json.loads(body)["email"] == alias_b


def run() -> int:
    tests = [value for name, value in sorted(globals().items()) if name.startswith("test_") and callable(value)]
    failed = 0
    for test in tests:
        try:
            test()
            print(f"PASS {test.__name__}")
        except Exception as exc:
            failed += 1
            print(f"FAIL {test.__name__}: {exc}", file=sys.stderr)
    print(f"RESULT passed={len(tests) - failed} failed={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(run())
