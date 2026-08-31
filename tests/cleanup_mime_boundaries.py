#!/usr/bin/env python3
"""Remove trailing MIME boundary delimiters from stored email bodies."""
import sqlite3

DB = "/opt/tempmail/backend/data/tempmail.db"

def clean(s: str) -> str:
    if not s:
        return s
    while True:
        nl = s.rfind("\n")
        line = (s[nl + 1:] if nl >= 0 else s).strip()
        if line.startswith("--") and len(line) >= 4:
            s = s[:nl] if nl > 0 else ""
            s = s.rstrip("\r\n")
        else:
            break
    return s

def main() -> None:
    conn = sqlite3.connect(DB)
    rows = conn.execute("SELECT id, body_html, body_text FROM emails").fetchall()
    fixed = 0
    for eid, html, text in rows:
        nh, nt = clean(html or ""), clean(text or "")
        if nh != (html or "") or nt != (text or ""):
            conn.execute("UPDATE emails SET body_html=?, body_text=? WHERE id=?", (nh, nt, eid))
            fixed += 1
    conn.commit()
    print(f"fixed {fixed} of {len(rows)}")

if __name__ == "__main__":
    main()
