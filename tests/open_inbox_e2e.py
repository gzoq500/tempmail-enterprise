#!/usr/bin/env python3
"""E2E: open an inbox from a fresh browser using email + API key."""
import json
import time
import urllib.request

import websocket

BASE = "https://tempmail.routerssh.web.id"


def api(p, m="GET", d=None, key=None):
    data = json.dumps(d).encode() if d is not None else None
    h = {"Content-Type": "application/json"} if d is not None else {}
    if key:
        h["X-API-Key"] = key
    req = urllib.request.Request(BASE + p, data=data, headers=h, method=m)
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read())


def main():
    a = api("/api/alias", "POST", {"duration": "24h"})
    email, key = a["email"], a["api_key"]
    api("/api/incoming", "POST", {"from": "noreply@notice.owlproxy.com", "to": email,
                                  "subject": "Cross-device OTP", "body": "Your code is 556677"})
    print("alias ready:", email)

    tabs = json.loads(urllib.request.urlopen("http://127.0.0.1:9222/json").read())
    ws_url = [t for t in tabs if t["type"] == "page"][0]["webSocketDebuggerUrl"]
    ws = websocket.create_connection(ws_url, timeout=30)
    counter = [0]

    def send(method, params=None):
        counter[0] += 1
        ws.send(json.dumps({"id": counter[0], "method": method, "params": params or {}}))
        while True:
            m = json.loads(ws.recv())
            if m.get("id") == counter[0]:
                return m.get("result", {})

    def ev(expr):
        return send("Runtime.evaluate", {"expression": expr, "returnByValue": True}).get("result", {}).get("value")

    send("Page.enable")
    send("Runtime.enable")
    send("Page.navigate", {"url": BASE + "/?v=" + str(time.time())})
    time.sleep(3)

    ev("[...document.querySelectorAll('button')].find(b=>b.textContent.includes('Buka Inbox dengan Key'))?.click()")
    time.sleep(0.5)

    fill_js = (
        "(()=>{const em=document.getElementById('open-email'),ky=document.getElementById('open-key');"
        "const set=Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype,'value').set;"
        f"set.call(em,'{email}'); em.dispatchEvent(new Event('input',{{bubbles:true}}));"
        f"set.call(ky,'{key}'); ky.dispatchEvent(new Event('input',{{bubbles:true}}));"
        "return 'filled';})()"
    )
    print("fill:", ev(fill_js))
    ev("document.getElementById('open-key').form.requestSubmit()")
    time.sleep(3)

    state_js = (
        "(()=>{const hdr=document.querySelector('.font-mono.text-purple-300');"
        "return JSON.stringify({activeEmail: hdr?hdr.textContent:null,"
        "inboxVisible: document.body.textContent.includes('Cross-device OTP')});})()"
    )
    print("state:", ev(state_js))

    ev("[...document.querySelectorAll('button')].find(b=>b.textContent.includes('Cross-device OTP'))?.click()")
    time.sleep(2)
    body_js = (
        "(()=>{const t=document.body.textContent;"
        "return JSON.stringify({otpVisible: t.includes('556677'),"
        "senderOk: t.includes('Owlproxy')});})()"
    )
    print("body:", ev(body_js))

    # wrong key flow
    ev("localStorage.clear(); location.reload(); return 'reloading';")
    time.sleep(2.5)
    ev("[...document.querySelectorAll('button')].find(b=>b.textContent.includes('Buka Inbox dengan Key'))?.click()")
    time.sleep(0.5)
    wrong_js = (
        "(()=>{const em=document.getElementById('open-email'),ky=document.getElementById('open-key');"
        "const set=Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype,'value').set;"
        f"set.call(em,'{email}'); em.dispatchEvent(new Event('input',{{bubbles:true}}));"
        "set.call(ky,'temp-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'); ky.dispatchEvent(new Event('input',{{bubbles:true}}));"
        "document.getElementById('open-key').form.requestSubmit(); return 'submitted-wrong';})()"
    )
    ev(wrong_js)
    time.sleep(2)
    err_js = "(()=>{const e=document.querySelector('.text-red-400'); return e?e.textContent:'no error shown';})()"
    print("wrong-result:", ev(err_js))

    ws.close()
    api("/api/alias", "DELETE", key=key)
    print("cleanup done")


if __name__ == "__main__":
    main()
