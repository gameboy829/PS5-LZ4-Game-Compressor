#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib,tempfile,subprocess,os,hashlib
root=pathlib.Path(__file__).resolve().parent.parent
exe=root/'build/original-host'
def run(*args,success=True):
    result=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True)
    assert (result.returncode==0)==success,(args,result.stdout,result.stderr)
    return result
run('consent',success=False);run('unverified-consent',success=False);run('confirmed')
with tempfile.TemporaryDirectory(prefix='lz4-original-tests-') as tmp:
    tmp=pathlib.Path(tmp);backup=tmp/'backups';backup.mkdir();image=tmp/'fixture.ffpfsc';data=os.urandom(300000)
    image.write_bytes(data);run('cancel',image,backup,success=False);assert image.read_bytes()==data and not list(backup.iterdir())
    (backup/image.name).write_bytes(b'existing');run('move',image,backup,success=False);assert image.read_bytes()==data and (backup/image.name).read_bytes()==b'existing';(backup/image.name).unlink()
    run('changed',image,backup,success=False);assert image.exists() and not list(backup.iterdir())
    image.write_bytes(data);run('move',image,backup);assert not image.exists() and (backup/image.name).read_bytes()==data
    source=tmp/'delete-fixture.ffpfsc';source.write_bytes(data);run('delete',source,backup);assert not source.exists()
    link=tmp/'linked.ffpfsc';link.symlink_to(backup/image.name);run('delete',link,backup,success=False);assert (backup/image.name).read_bytes()==data
    if pathlib.Path('/dev/shm').is_dir() and os.stat('/dev/shm').st_dev!=os.stat(tmp).st_dev:
        with tempfile.TemporaryDirectory(prefix='lz4-backup-',dir='/dev/shm') as target:
            source.write_bytes(data);result=run('move',source,target);assert 'Verifying original backup' in result.stdout
            assert not source.exists() and hashlib.sha256((pathlib.Path(target)/source.name).read_bytes()).digest()==hashlib.sha256(data).digest()
            assert not list(pathlib.Path(target).glob('*.lz4-backup-partial'))
    if pathlib.Path('/dev/shm').is_dir() and os.stat('/dev/shm').st_dev!=os.stat(tmp).st_dev:
        for command in ['swap','swap-cancel','swap-changed']:
            with tempfile.TemporaryDirectory(prefix='lz4-swap-',dir='/dev/shm') as target:
                working=pathlib.Path(target)/'converted';working.mkdir();(working/'data').write_bytes(data)
                returned=tmp/command;returned.mkdir();(returned/'data').write_bytes(data)
                source.write_bytes(data)
                run(command,working,returned,source,target,success=command=='swap')
                assert (returned/'data').read_bytes()==data and (pathlib.Path(target)/source.name).read_bytes()==data and not source.exists()
                assert working.exists()==(command!='swap')
                if command=='swap-changed':assert (pathlib.Path(str(working)+'-saved')/'data').read_bytes()==data
    print('PASS: deletion consent, extra unverified consent, cancellation, changed image, occupied backup, symlink rejection, same-drive move and verified cross-drive backup')
