#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Host integration checks; never contacts a console or modifies game files."""
import pathlib,json,threading,subprocess,time,urllib.request,urllib.error,http.server,socket,tempfile
root=pathlib.Path(__file__).resolve().parent.parent
fixture=tempfile.TemporaryDirectory(prefix='lz4-api-source-')
calls=[]
game={'title_id':'PPSA00001','title_name':'Synthetic fixture','path':fixture.name+'/fixture.ffpfsc','runtime_path':fixture.name,'managed':True,'source_available':True,'mounted':True,'image_backed':True}
class Shadow(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        calls.append((self.path,body))
        data=json.dumps({'status':0,'games':[game]} if self.path.endswith('/games') else {'status':0}).encode()
        self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*args):pass
mock=http.server.ThreadingHTTPServer(('127.0.0.1',10102),Shadow)
threading.Thread(target=mock.serve_forever,daemon=True).start()
service=subprocess.Popen([str(root/'build/host-service')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def request(path,method='GET'):
    req=urllib.request.Request('http://127.0.0.1:15911'+path,method=method)
    try:
        with urllib.request.urlopen(req,timeout=10) as r:return r.status,r.read()
    except urllib.error.HTTPError as e:return e.code,e.read()
def api(path,method='GET',ok=True):
    code,data=request(path,method);j=json.loads(data);assert j['ok']==ok,(code,j);assert (code==200)==ok,(code,j);return j
try:
    for _ in range(100):
        try:api('/api/gc/ampr-lz4/status');break
        except (OSError,urllib.error.URLError):time.sleep(.05)
    else:raise AssertionError('Service did not start')
    assert request('/')[0]==200
    assert b'Controller' in request('/')[1]
    assert b'beta.2' in request('/')[1] and b'Conversion options' in request('/')[1]
    assert api('/api/gc/games')['games'][0]['titleId']=='PPSA00001'
    query=urllib.parse.urlencode({'titleId':'PPSA00001','sourcePath':game['path'],'output':'/data/homebrew/fixture-output','level':'7','mode':'internal'})
    p=api('/api/gc/ampr-lz4/plan?'+query);assert not p['canStart'] and not p['readOnlyVerified']
    api('/api/gc/ampr-lz4/plan?'+query+'&verifyOutput=invalid',ok=False)
    api('/api/gc/ampr-lz4/plan?'+query+'&originalAction=invalid',ok=False)
    api('/api/gc/ampr-lz4/plan?'+query+'&originalAction=swap',ok=False)
    api('/api/gc/ampr-lz4/plan?'+query.replace('mode=internal','mode=swap')+'&originalAction=swap&verifyCopy=no',ok=False)
    api('/api/gc/ampr-lz4/start?'+query,'POST',False)
    api('/api/gc/ampr-lz4/mount?'+query,'GET',False)
    api('/api/gc/ampr-lz4/mount?'+query,'POST')
    assert [x[0] for x in calls[-2:]]==['/api/v1/games/unmount','/api/v1/games/mount']
    assert calls[-1][1]['mode']=='ro'
    settings=api('/api/gc/ampr-lz4/threads');assert settings['threadLimit']==8
    api('/api/gc/ampr-lz4/threads?packReaders=2&packWorkers=4&packWriters=2','POST',False)
    api('/api/gc/ampr-lz4/threads?packReaders=1&packWorkers=4&packWriters=2','POST')
    assert api('/api/gc/ampr-lz4/threads')['packing']['workers']==4
    api('/api/gc/ampr-lz4/threads?packReaders=1&packReaders=2','POST',False)
    api('/api/gc/ampr-lz4/threads?packReaders=%00','POST',False)
    api('/api/gc/job/cancel','GET',False)
    api('/api/gc/job/cancel','POST')
    assert not api('/api/gc/ampr-lz4/status')['active']
    assert request('/ampr-runtime.sprx')[0]==404
    assert request('/../src/ampr_backend.cpp')[0]==404
    assert request('/api/nonexistent')[0]==400
    api('/api/gc/ampr-lz4/delete-partial','GET',False)
    api('/api/gc/ampr-lz4/delete-partial','POST')
    print('PASS: service assets, discovery, mount order, read-only gate, worker limit, method checks, malformed queries, cleanup and status')
finally:
    service.terminate();service.wait(timeout=5);mock.shutdown();fixture.cleanup()
