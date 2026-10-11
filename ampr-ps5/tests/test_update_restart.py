#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise real restart helper with local mock services; no console access."""
import pathlib,tempfile,subprocess,json,http.server,threading
root=pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='lz4-update-test-') as tmp:
 folder=pathlib.Path(tmp);payload=folder/'app.elf';autoload=folder/'autoload.txt';exe=folder/'restart'
 subprocess.run(['g++','-std=c++17','-pthread','-DSTUDIO_HOST_TEST',f'-DTEST_UPDATE_ROOT="{folder}"',f'-DTEST_UPDATE_PAYLOAD="{payload}"',f'-DTEST_UPDATE_AUTOLOAD="{autoload}"',str(root/'standalone/update_helper.cpp'),'-o',str(exe)],check=True)
 class App(http.server.BaseHTTPRequestHandler):
  def do_GET(self):
   data=json.dumps({'currentVersion':'0.1.0-beta.3','busy':False}).encode();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
  def log_message(self,*a):pass
 app=None;mode='success';loads=[]
 class PM(http.server.BaseHTTPRequestHandler):
  def do_GET(self):
   global app
   loads.append(self.path)
   if mode=='success':
    app=http.server.ThreadingHTTPServer(('127.0.0.1',15912),App);threading.Thread(target=app.serve_forever,daemon=True).start()
   data=b'OK' if mode=='success' else b'Load failed';self.send_response(200 if mode=='success' else 500);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
  def log_message(self,*a):pass
 pm=http.server.ThreadingHTTPServer(('127.0.0.1',18084),PM);threading.Thread(target=pm.serve_forever,daemon=True).start()
 try:
  (folder/'handoff.json').write_text(json.dumps({'payload':str(payload),'version':'beta.3'}));(folder/'previous.elf').write_bytes(b'old build');payload.write_bytes(b'new build');autoload.write_text('other-payload.elf\nLZ4-Game-Compressor-beta.2.elf\n')
  subprocess.run([str(exe)],check=True,timeout=10);assert payload.read_bytes()==b'new build';assert autoload.read_text()=='other-payload.elf\nLZ4-Game-Compressor.elf\n';assert json.loads((folder/'result.json').read_text())['phase']=='complete'
  app.shutdown();app.server_close();app=None;mode='failure';loads.clear();payload.write_bytes(b'failed build');autoload.write_text('original-autoload\n')
  result=subprocess.run([str(exe)],timeout=10);assert result.returncode==1;assert payload.read_bytes()==b'old build';assert autoload.read_text()=='original-autoload\n';assert len(loads)==2;assert json.loads((folder/'result.json').read_text())['phase']=='error'
  print('PASS: healthy restart preserves new build and unrelated autoload; failed startup restores prior ELF and leaves autoload unchanged')
 finally:
  if app:app.shutdown();app.server_close()
  pm.shutdown();pm.server_close()
