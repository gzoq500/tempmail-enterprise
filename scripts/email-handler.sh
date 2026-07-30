#!/bin/bash
# TempMail Email Handler v3 - send raw body, let backend parse
INPUT=$(cat)

FROM=$(echo "$INPUT" | grep -m1 "^From:" | sed 's/^From: //')
TO=$(echo "$INPUT" | grep -m1 "^To:" | sed 's/^To: //')
SUBJECT=$(echo "$INPUT" | grep -m1 "^Subject:" | sed 's/^Subject: //')
TO_EMAIL=$(echo "$TO" | grep -oP '[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}' | head -1 | tr '[:upper:]' '[:lower:]')

# Get full body after first blank line (preserves all MIME structure)
RAW_BODY=$(echo "$INPUT" | sed '1,/^$/d')

# Escape for JSON (preserve newlines as \n)
BODY_ESCAPED=$(echo "$RAW_BODY" | python3 -c "
import sys, json
text = sys.stdin.read()
print(json.dumps(text)[1:-1])
" 2>/dev/null)

# Send raw body as both body and html - let backend C++ parser handle extraction
curl -s -X POST http://localhost:3001/api/incoming \
  -H "Content-Type: application/json" \
  -d "{\"from\":\"$(echo "$FROM" | sed 's/"/\\"/g')\",\"to\":\"$TO_EMAIL\",\"subject\":\"$(echo "$SUBJECT" | sed 's/"/\\"/g')\",\"body\":\"$BODY_ESCAPED\",\"html\":\"\"}" 2>/dev/null

exit 0
