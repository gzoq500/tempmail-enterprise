#!/usr/bin/env python3
"""Deep MIME fixtures against isolated staging backend."""
import json, os, shutil, subprocess, tempfile, time, urllib.request, urllib.error

from pathlib import Path
REPO_ROOT=Path(__file__).resolve().parents[1]
BIN=os.environ.get('TEMPMAIL_TEST_BIN',str(REPO_ROOT/'backend/build/tempmail-server')); PORT=3152; BASE=f'http://127.0.0.1:{PORT}'
def call(p,m='GET',d=None,key=None):
 data=json.dumps(d).encode() if d is not None else None; h={'Content-Type':'application/json'} if d is not None else {}
 if key:h['X-API-Key']=key
 r=urllib.request.Request(BASE+p,data=data,headers=h,method=m)
 with urllib.request.urlopen(r,timeout=20) as x:return json.loads(x.read())
def main():
 root=tempfile.mkdtemp(prefix='tm-mime-'); os.makedirs(root+'/keys'); env=dict(os.environ,TEMPMAIL_DB=root+'/db.sqlite',TEMPMAIL_KEY_DIR=root+'/keys')
 p=subprocess.Popen([BIN,'--port',str(PORT),'--domain','routerssh.web.id'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 try:
  for _ in range(100):
   try:call('/api/health');break
   except:time.sleep(.05)
  a=call('/api/alias','POST',{'duration':'1h'}); key=a['api_key']; to=a['email']
  fixtures=[]
  # nested multipart/mixed -> multipart/alternative (common attachments + body)
  fixtures.append(('nested', '''MIME-Version: 1.0\r
Content-Type: multipart/mixed; boundary="outer"\r
\r
--outer\r
Content-Type: multipart/alternative; boundary="inner"\r
\r
--inner\r
Content-Type: text/plain; charset=UTF-8\r
Content-Transfer-Encoding: quoted-printable\r
\r
Nested plain OTP 445566\r
--inner\r
Content-Type: text/html; charset=UTF-8\r
Content-Transfer-Encoding: quoted-printable\r
\r
<html><body><b>Nested HTML OTP 445566</b></body></html>\r
--inner--\r
--outer\r
Content-Type: application/octet-stream\r
Content-Transfer-Encoding: base64\r
\r
QUJDRA==\r
--outer--'''))
  # LF-only multipart
  fixtures.append(('lf_only','''MIME-Version: 1.0
Content-Type: multipart/alternative; boundary=lfb

--lfb
Content-Type: text/plain

LF OTP 667788
--lfb
Content-Type: text/html

<html><body>LF HTML 667788</body></html>
--lfb--'''))
  # QP must decode exactly once: =3D41 represents the literal text "=41",
  # not "A" (which a second QP pass would produce).
  fixtures.append(('qp_once','''MIME-Version: 1.0\r
Content-Type: text/plain; charset=UTF-8\r
Content-Transfer-Encoding: quoted-printable\r
\r
Literal token =3D41 and OTP 112233'''))
  # boundary case differs from header case
  fixtures.append(('lower_header','''mime-version: 1.0\r
content-type: multipart/alternative; boundary="low"\r
\r
--low\r
content-type: text/plain\r
\r
Lower OTP 778899\r
--low--'''))
  bad=[]
  for name,raw in fixtures:
   call('/api/incoming','POST',{'from':'mime@example.com','to':to,'subject':name,'body':raw,'html':''})
  msgs=call('/api/messages?full=1',key=key)['emails']
  by={m['subject']:m for m in msgs}
  for name,_ in fixtures:
   m=by[name]; print(name,'text=',repr(m.get('body_text','')[:120]),'html=',repr(m.get('body_html','')[:120]))
  if '445566' not in by['nested'].get('body_text','') or '<html' not in by['nested'].get('body_html','').lower():bad.append('nested multipart lost body/html')
  if '--inner' in by['nested'].get('body_text','') or '--outer' in by['nested'].get('body_html','') or 'application/octet-stream' in by['nested'].get('body_html',''):bad.append('nested multipart framing/attachment leaked into body')
  if '667788' not in by['lf_only'].get('body_text',''):bad.append('LF-only lost')
  if '--lfb' in by['lf_only'].get('body_text',''):bad.append('LF-only multipart framing leaked into text')
  if '=41' not in by['qp_once'].get('body_text','') or 'Literal token A ' in by['qp_once'].get('body_text',''):bad.append('quoted-printable decoded more than once')
  if '778899' not in by['lower_header'].get('body_text',''):bad.append('case-insensitive MIME headers lost')
  print('FINDINGS',json.dumps(bad)); return 1 if bad else 0
 finally:
  p.terminate();
  try:p.wait(5)
  except:p.kill()
  shutil.rmtree(root,ignore_errors=True)
if __name__=='__main__':raise SystemExit(main())
