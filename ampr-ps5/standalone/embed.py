#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib, mimetypes
root=pathlib.Path(__file__).resolve().parent.parent
names=('ampr-studio.html','ampr-studio.css','ampr-studio.js','ampr-tv.js','ampr-sync.js','ampr-progress.js','ampr-diagnostics.js','ampr-update.js','lz4-logo.svg','ampr-runtime.sprx','update-helper.elf')
lines=['// Generated from application assets.', 'static constexpr const char* APP_VERSION="'+(root.parent/'VERSION').read_text().strip()+'";','struct EmbeddedAsset {const char* name;const char* mime;const unsigned char* data;size_t size;};']
for i,name in enumerate(names):
    data=(root/'build/update-helper.elf' if name=='update-helper.elf' else root/'assets'/name).read_bytes()
    if name.endswith('.html'):data=data.replace(b'@APP_VERSION@',(root.parent/'VERSION').read_text().strip().encode())
    lines.append(f'static const unsigned char asset{i}[]={{'+','.join(str(n) for n in data)+'};')
lines.append('static const EmbeddedAsset embeddedAssets[]={')
for i,name in enumerate(names):
    mime={'js':'application/javascript','sprx':'application/octet-stream'}.get(name.rsplit('.',1)[-1],mimetypes.guess_type(name)[0] or 'application/octet-stream')
    lines.append(f'{{"{name}","{mime}",asset{i},sizeof(asset{i})}},')
lines.append('};')
(root/'standalone/assets.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
launcher=['// Generated application-owned launcher assets.','#include <cstddef>','struct LauncherAsset {const char* name;const unsigned char* data;size_t size;};']
for i,name in enumerate(('param.json','icon0.png')):
    data=(root/'assets-app'/name).read_bytes()
    launcher.append(f'static const unsigned char launcher{i}[]={{'+','.join(str(n) for n in data)+'};')
launcher.append('static const LauncherAsset launcherAssets[]={{"param.json",launcher0,sizeof(launcher0)},{"icon0.png",launcher1,sizeof(launcher1)}};')
(root/'standalone/launcher-assets.h').write_text('\n'.join(launcher)+'\n',encoding='utf-8')

# Public Mozilla trust certificates, distributed by curl.se/ca.
import re,ssl
certs=re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",(root/'assets/github-ca.pem').read_text(),re.S)
ca=['// Generated public trust-anchor data.','#pragma once','struct UpdateCa {const void* data;size_t size;};']
for i,cert in enumerate(certs):
    data=(cert+'\n').encode()
    ca.append('static const unsigned char updateCa'+str(i)+'[]={'+','.join(str(n) for n in data)+'};')
ca.append('static const UpdateCa updateCas[]={'+','.join('{updateCa'+str(i)+',sizeof(updateCa'+str(i)+')}' for i in range(len(certs)))+'};')
(root/'standalone/update-ca.h').write_text('\n'.join(ca)+'\n',encoding='utf-8')
