# TempMail Repository Sync Scope

This repository contains the current non-secret TempMail source and deployment templates.

Excluded intentionally:
- `backend/data/tempmail.db` and all runtime database files
- `backend/data/keys/` encryption keys
- `.env`, API keys, tokens, passwords, private credentials
- `backups/`, generated build caches, `node_modules/`, `.next/`
- Runtime email contents and customer/user data

The live database and encryption keys are backed up separately on the VPS.
