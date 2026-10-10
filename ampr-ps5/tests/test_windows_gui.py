#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows native GUI packing and full-data comparison regression."""
from pathlib import Path
import subprocess,tempfile,shutil,os
root=Path(__file__).resolve().parents[2]
gui=root/'ampr-native/bin/ampr-gui-beta.exe'
def run(args,success=True,skip=False):
    environment=os.environ.copy()
    if skip:environment['AMPR_TEST_SKIP_VERIFY']='1'
    result=subprocess.run([str(gui),*map(str,args)],timeout=120,env=environment)
    assert (result.returncode==0)==success,(args,result.returncode)
with tempfile.TemporaryDirectory(prefix='lz4-windows-gui-') as temporary:
    temporary=Path(temporary);source=temporary/'source';source.mkdir()
    for name,data in {'eboot.bin':b'executable-fixture','assets/repeated':b'game-asset'*60000,'assets/random':os.urandom(90000),'sce_sys/param.json':b'{"titleId":"PPSA00001"}'}.items():
        path=source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    runtime=source/'fakelib/libSceAmpr.sprx';runtime.parent.mkdir();shutil.copyfile(root/'ampr-native/resources/fakelib/libSceAmpr.sprx',runtime)
    native=temporary/'native';reference=temporary/'reference'
    run(['--self-test',source,'',native]);shutil.copytree(native,reference)
    run(['--test-compare',source,native,reference,'exact'])
    victim=reference/'eboot.bin';original=victim.read_bytes();victim.write_bytes(b'corrupted')
    run(['--test-compare',source,native,reference,'content'],False)
    victim.write_bytes(original);victim.unlink()
    run(['--test-compare',source,native,reference,'content'],False)
    victim.write_bytes(original);(reference/'unexpected.bin').write_bytes(b'extra')
    run(['--test-compare',source,native,reference,'content'],False)
    skipped=temporary/'skipped';run(['--self-test',source,'',skipped],skip=True);assert (skipped/'UNVERIFIED.txt').is_file()
    copied=temporary/'skipped-reference';shutil.copytree(skipped,copied);run(['--test-compare',source,skipped,copied,'content'])
    print('PASS: Windows GUI packing, exact comparison, corrupt/missing/extra rejection, skipped verification notice and independent comparison of unverified output')
