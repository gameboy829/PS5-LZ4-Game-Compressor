#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import json,os,pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lz4-folder-library-') as tmp:
    base=pathlib.Path(tmp)
    def game(relative,title='PPSA00002',compressed=False):
        folder=base/relative;(folder/'sce_sys').mkdir(parents=True)
        (folder/'eboot.bin').write_bytes(b'synthetic')
        (folder/'sce_sys/param.json').write_text(json.dumps({'titleId':title,'localizedParameters':{'defaultLanguage':'en-US','en-US':{'titleName':'Folder fixture'}}}))
        if compressed:(folder/'ampr_assets.index').write_bytes(b'fixture')
        return folder
    valid=game('game');nested=game('category/nested','PPSA00003')
    game('already-compressed',compressed=True);game('invalid','CUSA00002')
    bad=game('broken-json');(bad/'sce_sys/param.json').write_text('{')
    missing=game('no-eboot');(missing/'eboot.bin').unlink()
    (base/'linked-game').symlink_to(valid,target_is_directory=True)
    badlink=game('linked-eboot');(badlink/'eboot.bin').unlink();(badlink/'eboot.bin').symlink_to(valid/'eboot.bin')
    result=subprocess.run([str(root/'build/library-host')],env={**os.environ,'LZ4_TEST_LIBRARY':str(base)},capture_output=True,text=True,check=True)
    found=json.loads(result.stdout)
    assert {g['path'] for g in found}=={str(valid),str(nested)},found
    assert all(not g['image_backed'] and g['title_name']=='Folder fixture' for g in found)
    print('PASS: folder discovery, nested dumps, localized names; compressed games, malformed metadata, missing executable and links excluded')
