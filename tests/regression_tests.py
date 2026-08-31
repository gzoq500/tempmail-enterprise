#!/usr/bin/env python3
"""TempMail regression tests for security and reliability fixes."""

from __future__ import annotations

import json
import os
import signal
import sqlite3
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BASE_URL = os.environ.get("TEMPMAIL_TEST_URL", "http://127.0.0.1:3001")
DB_PATH = Path(os.environ.get("TEMPMAIL_TEST_DB", str(REPO_ROOT / "backend/data/tempmail.db")))
SERVER_SOURCE = Path(os.environ.get("TEMPMAIL_SERVER_SOURCE", str(REPO_ROOT / "backend/src/server.cpp")))
DATABASE_SOURCE = Path(os.environ.get("TEMPMAIL_DATABASE_SOURCE", str(REPO_ROOT / "backend/src/database.cpp")))
HANDLER_PATH = Path(os.environ.get("TEMPMAIL_HANDLER_PATH", str(REPO_ROOT / "scripts/tempmail-handler")))


def request(path: str, *, method: str = "GET", payload: dict | None = None, timeout: float = 5) -> tuple[int, str]:
    data = json.dumps(payload).encode() if payload is not None else None
    headers = {"Content-Type": "application/json"} if payload is not None else {}
    req = urllib.request.Request(BASE_URL + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            return response.status, response.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", "replace")


def test_invalid_numeric_parameters_return_400() -> None:
    status, body = request("/api/alias", method="POST", payload={"duration": "forever"})
    assert status == 200, (status, body)
    email = json.loads(body)["email"]
    encoded = urllib.parse.quote(email, safe="")
    for path in (
        f"/api/check/{encoded}?after=invalid",
        f"/api/emails/{encoded}?after=invalid",
        f"/api/wait/{encoded}?after=invalid&timeout=1",
        f"/api/wait/{encoded}?after=0&timeout=invalid",
        f"/api/wait/{encoded}?after=0&timeout=-1",
    ):
        status, _ = request(path)
        assert status == 400, (path, status)


def test_custom_alias_rejects_external_domain() -> None:
    # Use an unmistakably external reserved domain. The staging server itself
    # may legitimately be configured as example.com.
    status, _ = request("/api/alias", method="POST", payload={"email": "user@invalid.example", "duration": "1h"})
    assert status == 400, status


def test_send_requires_active_sender_alias() -> None:
    status, _ = request(
        "/api/send",
        method="POST",
        payload={
            "from": "not-an-alias@routerssh.web.id",
            "name": "Audit",
            "to": "target@example.com",
            "subject": "Audit",
            "body": "Audit",
        },
    )
    assert status == 403, status


def test_send_endpoint_rejects_shell_metacharacters() -> None:
    marker = Path("/tmp/tempmail_command_injection_marker")
    marker.unlink(missing_ok=True)
    payload = {
        "from": "safe@routerssh.web.id' ; touch /tmp/tempmail_command_injection_marker ; echo '",
        "name": "Audit",
        "to": "target@example.com",
        "subject": "Audit",
        "body": "Audit",
    }
    status, _ = request("/api/send", method="POST", payload=payload)
    assert status == 400, status
    assert not marker.exists(), "command injection marker was created"


def test_delete_uses_parameterized_sql() -> None:
    source = DATABASE_SOURCE.read_text()
    assert "WHERE email='\" + email" not in source
    assert "sqlite3_bind_text" in source


def test_send_does_not_use_shell_system() -> None:
    source = SERVER_SOURCE.read_text()
    assert "system(cmd.c_str())" not in source
    assert "execv(" in source or "posix_spawn" in source


def test_handler_uses_envelope_recipient() -> None:
    source = HANDLER_PATH.read_text()
    assert "sys.argv[2]" in source
    assert "envelope_recipient" in source


def test_handler_propagates_delivery_failure() -> None:
    raw = b"From: sender@example.com\nTo: receiver@routerssh.web.id\nSubject: Retry test\n\nRetry body\n"
    env = os.environ.copy()
    env["TEMPMAIL_API_URL"] = "http://127.0.0.1:1/api/incoming"
    proc = subprocess.run([str(HANDLER_PATH)], input=raw, env=env, capture_output=True, timeout=15)
    assert proc.returncode != 0, proc.returncode
    assert proc.stderr, "delivery failure should be logged"


def test_frontend_renders_email_unmodified() -> None:
    source = (REPO_ROOT / "frontend-svelte/src/lib/helpers.ts").read_text()
    # Render-as-received policy: no sanitization, no stripping.
    assert "buildEmailDocument" in source
    # Shadow DOM host replaces the old iframe (same compositor tree as the
    # page — no iframe layer rasterization stalls while scrolling on phones).
    assert "email-shadow-host" in (REPO_ROOT / "frontend-svelte/src/App.svelte").read_text()
    # Plain text must never be blank when body_text exists (short OTP included).
    assert "kind: 'text'" in source
    assert "renderPlainText" in source


def test_public_incoming_rejects_malformed_payload() -> None:
    request_object = urllib.request.Request(
        "https://tempmail.routerssh.web.id/api/incoming",
        data=b"{}",
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        urllib.request.urlopen(request_object, timeout=5)
        raise AssertionError("malformed incoming payload was accepted")
    except urllib.error.HTTPError as exc:
        assert exc.code == 400, exc.code


def test_malformed_mime_q_header_does_not_crash() -> None:
    status, body = request("/api/alias", method="POST", payload={"duration": "1h"})
    assert status == 200
    email = json.loads(body)["email"]
    payload = {
        "from": "sender@example.com",
        "to": email,
        "subject": "=?UTF-8?Q?broken=ZZheader?=",
        "body": "Test body",
        "html": "",
    }
    status, response = request("/api/incoming", method="POST", payload=payload)
    assert status == 200, (status, response)


def test_database_integrity() -> None:
    connection = sqlite3.connect(f"file:{DB_PATH}?mode=ro", uri=True)
    assert connection.execute("PRAGMA integrity_check").fetchone()[0] == "ok"


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
