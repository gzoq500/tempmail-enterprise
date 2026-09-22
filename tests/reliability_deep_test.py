#!/usr/bin/env python3
"""Reliability regressions: burst wait ordering + invalid SMTP bytes."""
from __future__ import annotations
import json, os, shutil, subprocess, tempfile, time, urllib.request, urllib.error
BIN='/opt/tempmail/backend/build/tempmail-server'; PORT=3153; BASE=f'http://127.0.0.1:{PORT}'
def call(p,m='GET',d=None,key=None,timeout=15):
 data=json.dumps(d).encode() if d is not None else None; h={'Content-Type':'application/json'} if d is not None else {}
 if key:h['X-API-Key']=key
 r=urllib.request.Request(BASE+p,data=data,headers=h,method=m)
 try:
  with urllib.request.urlopen(r,timeout=timeout) as x:return x.status,json.loads(x.read())
 except urllib.error.HTTPError as e:
  return e.code,json.loads(e.read())
def main():
 root=tempfile.mkdtemp(prefix='tm-rel-'); os.makedirs(root+'/keys'); env=dict(os.environ,TEMPMAIL_DB=root+'/db',TEMPMAIL_KEY_DIR=root+'/keys')
 p=subprocess.Popen([BIN,'--port',str(PORT),'--domain','routerssh.web.id'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 bad=[]
 try:
  for _ in range(100):
   try:
    if call('/api/health')[0]==200:break
   except:time.sleep(.05)
  _,a=call('/api/alias','POST',{'duration':'1h'}); key,email=a['api_key'],a['email']
  for code in ('111111','222222','333333'):
   call('/api/incoming','POST',{'from':'burst@example.com','to':email,'subject':'Burst '+code,'body':'OTP '+code})
  after=0; seen=[]
  for _ in range(3):
   s,m=call(f'/api/wait?after={after}&timeout=2',key=key); seen.append(m['subject']); after=m['id']
  print('wait-order',seen)
  if seen != ['Burst 111111','Burst 222222','Burst 333333']:bad.append('wait burst skipped/reordered messages')
  # The SMTP handler must not crash with exit 1 on arbitrary message bytes.
  handler=os.environ.get('TEMPMAIL_HANDLER_PATH','/usr/local/bin/tempmail-handler')
  handler_env=dict(os.environ,TEMPMAIL_API_URL=BASE+'/api/incoming')
  raw=b'From: bad@example.com\nTo: nobody@routerssh.web.id\nSubject: invalid byte\n\nbody:\xff\n'
  hp=subprocess.run([handler,'bad@example.com','nobody@routerssh.web.id'],input=raw,capture_output=True,env=handler_env)
  print('handler-invalid-byte rc',hp.returncode)
  if hp.returncode not in (0,75):bad.append(f'handler crashed with rc={hp.returncode}')
  # Real Postfix path must preserve top-level MIME headers so nested parts and
  # transfer encodings decode correctly.
  raw_mime=(f'From: sender@example.com\r\nTo: {email}\r\nSubject: Handler MIME\r\n'
            'MIME-Version: 1.0\r\nContent-Type: multipart/alternative; boundary="hb"\r\n\r\n'
            '--hb\r\nContent-Type: text/plain\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n'
            'Literal =3D41 OTP 889900\r\n--hb\r\nContent-Type: text/html\r\n\r\n'
            '<html><body>HTML 889900</body></html>\r\n--hb--\r\n').encode()
  hp=subprocess.run([handler,'sender@example.com',email],input=raw_mime,capture_output=True,env=handler_env)
  s,listing=call('/api/messages?full=1',key=key)
  msg=next((m for m in listing['emails'] if m['subject']=='Handler MIME'),{})
  print('handler-mime rc',hp.returncode,'text',repr(msg.get('body_text')))
  if hp.returncode!=0 or '=41' not in msg.get('body_text','') or '<html' not in msg.get('body_html','').lower():bad.append('handler lost top-level MIME headers')
  print('FINDINGS',json.dumps(bad)); return 1 if bad else 0
 finally:
  p.terminate();
  try:p.wait(5)
  except:p.kill()
  shutil.rmtree(root,ignore_errors=True)
if __name__=='__main__':raise SystemExit(main())
