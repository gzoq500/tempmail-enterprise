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

## Automation API Keys

Every newly generated alias receives its own bearer key:

```json
{
  "email": "example123@example.com",
  "api_key": "temp-<64 random hexadecimal characters>"
}
```

The browser stores the `email + api_key` pair in localStorage and shows the key in the alias panel. One key can access only the alias created with it.

### Generate alias and key

```bash
curl -sS -X POST https://tempmail.example.com/api/alias \
  -H 'Content-Type: application/json' \
  -d '{"duration":"1h"}'
```

### Read messages

Query parameter (convenient for automation URLs):

```bash
curl -sS 'https://tempmail.example.com/api/messages?key=temp-...'
```

Header (recommended when possible):

```bash
curl -sS https://tempmail.example.com/api/messages \
  -H 'X-API-Key: temp-...'
```

### Wait for a new message

```bash
curl -sS 'https://tempmail.example.com/api/wait?key=temp-...&after=0&timeout=30'
```

### Extract OTP, token, or magic link

```bash
curl -sS 'https://tempmail.example.com/api/extract/123?key=temp-...'
```

### Delete the alias and invalidate its key

```bash
curl -sS -X DELETE 'https://tempmail.example.com/api/alias?key=temp-...'
```

`POST /api/incoming` intentionally remains keyless because Cloudflare Email Routing and Postfix deliver by recipient alias. All browser/automation read, wait, extract, delete, and send operations require the matching alias key. API-key responses use `Cache-Control: no-store`.

Existing aliases created before the API-key migration remain in the database but do not receive a retroactive key. New aliases always receive one.

## Encryption & Post-Quantum Protection

### At-Rest (application layer)

- Email fields (`from`, `to`, `subject`, `body_text`, `body_html`) are encrypted with **AES-256-GCM** before insertion. Each row derives its own key via HKDF-SHA256 from a master key and the row id.
- API keys are stored only as **salted HMAC-SHA256** hashes; the raw key is returned once at alias creation and never persisted.
- Ciphertext and keys use length-aware BLOB handling (embedded NULs safe).

### Kyber (ML-KEM-768)

- The AES master key is wrapped in a **Kyber/ML-KEM-768 envelope** (PQClean reference implementation, vendored in `backend/third_party/kyber768`).
- `TEMPMAIL_KEY_DIR` (default `<db_dir>/keys`) holds `mlkem768.pk` (0644), `mlkem768.sk` (0600), and `master.kyber-envelope` (0600).
- At startup the server decapsulates the envelope with the Kyber secret key to recover the master key. First boot generates everything automatically.
- **Back up `TEMPMAIL_KEY_DIR` together with the database.** Losing the Kyber secret key or envelope makes stored ciphertexts unrecoverable. Legacy plaintext rows written before this feature remain readable via the decryption fallback.

### In-Transit

- Caddy terminates TLS 1.3 with AES-256-GCM suites verified against the public endpoint.
- Cloudflare edge negotiates hybrid post-quantum X25519MLKEM768 automatically for capable clients.

### Operational Checks

```bash
# Keys present with correct permissions
ls -l /opt/tempmail/backend/data/keys/

# No plaintext email content in the database
sqlite3 backend/data/tempmail.db \
  "SELECT count(*) FROM emails WHERE subject LIKE '%code%' OR body_text LIKE '%http%';"

# API keys are 32-byte hashes
sqlite3 backend/data/tempmail.db \
  "SELECT DISTINCT length(CAST(api_key AS BLOB)) FROM aliases WHERE api_key IS NOT NULL;"
```

## Renderer Behavior

HTML email is mounted into an isolated Shadow DOM root. Sender styles remain scoped to the email while the content stays in the main page compositor tree for smooth mobile scrolling. Images and tables are constrained to the viewport. CSS animations and transitions are frozen to avoid layout churn during scroll. Links open in a new tab with `noopener noreferrer`.

## License

Use and modify according to the repository owner's requirements.
