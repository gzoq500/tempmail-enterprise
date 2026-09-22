# TempMail Frontend Agent Guide

## Scope

This directory contains the production Svelte 4 static frontend for TempMail. Keep frontend work isolated from `/opt/tempmail/backend`, Caddy, secrets, and service configuration unless the task explicitly expands the scope.

## Stack and commands

- Svelte `4.2.19`
- Vite `5`
- Tailwind CSS `3`
- Install dependencies: `npm ci`
- Production build: `npm run build`

## Implementation rules

- Preserve the existing API contracts in `src/lib/api.ts`.
- Preserve the Shadow DOM email renderer in `src/App.svelte`; sender HTML and CSS must remain isolated from application styles.
- Treat 360–390 px mobile viewports as the primary layout target, then verify wider layouts.
- Keep interactive controls usable by touch and long email addresses readable without widening the viewport.
- Use neutral dark surfaces and subtle borders. Burnt orange is an accent for branding and interactive emphasis, not a structural border color.
- Keep all technical source text and comments in English. User-facing copy belongs in `src/lib/i18n.ts`.
- Do not deploy or copy `dist` unless explicitly requested. Building locally is not deployment.

## Verification

Run `npm run build` after source changes. Do not claim deployment from a successful build; Caddy serves the production `dist` directory directly.
