# CLAUDE.md

## Overview

TempMail Enterprise is a lightweight temporary email service.

- Backend: C++17, cpp-httplib, SQLite WAL
- Frontend: Svelte 4, TypeScript, Vite 5, Tailwind CSS
- Runtime: Caddy + systemd, with Cloudflare Email Routing or Postfix
- Package manager: npm

## Commands

```bash
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build --parallel

cd frontend-svelte
npm ci
npm run build

cd ..
python3 tests/regression_tests.py
```

## Conventions

- Use English for source, comments, logs, and documentation.
- Use prepared SQLite statements for user-controlled values.
- Use `fork`/`execv`, not shell commands, for external processes.
- Keep account/email state explicit and failure-isolated.
- Render email HTML in Shadow DOM, not iframe.
- Preserve sender HTML and styles, while constraining media/table width and freezing animation for mobile performance.

Example:

```cpp
sqlite3_prepare_v2(db_, "DELETE FROM aliases WHERE email = ?", -1, &stmt, nullptr);
sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
```

## Boundaries

Never touch or commit:

- `.env`, API tokens, SMTP credentials, Cloudflare credentials
- Production databases, WAL/SHM files, user emails, sessions
- `node_modules/`, `frontend-svelte/dist/`, `backend/build/`
- Production configuration containing real secrets or private data

Do not reintroduce iframe rendering, polling-based height measurement, ResizeObserver on email bodies, large blur filters, or forced `will-change` layers for email content.

- Kyber key material (`keys/`, `*.kyber-envelope`, `mlkem768.sk`) — production secrets; never commit or delete without explicit instruction.

## Dependencies

- CMake 3.16+
- C++17 compiler and pthread
- SQLite
- Node.js 18+ and npm
- Caddy in production
- Postfix when the pipe handler is used

## Configuration

Tracked examples use placeholders:

```text
TEMPMAIL_PORT=3001
TEMPMAIL_DOMAIN=example.com
TEMPMAIL_DB=/opt/tempmail/backend/data/tempmail.db
TEMPMAIL_API_URL=http://127.0.0.1:3001/api/incoming
```

## Error Handling

- Bound and validate external input.
- Return explicit 4xx responses for invalid requests.
- Convert backend exceptions into JSON 500 responses.
- Return exit 75 from the Postfix handler after bounded delivery retries fail.
- Abort stale frontend long-poll requests and prevent duplicate polling generations.

## Troubleshooting

- Missing inbound mail: verify Cloudflare Worker destination and public `/api/incoming` routing.
- Wrong alias for forwarded/Bcc mail: verify Postfix passes `${recipient}` to the handler.
- MIME boundary visible: inspect trailing boundary stripping and run regression tests.
- Mobile scroll jank: preserve Shadow DOM rendering and frozen email animations.
