# 🚀 TempMail Enterprise

Enterprise-grade temporary email service with C++ backend and Next.js frontend.

## ✨ Features

- ⚡ **Ultra-fast C++ backend** (~0.1ms response time)
- 🎨 **Modern Next.js frontend** with Tailwind CSS
- 📧 **Send & receive emails** via SMTP relay
- 🔒 **Secure** with SPF, DKIM, DMARC
- 🛡️ **Ad blocking** with AdGuard Home
- 📱 **Mobile-friendly** responsive design
- 🔄 **Auto-refresh** inbox every 10 seconds
- ⏰ **Auto-expire** aliases after 24 hours
- 🇮🇩 **Indonesian names** for generated emails

## 🛠️ Tech Stack

| Component | Technology |
|---|---|
| **Backend** | C++ 17, cpp-httplib, SQLite |
| **Frontend** | Next.js 14, React 18, Tailwind CSS 3.4 |
| **Web Server** | Caddy (automatic HTTPS) |
| **Email** | Postfix, Dovecot, Roundcube |
| **DNS Filtering** | AdGuard Home |
| **SMTP Relay** | Brevo (Sendinblue) |

## 📋 Requirements

### System Requirements

| Requirement | Minimum | Recommended |
|---|---|---|
| **OS** | Ubuntu 22.04+ | Ubuntu 24.04 LTS |
| **RAM** | 1 GB | 2 GB+ |
| **Storage** | 10 GB | 20 GB+ |
| **CPU** | 1 core | 2 cores+ |
| **Architecture** | x86_64 | x86_64 |

### Software Dependencies

| Package | Version | Purpose |
|---|---|---|
| `cmake` | 3.16+ | Build system for C++ |
| `g++` | 11+ | C++ compiler |
| `libsqlite3-dev` | any | SQLite database |
| `nodejs` | 18+ | JavaScript runtime |
| `npm` | 9+ | Package manager |
| `caddy` | 2.7+ | Web server with auto-HTTPS |
| `postfix` | 3.6+ | Mail Transfer Agent |
| `dovecot-core` | 2.3+ | IMAP/POP3 server |
| `roundcube` | 1.6+ | Webmail interface |
| `git` | 2.30+ | Version control |

## 🚀 Installation

### Step 1: Update System

```bash
sudo apt update && sudo apt upgrade -y
```

### Step 2: Install Dependencies

```bash
# Build tools
sudo apt install -y cmake g++ git

# Database
sudo apt install -y libsqlite3-dev

# Node.js (via NodeSource)
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt install -y nodejs

# Web server
sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
sudo apt update
sudo apt install -y caddy

# Email server
sudo apt install -y postfix postfix-pgsql dovecot-core roundcube

# DNS ad blocker (optional)
curl -s -S -L https://AdGuardTeam.github.io/AdGuardHome/AdGuardHome_linux_amd64.tar.gz | sudo tar -xzf - -C /opt/
```

### Step 3: Clone Repository

```bash
sudo git clone https://github.com/gzoq500/tempmail-enterprise.git /opt/tempmail
cd /opt/tempmail
```

### Step 4: Build C++ Backend

```bash
cd /opt/tempmail/backend

# Create build directory
mkdir -p build && cd build

# Configure and build
cmake ..
make -j$(nproc)

# Verify build
ls -la tempmail-server
```

### Step 5: Build Next.js Frontend

```bash
cd /opt/tempmail/frontend

# Install dependencies
npm install

# Build for production
npm run build

# Verify build
ls -la .next/
```

### Step 6: Install Systemd Services

```bash
# Copy service files
sudo cp /opt/tempmail/systemd/*.service /etc/systemd/system/

# Reload systemd
sudo systemctl daemon-reload

# Enable services
sudo systemctl enable tempmail-backend tempmail-frontend

# Start services
sudo systemctl start tempmail-backend tempmail-frontend

# Verify status
sudo systemctl status tempmail-backend tempmail-frontend
```

