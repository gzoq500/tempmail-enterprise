# TempMail frontend

## Overview

TempMail is a Svelte 4 single-page frontend for the RouterSSH temporary email service. Vite builds static files served by Caddy. The backend API is a separate C++ service exposed under `/api`.

Stack: Svelte 4, Vite 5, TypeScript modules, Tailwind CSS 3, npm.

## Commands

```bash
npm install
npm run build
npm run dev -- --host 127.0.0.1
```

There is currently no separate lint or test script. `npm run build` is the required compile and bundle check.

## Conventions

Keep API and storage logic in `src/lib/api.ts`, rendering helpers in `src/lib/helpers.ts`, localized user copy in `src/lib/i18n.ts`, and component layout in `src/App.svelte`. Global design tokens and responsive styles belong in `src/app.css`. The standalone API documentation route lives in `public/docs/` and is copied to `dist/docs/` by Vite.

Example:

```typescript
export async function getEmails(email: string): Promise<EmailResponse> {
  // API logic stays in this module.
}
```

Use English for source code, comments, identifiers, logs, and documentation. Indonesian is allowed only in the `id` locale user-facing strings.

## Boundaries

Never edit or commit secrets, `.env` files, API keys, session files, production databases, mailbox data, `node_modules`, or generated `dist` files as source changes. Do not change backend API contracts without coordinating the C++ backend update. Do not expose access keys in logs or screenshots.

## Dependencies

Runtime dependencies are declared in `package.json`. Install with npm. Do not hand-edit `node_modules`.

## Configuration

The frontend uses relative `/api` URLs. Caddy must route `/api/*` to the backend and serve `dist` for all other paths. The public email domain is currently represented in the UI as `routerssh.web.id`.

## Error handling

Show concise user-facing errors without leaking response bodies, access keys, or internal details. Preserve locally stored mailbox keys through transient network failures. Abort long polling when switching mailbox or opening a message.

## Troubleshooting

If the page is blank, run `npm run build` and check the browser console. The Vite configuration contains a Svelte runtime tree-shaking compatibility plugin; preserve it unless the dependency version has been upgraded and verified. If API calls fail only in production, verify Caddy's `/api/*` route and backend health on port 3001. Verify API documentation with `curl -I https://tempmail.routerssh.web.id/docs/` and confirm `/docs` redirects to the canonical trailing-slash route.
