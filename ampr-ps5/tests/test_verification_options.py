#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
exe=root/'ampr-ps5/build/production-host'
runtime=root/'ampr-native/resources/fakelib/libSceAmpr.sprx'
def run(*args):
    result=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True)
    assert result.returncode==0,(result.stdout[-1000:],result.stderr)
    return result.stdout
with tempfile.TemporaryDirectory(prefix='lz4-verify-options-') as tmp:
    tmp=pathlib.Path(tmp);source=tmp/'source';(source/'assets').mkdir(parents=True);(source/'assets/content').write_bytes(b'fixture'*100000);(source/'eboot.bin').write_bytes(b'fixture executable')
    full=tmp/'full';skipped=tmp/'skipped'
    log=run('game',source,full,runtime,tmp/'profiles');assert 'Native verification passed' in log and not (full/'UNVERIFIED.txt').exists()
    log=run('game-skip',source,skipped,runtime,tmp/'profiles');assert 'Native verification passed' not in log and (skipped/'UNVERIFIED.txt').exists()
    import hashlib
    for output in [full,skipped]:
        installed=output/'fakelib/libSceAmpr.sprx'
        assert installed.read_bytes()==runtime.read_bytes()
        assert installed.stat().st_mode&0o777==0o755
    original=(full/'fakelib/libSceAmpr.sprx').read_bytes()
    (full/'fakelib/libSceAmpr.sprx').unlink()
    probe=subprocess.run([str(root/'ampr-ps5/build/profile-host'),'verify-required',str(full),str(source)],capture_output=True,text=True)
    assert probe.returncode!=0 and 'runtime missing' in probe.stderr.lower()
    # Corrupt deployment runtime must also fail.
    (full/'fakelib/libSceAmpr.sprx').write_bytes(b'corrupt')
    probe=subprocess.run([str(root/'ampr-ps5/build/profile-host'),'verify-required',str(full),str(source)],capture_output=True,text=True)
    assert probe.returncode!=0 and 'runtime' in probe.stderr.lower()
    (full/'fakelib/libSceAmpr.sprx').write_bytes(original);(full/'fakelib/libSceAmpr.sprx').chmod(0o755)
    copied=tmp/'copy-skip';log=run('copy-skip',full,copied);assert (copied/'UNVERIFIED.txt').exists() and (copied/'eboot.bin').read_bytes()==(full/'eboot.bin').read_bytes()
    verifiedCopy=tmp/'copy-full';run('copy',full,verifiedCopy);assert not (verifiedCopy/'UNVERIFIED.txt').exists()
    print('PASS: full verification remains default; pack/copy skip options omit checks and persist UNVERIFIED notice')