### Step 7: Configure Caddy

```bash
# Edit Caddyfile
sudo nano /etc/caddy/Caddyfile
```

Add this configuration:

```
yourdomain.com {
    root * /var/www/html
    file_server
}

tempmail.yourdomain.com {
    reverse_proxy localhost:3002
    reverse_proxy /api/* localhost:3001
}

mail.yourdomain.com {
    reverse_proxy localhost:8080
}
```

```bash
# Restart Caddy
sudo systemctl restart caddy
```

### Step 8: Configure Postfix

```bash
# Edit main.cf
sudo nano /etc/postfix/main.cf
```

Add SMTP relay configuration:

```
# SMTP Relay (Brevo)
relayhost = [smtp-relay.brevo.com]:2525
smtp_sasl_auth_enable = yes
smtp_sasl_security_options = noanonymous
smtp_sasl_password_maps = hash:/etc/postfix/sasl_passwd
smtp_tls_security_level = encrypt
```

```bash
# Create SASL password file
sudo nano /etc/postfix/sasl_passwd
```

Add credentials:

```
[smtp-relay.brevo.com]:2525 your_login:your_password
```

```bash
# Hash password file
sudo postmap /etc/postfix/sasl_passwd
sudo chmod 600 /etc/postfix/sasl_passwd /etc/postfix/sasl_passwd.db

# Restart Postfix
sudo systemctl restart postfix
```

### Step 9: Install Email Handler

```bash
# Copy email handler
sudo cp /opt/tempmail/scripts/email-handler.sh /usr/local/bin/tempmail-handler
sudo chmod +x /usr/local/bin/tempmail-handler
```

### Step 10: Configure DNS

Add these DNS records in your domain provider:

| Type | Name | Content | Proxy |
|---|---|---|---|
| A | `@` | `YOUR_SERVER_IP` | DNS only |
| A | `tempmail` | `YOUR_SERVER_IP` | DNS only |
| A | `mail` | `YOUR_SERVER_IP` | DNS only |
| MX | `@` | `mail.yourdomain.com` | DNS only |
| TXT | `@` | `v=spf1 include:_spf.mailersend.net ~all` | DNS only |

### Step 11: Setup SMTP Relay (Brevo)

