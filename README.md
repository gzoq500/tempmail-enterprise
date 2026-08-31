# TempMail Enterprise

A lightweight temporary email service with a C++ backend and Svelte 4 frontend.

## Highlights

- C++17 API server using cpp-httplib and SQLite WAL
- Static Svelte 4 + TypeScript frontend (about 20 KB compressed initial transfer)
- Shadow DOM email rendering for isolated styles and smooth mobile scrolling
- Cloudflare Email Worker or Postfix pipe delivery support
- Event-driven long polling for real-time inbox updates
- Per-alias expiration and cleanup
- Secure sendmail execution with `fork`/`execv` (no shell)
- Parameterized SQLite queries and transactional deletion
- Mobile Gmail-style sender header and responsive email content

## Architecture

```text
Incoming email
  ├─ Cloudflare Email Routing -> Email Worker -> POST /api/incoming
  └─ Postfix pipe -> scripts/tempmail-handler -> POST /api/incoming

Browser -> Caddy -> static Svelte frontend
                 -> /api/* -> C++ backend (127.0.0.1:3001)
                              -> SQLite WAL database
```

## Stack

- Backend: C++17, cpp-httplib, SQLite
- Frontend: Svelte 4, TypeScript, Vite 5, Tailwind CSS
- Proxy/static server: Caddy
- Mail: Cloudflare Email Routing and/or Postfix

## Build

### Backend

```bash
cmake -S backend -B backend/build -DCMAKE_BUILD_TYPE=Release
cmake --build backend/build --parallel
```

### Frontend

```bash
cd frontend-svelte
npm ci
npm run build
```

Static output is generated in `frontend-svelte/dist/`.

## Test

The regression suite targets a running local backend on `127.0.0.1:3001` and the configured public endpoint:

```bash
python3 tests/regression_tests.py
```

Current coverage includes:

- Database integrity
- SQL parameterization
- Send endpoint command-injection rejection
- Active-sender enforcement
- Malformed MIME handling
- Envelope recipient handling
- Delivery failure propagation
- Public incoming payload validation
- Shadow DOM renderer invariants

## Deploy

1. Build backend and frontend.
2. Create a dedicated `tempmail` system user.
3. Create writable runtime directories:

```bash
sudo install -d -o tempmail -g tempmail /opt/tempmail/backend/data
```

4. Copy and customize:

```text
systemd/tempmail-backend.service
Caddyfile
```

Replace `example.com` and `tempmail.example.com` with the deployment domains.

5. Install the service and reload Caddy:

```bash
sudo cp systemd/tempmail-backend.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now tempmail-backend
sudo caddy validate --config /etc/caddy/Caddyfile
sudo systemctl reload caddy
```

## Postfix Pipe Handler

Install the reliable handler:

```bash
sudo install -m 0755 scripts/tempmail-handler /usr/local/bin/tempmail-handler
```

Postfix should pass envelope sender and recipient:

```text
tempmail unix - n n - - pipe flags=Rq user=tempmail argv=/usr/local/bin/tempmail-handler ${sender} ${recipient}
```

The handler uses the envelope recipient as the authoritative destination and returns `EX_TEMPFAIL` (`75`) when backend delivery fails so Postfix queues and retries the message.

## Configuration

Backend options:

```text
--port 3001
--domain example.com
```

Environment variables:

```text
TEMPMAIL_PORT=3001
TEMPMAIL_DOMAIN=example.com
TEMPMAIL_DB=/opt/tempmail/backend/data/tempmail.db
TEMPMAIL_API_URL=http://127.0.0.1:3001/api/incoming  # handler only
```

Do not commit production databases, email data, tokens, `.env` files, session files, or private configuration. The repository `.gitignore` excludes these artifacts.

## Operational Checks

```bash
curl -fsS http://127.0.0.1:3001/api/health
sqlite3 backend/data/tempmail.db 'PRAGMA integrity_check;'
systemctl is-active tempmail-backend caddy postfix
postqueue -p
```

## Renderer Behavior

HTML email is mounted into an isolated Shadow DOM root. Sender styles remain scoped to the email while the content stays in the main page compositor tree for smooth mobile scrolling. Images and tables are constrained to the viewport. CSS animations and transitions are frozen to avoid layout churn during scroll. Links open in a new tab with `noopener noreferrer`.

## License

Use and modify according to the repository owner's requirements.
