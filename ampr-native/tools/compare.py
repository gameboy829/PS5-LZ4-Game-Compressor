"""GPL-3.0-or-later. Strict streaming semantic comparison using the pinned oracle.
Exact archive-byte parity is separately reported, and can be required.
"""
from pathlib import Path
import argparse, ctypes, hashlib, json, sys
BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BASE / 'reference/external/ampr_emu/tools'))
import ampr_pack as oracle
import ampr_pack_format as fmt

def codec():
    # Both implementations deliberately use the very same bundled LZ4 binary.
    original = ctypes.util.find_library
    ctypes.util.find_library = lambda name: str(BASE / 'bin/lz4.dll') if name == 'lz4' else original(name)
    c = fmt.Lz4Codec()
    c._py = None
    return c

def digest(chunks):
    h = hashlib.sha256()
    for block in chunks:
        h.update(block)
    return h.hexdigest()

def file_blocks(path):
    with path.open('rb') as f:
        while block := f.read(1024 * 1024):
            yield block

def inspect(root, source):
    index = root / 'ampr_assets.index'
    manifest = fmt.load_manifest(index)
    fmt.load_chunk_crcs(fmt.chunk_crc_path(index), manifest.build_id, len(manifest.chunks))
    entries = fmt.read_ampridx3(root / 'ampr_emu.index')
    if len(entries) != len(manifest.files):
        raise ValueError('source index count differs')
    items = []
    expected = {'ampr_assets.index', 'ampr_assets.index.crc', 'ampr_emu.index'}
    expected.update(manifest.pack_name(i) for i in range(len(manifest.packs)))
    with oracle.PackReader(manifest) as reader:
        for file_id, (record, entry) in enumerate(zip(manifest.files, entries), 1):
            path = manifest.file_path(file_id)
            if path != entry.path or record.logical_size != entry.size:
                raise ValueError(f'file ID {file_id} differs from source index')
            relative = fmt.asset_relative_path(path)
            if record.flags & fmt.FILE_FLAG_PACKED:
                hashed = digest(reader.iter_file_data(file_id))
            else:
                expected.add(relative)
                loose = fmt.safe_output_path(root, relative)
                if loose.stat().st_size != record.logical_size:
                    raise ValueError('loose size differs')
                hashed = digest(file_blocks(loose))
            src = fmt.safe_output_path(source, relative)
            if src.stat().st_size != record.logical_size or digest(file_blocks(src)) != hashed:
                raise ValueError(f'source content mismatch: {relative}')
            items.append({'id': file_id, 'path': path, 'size': record.logical_size,
                          'sha256': hashed, 'flags': record.flags,
                          'block_shift': record.block_shift, 'mtime': record.mtime,
                          'packing_class': record.packing_class})
    actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    if actual != expected:
        raise ValueError(f'missing/extra output files: missing={sorted(expected-actual)}, extra={sorted(actual-expected)}')
    return items, actual

def compare(left, right, source, require_exact=False):
    codec()
    a, af = inspect(left, source)
    b, bf = inspect(right, source)
    if a != b or af != bf:
        raise ValueError('semantic structure/content mismatch')
    differences = [name for name in sorted(af) if digest(file_blocks(left/name)) != digest(file_blocks(right/name))]
    result = {'semantic_equal': True, 'exact_archive_bytes_equal': not differences,
              'byte_differences': differences, 'files': a}
    if require_exact and differences:
        raise ValueError(f'exact byte mismatch: {differences}')
    return result

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('native', type=Path)
    p.add_argument('reference', type=Path)
    p.add_argument('source', type=Path)
    p.add_argument('--require-exact', action='store_true')
    args = p.parse_args()
    try:
        print(json.dumps(compare(args.native, args.reference, args.source, args.require_exact), indent=2))
    except Exception as exc:
        print(f'COMPARISON FAILED: {exc}', file=sys.stderr)
        sys.exit(1)
