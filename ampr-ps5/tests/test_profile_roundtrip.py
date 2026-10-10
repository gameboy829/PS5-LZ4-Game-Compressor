#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib,tempfile,subprocess,os,sys,tomllib,hashlib
root=pathlib.Path(__file__).resolve().parents[2]
engine=root/'ampr-ps5/build/profile-host'
oracle=root/'ampr-native/reference/external/ampr_emu/tools/ampr_pack.py'
def run(*args,pass_expected=True):
    result=subprocess.run([str(x) for x in args],capture_output=True,text=True)
    assert (result.returncode==0)==pass_expected,(result.returncode,result.stdout[-1000:],result.stderr[-1000:])
    return result
with tempfile.TemporaryDirectory(prefix='lz4-profile-') as tmp:
    tmp=pathlib.Path(tmp);source=tmp/'source';source.mkdir()
    files={'eboot.bin':b'Executable fixture','sce_sys/param.json':b'{"titleId":"PPSA00001"}','assets/repeated':b'asset-block'*50000,'assets/random':os.urandom(200000),'assets/empty':b'','audio/voice':b'audio-block'*30000,'assets/movie':b'video-block'*20000,'assets/data.01':b'numbered'*20000}
    for name,data in files.items():p=source/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    source_hash={name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in files}
    profile=tmp/'generated.toml';index=tmp/'source.index';packed=tmp/'native';decoded=tmp/'decoded'
    run(engine,'profile',source,profile);config=tomllib.loads(profile.read_text());assert config['pack']['compression_level']==7;assert config['groups']['assets']['pack_count']==4
    run(engine,'index',source,index);run(engine,'pack',source,index,packed,profile);run(engine,'verify',packed,source)
    reference=tmp/'python'
    run(sys.executable,oracle,'pack','--root',source,'--ampr-index',index,'--output',reference,'--config',profile)
    sys.path.insert(0,str(oracle.parent));import ampr_pack_format as fmt
    native_manifest=fmt.load_manifest(packed/'ampr_assets.index');python_manifest=fmt.load_manifest(reference/'ampr_assets.index')
    def structure(manifest):
        return [(manifest.file_path(i+1),f.logical_size,f.flags,f.block_shift,f.packing_class) for i,f in enumerate(manifest.files)]
    assert structure(native_manifest)==structure(python_manifest),'Python/native file classification differs'
    run(sys.executable,oracle,'verify','--index',packed/'ampr_assets.index')
    run(sys.executable,oracle,'unpack','--index',packed/'ampr_assets.index','--output',decoded)
    for name,digest in source_hash.items():
        file=decoded/name if (decoded/name).is_file() else packed/name
        assert file.is_file() and hashlib.sha256(file.read_bytes()).hexdigest()==digest,name
        assert hashlib.sha256((source/name).read_bytes()).hexdigest()==digest,'source changed: '+name
    assert os.stat(packed/'eboot.bin').st_mode&0o777==0o755
    assert all(os.stat(p).st_mode&0o444==0o444 for p in packed.glob('*.pak'))
    run(engine,'pack',source,index,packed,profile,pass_expected=False)
    archive=next(packed.glob('*.pak'));original=archive.read_bytes();data=bytearray(original);data[0]^=1;archive.write_bytes(data)
    run(engine,'verify',packed,source,pass_expected=False);archive.write_bytes(original)
    victim=packed/'eboot.bin';victim.write_bytes(b'changed');run(engine,'verify',packed,source,pass_expected=False)
    print('PASS: generated profile, Python/native manifest parity, native pack/verify, licensed Python decoder, file hashes, original immutability, permissions, overwrite refusal and archive/loose-file corruption detection')
