# AGENTS.md

## Overview

TempMail Enterprise is a lightweight temporary email service.

- Backend: C++17, cpp-httplib, SQLite WAL
- Frontend: Svelte 4, TypeScript, Vite 5, Tailwind CSS
- Runtime: systemd + Caddy, with Cloudflare Email Routing or Postfix
- Package manager: npm (`npm ci`)

## Commands

```bash
# Backend build
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build --parallel

# Frontend setup/build
cd frontend-svelte
npm ci
npm run build

# Regression tests (requires the local backend on port 3001)
cd ..
python3 tests/regression_tests.py

# Runtime checks
curl -fsS http://127.0.0.1:3001/api/health
sqlite3 backend/data/tempmail.db 'PRAGMA integrity_check;'
```

## Conventions

- Technical files, source code, comments, logs, and documentation use English.
- C++ endpoint handlers validate and bound all external numeric/string inputs.
- SQLite queries use prepared statements; never concatenate user input into SQL.
- External processes use `fork`/`execv`; never execute user input through a shell.
- Frontend API paths use `encodeURIComponent` for user-derived URL components.
- Email HTML renders inside Shadow DOM to isolate sender CSS without iframe scroll cost.

Example prepared statement:

```cpp
sqlite3_prepare_v2(db_, "DELETE FROM aliases WHERE email = ?", -1, &stmt, nullptr);
sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
```

## Boundaries

Never commit or modify without explicit task scope:

- `.env` or secret files
- Access tokens, SMTP credentials, Cloudflare credentials
- SQLite production databases and WAL/SHM files
- User email data, `.eml` files, mailboxes, sessions
- `node_modules/`, `dist/`, `build/`, vendored generated output
- Production domain/service configuration with private values

Do not remove `backend/include/httplib.h`, `backend/include/json.hpp`, or the checked-in SQLite amalgamation without replacing the dependency mechanism and updating build instructions.

- Kyber key material (`keys/`, `*.kyber-envelope`, `mlkem768.sk`) — production secrets; never commit or delete without explicit instruction.

## Dependencies

- CMake 3.16+
- C++17 compiler
- pthread
- SQLite (system library or checked-in amalgamation as configured)
- Node.js 18+
- npm
- Caddy for production static serving and reverse proxy
- Postfix only when using the pipe handler

## Configuration

Use placeholders in tracked configuration:

```text
TEMPMAIL_PORT=3001
TEMPMAIL_DOMAIN=example.com
TEMPMAIL_DB=/opt/tempmail/backend/data/tempmail.db
TEMPMAIL_API_URL=http://127.0.0.1:3001/api/incoming
```

Customize `Caddyfile` and `systemd/tempmail-backend.service` at deployment time.

## Error Handling

- API handlers return explicit 4xx responses for invalid input.
- Backend exceptions are converted to JSON 500 responses.
- Postfix handler retries bounded transient failures and exits 75 on final failure.
- Frontend long polling uses AbortController and generation IDs to prevent duplicate loops.
- One alias/email failure must not corrupt unrelated state.

## Troubleshooting

### Email reaches Cloudflare but not the web inbox

Check the Email Worker destination and confirm public `POST /api/incoming` is not blocked. A malformed payload should return 400, not 404.

### Forwarded/Bcc email lands on the wrong alias

Postfix must invoke `scripts/tempmail-handler` with `${sender} ${recipient}`. The handler uses the envelope recipient, not the visible `To` header.

### Email content scrolls poorly on mobile

Do not restore iframe rendering, ResizeObserver height loops, large CSS blur filters, or `will-change` on tall email content. The Shadow DOM renderer is intentional.

### Email displays a MIME boundary line

Run the ingestion regression tests and inspect trailing multipart boundary stripping in `backend/src/server.cpp`. `tests/cleanup_mime_boundaries.py` is for one-time cleanup of existing data.
