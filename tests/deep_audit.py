#!/usr/bin/env python3
"""Deep API audit against an isolated staging backend.

Uses a copy of production DB + production key directory read-only copy, never
mutates production. Covers numeric overflow, auth isolation, malformed JSON,
concurrent duplicate alias creation, long-poll deletion, and ciphertext tamper.
"""
from __future__ import annotations
import concurrent.futures, json, os, shutil, sqlite3, subprocess, tempfile, threading, time
import urllib.error, urllib.request

BIN = "/opt/tempmail/backend/build/tempmail-server"
PROD_DB = "/opt/tempmail/backend/data/tempmail.db"
PROD_KEYS = "/opt/tempmail/backend/data/keys"
PORT = 3151
BASE = f"http://127.0.0.1:{PORT}"


def req(path, method="GET", payload=None, key=None, timeout=10):
    data = json.dumps(payload).encode() if payload is not None else None
    h = {"Content-Type":"application/json"} if payload is not None else {}
    if key: h["X-API-Key"] = key
    r = urllib.request.Request(BASE+path, data=data, headers=h, method=method)
    try:
        with urllib.request.urlopen(r, timeout=timeout) as x:
            raw=x.read(); return x.status, raw.decode(errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors="replace")
    except Exception as e:
        return -1, type(e).__name__+":"+str(e)


def main():
    root=tempfile.mkdtemp(prefix="tm-deep-")
    db=f"{root}/tempmail.db"; keys=f"{root}/keys"
    shutil.copy2(PROD_DB,db); shutil.copytree(PROD_KEYS,keys)
    env=dict(os.environ,TEMPMAIL_DB=db,TEMPMAIL_KEY_DIR=keys)
    p=subprocess.Popen([BIN,"--port",str(PORT),"--domain","routerssh.web.id"],env=env,
                       stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
    findings=[]
    created=[]
    try:
        for _ in range(100):
            if req('/api/health')[0]==200: break
            time.sleep(.05)
        # numeric overflow should be 400, never 500/-1
        huge='9'*500
        for path in (f'/api/email/{huge}',f'/api/extract/{huge}'):
            s,b=req(path,key='temp-'+'A'*34)
            print('numeric',path[:25],s,b[:80])
            if s>=500 or s==-1: findings.append(f'numeric overflow {path}: {s} {b[:80]}')
        # malformed JSON/type matrix
        malformed=[b'',b'{',b'[]',b'null',b'{"duration":1}',b'{"email":true}',b'{"duration":"bogus"}']
        for raw in malformed:
            r=urllib.request.Request(BASE+'/api/alias',data=raw,headers={'Content-Type':'application/json'},method='POST')
            try:
                with urllib.request.urlopen(r,timeout=5) as x: s=x.status; body=x.read()
            except urllib.error.HTTPError as e: s=e.code; body=e.read()
            print('json',raw[:25],s,body[:60])
            # Empty body is a supported shortcut for a default 24h alias.
            # Any non-empty malformed/non-object/wrong-type/unknown duration
            # must be rejected instead of silently creating an alias.
            if raw and s != 400:
                findings.append(f'malformed alias JSON accepted ({s}): {raw!r}')
        # create normal alias
        s,b=req('/api/alias','POST',{'duration':'1h'}); a=json.loads(b); created.append(a)
        # auth matrix
        for path in ('/api/messages','/api/aliases','/api/wait?timeout=1'):
            s,_=req(path); print('missing-auth',path,s)
            if s!=401: findings.append(f'{path} missing auth => {s}')
        # duplicate custom alias race (one success only)
        custom='audit-race@routerssh.web.id'
        def create_custom(_): return req('/api/alias','POST',{'email':custom,'duration':'1h'})
        with concurrent.futures.ThreadPoolExecutor(max_workers=20) as ex:
            out=list(ex.map(create_custom,range(20)))
        codes=[x[0] for x in out]; print('duplicate-race', {c:codes.count(c) for c in set(codes)})
        if codes.count(200)!=1 or any(c>=500 for c in codes): findings.append(f'duplicate race codes {codes}')
        for s,b in out:
            if s==200: created.append(json.loads(b))
        # ISO timestamp expiry must be interpreted chronologically, not compared
        # lexicographically against SQLite's space-separated datetime format.
        s,b=req('/api/alias','POST',{'duration':'1h'}); expired=json.loads(b); created.append(expired)
        conn=sqlite3.connect(db)
        conn.execute("UPDATE aliases SET expires_at=datetime('now','-1 minute') WHERE email=?",(expired['email'],))
        conn.commit(); conn.close()
        s,_=req('/api/messages',key=expired['api_key'])
        print('expired-key',s)
        if s != 401: findings.append(f'expired alias still authenticates: {s}')
        # long-poll then delete alias; wait should terminate boundedly, not hang/crash
        result={}
        def waiter(): result['wait']=req('/api/wait?after=0&timeout=5',key=a['api_key'],timeout=8)
        t=threading.Thread(target=waiter); t.start(); time.sleep(.3)
        sd,bd=req('/api/alias','DELETE',key=a['api_key']); t.join(7)
        print('delete-during-wait',sd,result.get('wait'))
        if t.is_alive(): findings.append('wait hangs after alias deletion')
        # ciphertext tamper on an isolated fresh alias with one email
        s,b=req('/api/alias','POST',{'duration':'1h'}); c=json.loads(b); created.append(c)
        req('/api/incoming','POST',{'from':'audit@example.com','to':c['email'],'subject':'Tamper','body':'code 123456'})
        conn=sqlite3.connect(db)
        row=conn.execute('SELECT e.id FROM emails e JOIN aliases a ON a.id=e.alias_id WHERE a.email=?',(c['email'],)).fetchone(); eid=row[0]
        conn.execute("UPDATE emails SET subject=randomblob(80) WHERE id=?",(eid,)); conn.commit(); conn.close()
        s,b=req(f'/api/email/{eid}',key=c['api_key']); print('tamper-read',s,b[:100].encode(errors='replace'))
        if s>=500 or s==-1: findings.append(f'tampered ciphertext causes service error {s}')
        print('FINDINGS',json.dumps(findings))
        return 1 if findings else 0
    finally:
        p.terminate()
        try:p.wait(timeout=5)
        except: p.kill()
        shutil.rmtree(root,ignore_errors=True)

if __name__=='__main__': raise SystemExit(main())
