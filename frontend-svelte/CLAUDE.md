# TempMail frontend

## Overview

This project is the Svelte 4 and Vite 5 frontend for RouterSSH TempMail. It builds to static files served by Caddy. The C++ backend remains separate and is accessed through relative `/api` routes.

Runtime: browser. Package manager: npm. Styling: Tailwind base plus a custom responsive design system in `src/app.css`.

## Commands

```bash
npm install
npm run dev -- --host 127.0.0.1
npm run build
```

No standalone test, lint, or format scripts currently exist. A production build is the minimum verification gate.

## Conventions

Keep network and browser storage logic in `src/lib/api.ts`. Keep MIME display and sender formatting in `src/lib/helpers.ts`. Keep all localized product copy in `src/lib/i18n.ts`. Keep component behavior and markup in `src/App.svelte`. Use CSS custom properties and mobile-first media queries in `src/app.css`. Keep the standalone API reference in `public/docs/`; Vite copies it unchanged to `dist/docs/`.

Source files, identifiers, comments, error internals, and documentation use English. Indonesian appears only in explicit `id` locale strings.

## Boundaries

Never touch secrets, `.env`, access keys, session files, production mailbox data, the SQLite database, vendor code, `node_modules`, or production Caddy configuration from this frontend project. Never include real user addresses or API keys in fixtures, docs, or logs. Do not change `/api` contracts without a coordinated backend change.

## Dependencies

Dependencies are declared in `package.json`. Use npm to install them. Preserve the Vite Svelte runtime compatibility plugin until a dependency upgrade is separately verified.

## Configuration

The frontend expects the same origin to proxy `/api/*` to the C++ backend. No client secret or backend hostname belongs in the bundle. Public domain examples may use `routerssh.web.id`.

## Error handling

Display short, actionable messages. Do not expose raw server responses or credentials. Preserve saved mailbox keys when failures are transient. Cancel long polling whenever the active mailbox or selected message changes.

## Troubleshooting

A blank page usually indicates a failed Vite/Svelte bundle; run `npm run build` and inspect the browser console. A healthy page with failed data calls usually means the Caddy API route or backend on port 3001 is unavailable. If email content breaks layout, keep sender HTML isolated in Shadow DOM and sanitize active content before mounting it. For documentation routing, verify both `/docs` and `/docs/`; Caddy should canonicalize the directory path and serve `dist/docs/index.html`.