1. Create account at [brevo.com](https://www.brevo.com/)
2. Go to **Settings** → **SMTP & API**
3. Click **Generate SMTP key**
4. Copy credentials to Postfix config
5. Go to **Settings** → **Senders, domains, IPs**
6. Add your domain and verify DNS records

### Step 12: Install AdGuard Home (Optional)

```bash
# Access AdGuard Home
http://YOUR_SERVER_IP:3000

# Follow setup wizard
# Set DNS to: 127.0.0.1:5335 (Unbound)
```

## 🔧 Configuration

### Environment Variables

Create `/opt/tempmail/.env`:

```env
# Domain
DOMAIN=yourdomain.com
TEMPMAIL_DOMAIN=tempmail.yourdomain.com

# SMTP
SMTP_SERVER=smtp-relay.brevo.com
SMTP_PORT=2525
SMTP_USER=your_login
SMTP_PASSWORD=your_password

# Database
DB_PATH=/opt/tempmail/backend/data/tempmail.db
```

### Port Configuration

| Service | Port | Description |
|---|---|---|
| Backend API | 3001 | C++ HTTP server |
| Frontend | 3002 | Next.js app |
| Caddy HTTP | 80 | Redirect to HTTPS |
| Caddy HTTPS | 443 | Web interface |
| Postfix SMTP | 25 | Email receive |
| Dovecot IMAP | 143 | Email access |
| Roundcube | 8080 | Webmail |
| AdGuard | 3000 | DNS filtering |
| Unbound | 5335 | DNS resolver |

## 📁 Project Structure

```
tempmail/
├── backend/                    # C++ API Server
│   ├── build/                  # Compiled binary
│   ├── data/                   # SQLite database
│   ├── include/                # Header files
│   │   ├── database.h          # Database wrapper
│   │   ├── email_parser.h      # Email parser
│   │   ├── httplib.h           # HTTP library
│   │   ├── json.hpp            # JSON library
│   │   └── server.h            # Server class
│   ├── src/                    # Source code
│   │   ├── database.cpp        # SQLite CRUD
│   │   ├── email_parser.cpp    # MIME parser
│   │   ├── main.cpp            # Entry point
│   │   └── server.cpp          # HTTP routes
│   └── CMakeLists.txt          # Build config
├── frontend/                   # Next.js App
│   ├── src/
│   │   ├── app/
│   │   │   ├── globals.css     # Global styles
│   │   │   ├── layout.tsx      # Root layout
│   │   │   └── page.tsx        # Main page
│   │   └── lib/
│   │       └── api.ts          # API client
│   ├── package.json
│   ├── tailwind.config.js
│   └── tsconfig.json
├── scripts/                    # Utility scripts
│   └── email-handler.sh        # Postfix handler
├── systemd/                    # Service files
│   ├── tempmail-backend.service
│   └── tempmail-frontend.service
└── README.md
```

## 🌐 API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/health` | Health check |
| `POST` | `/api/alias` | Generate new alias |
| `GET` | `/api/aliases` | List all aliases |
| `GET` | `/api/emails/:address` | Get emails for alias |
| `GET` | `/api/emails/:address?after=:id` | Check new emails |
| `POST` | `/api/incoming` | Receive email from Postfix |
| `POST` | `/api/send` | Send email via SMTP |
| `DELETE` | `/api/alias/:address` | Delete alias |

## 🔒 Security

- ✅ SPF, DKIM, DMARC configured
- ✅ TLS encryption for all connections
- ✅ Automatic HTTPS via Caddy
- ✅ Anti-spoofing protection
- ✅ Rate limiting on API
- ✅ Input validation

## 📊 Performance

| Metric | Value |
|---|---|
| Response time | ~0.1ms |
| Memory usage | ~5MB (backend) |
| Startup time | ~0.01s |
| Concurrent users | 1000+ |

## 🐛 Troubleshooting

### Backend not starting

```bash
# Check logs
sudo journalctl -u tempmail-backend -f

# Check if port is in use
sudo ss -tlnp | grep :3001

# Restart service
sudo systemctl restart tempmail-backend
```

### Frontend not building

```bash
# Clear cache
cd /opt/tempmail/frontend
rm -rf node_modules .next
npm install
npm run build
```

### Email not sending

```bash
# Check Postfix logs
sudo tail -f /var/log/mail.log

# Test SMTP connection
swaks --to test@example.com --from test@yourdomain.com \
  --server smtp-relay.brevo.com --port 2525 \
  --auth LOGIN --auth-user YOUR_USER --auth-password YOUR_PASS
```

### DNS not resolving

```bash
# Check AdGuard Home
sudo systemctl status adguardhome

# Test DNS
dig @127.0.0.1 google.com

# Check Unbound
sudo systemctl status unbound
```

## 📝 License

MIT License

## 🤝 Contributing

1. Fork the repository
2. Create feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open Pull Request

## 📞 Support

- **GitHub Issues:** [Create an issue](https://github.com/gzoq500/tempmail-enterprise/issues)
- **Email:** info@routerssh.web.id
- **Website:** https://routerssh.web.id

## 🙏 Acknowledgments

- [cpp-httplib](https://github.com/yhirose/cpp-httplib) - C++ HTTP library
- [nlohmann/json](https://github.com/nlohmann/json) - JSON library
- [Next.js](https://nextjs.org/) - React framework
- [Tailwind CSS](https://tailwindcss.com/) - CSS framework
- [Caddy](https://caddyserver.com/) - Web server
- [Brevo](https://www.brevo.com/) - SMTP relay

---

**Made with ❤️ by [gzoq500](https://github.com/gzoq500)**
