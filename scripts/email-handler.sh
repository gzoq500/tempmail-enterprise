#!/bin/bash
# TempMail Email Handler - clean version v2
INPUT=$(cat)

FROM=$(echo "$INPUT" | grep -m1 "^From:" | sed 's/^From: //')
TO=$(echo "$INPUT" | grep -m1 "^To:" | sed 's/^To: //')
SUBJECT=$(echo "$INPUT" | grep -m1 "^Subject:" | sed 's/^Subject: //')
TO_EMAIL=$(echo "$TO" | grep -oP '[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}' | head -1 | tr '[:upper:]' '[:lower:]')

# Extract body after first blank line
RAW_BODY=$(echo "$INPUT" | sed '1,/^$/d')

# Try to extract HTML content (between Content-Type: text/html and next boundary)
HTML_BODY=$(echo "$RAW_BODY" | sed -n '/Content-Type: text\/html/,/^--/p' | grep -v "^Content-Type:" | grep -v "^Content-Transfer-Encoding:" | grep -v "^charset=" | grep -v "^--" | grep -v "^$" | tr '\n' ' ' | sed 's/"/\\"/g')

# Try to extract plain text content
TEXT_BODY=$(echo "$RAW_BODY" | sed -n '/Content-Type: text\/plain/,/^--/p' | grep -v "^Content-Type:" | grep -v "^Content-Transfer-Encoding:" | grep -v "^charset=" | grep -v "^--" | grep -v "^$" | tr '\n' ' ' | sed 's/"/\\"/g')

# Use HTML if available, otherwise text
if [ -n "$HTML_BODY" ]; then
    BODY="$HTML_BODY"
elif [ -n "$TEXT_BODY" ]; then
    BODY="$TEXT_BODY"
else
    # Fallback: get first non-empty line after headers
    BODY=$(echo "$RAW_BODY" | grep -v "^$" | grep -v "^--[0-9]" | grep -v "^Content-Type:" | grep -v "^Content-Transfer-Encoding:" | head -5 | tr '\n' ' ' | sed 's/"/\\"/g')
fi

# If still empty, try to get from div dir="auto"
if [ -z "$BODY" ]; then
    BODY=$(echo "$RAW_BODY" | grep -oP '(?<=<div dir="auto">)[^<]+' | head -1 | sed 's/"/\\"/g')
fi

# Final cleanup - remove MIME boundaries
BODY=$(echo "$BODY" | sed 's/--[0-9a-f]\{20,\}//g' | sed 's/Content-Type: [^;]*;//g' | sed 's/charset=[^;]*;//g')

curl -s -X POST http://localhost:3001/api/incoming \
  -H "Content-Type: application/json" \
  -d "{\"from\":\"$(echo "$FROM" | sed 's/"/\\"/g')\",\"to\":\"$TO_EMAIL\",\"subject\":\"$(echo "$SUBJECT" | sed 's/"/\\"/g')\",\"body\":\"$BODY\",\"html\":\"$HTML_BODY\"}" 2>/dev/null

exit 0
HANDLER

chmod +x /opt/tempmail/scripts/email-handler.sh
cp /opt/tempmail/scripts/email-handler.sh /usr/local/bin/tempmail-handler
chmod +x /usr/local/bin/tempmail-handler

echo "Email handler v2 installed"