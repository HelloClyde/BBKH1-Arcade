"""Verify the two pinned, user-supplied BIOS archives without network access."""
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import stat
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = 'assets/bios/manifest.json'
ARCHIVES = ('pgm.zip', 'neogeo.zip')
BIOS_NOTICE = '''# 随包 BIOS

安装包已包含 ARCADE/pgm.zip 和 ARCADE/neogeo.zip，解压后随 ARCADE 目录复制到 H1 的 A 盘根目录即可。
这两个原始 ZIP 由用户提供，并依用户明确的发布要求随本版安装包及后续 CI 构建分发；不需要从网络下载，也不需要解压 BIOS ZIP。
BIOS-MANIFEST.json 记录原始档案与每个成员的大小、SHA256 和 CRC32，用于离线校验。
这些 BIOS 的原版权与原适用条款保留，不重新许可为 GPL；该清单不代表已经独立确认 BIOS 的再分发许可。
游戏 ROM 和个人存档不随包提供。请自行放入自己的 kof97.zip 或 kov.zip，然后在模拟器中选择游戏 ZIP。
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_relative(name, basename=False):
    assert isinstance(name, str) and name and '\\' not in name and ':' not in name and '\0' not in name, name
    parts = name.split('/')
    assert all(part not in ('', '.', '..') and part == part.rstrip(' .') for part in parts), name
    path = PurePosixPath(name)
    assert not path.is_absolute() and path.as_posix() == name, name
    assert not basename or len(parts) == 1, name
    return name


def validate_archive(data, spec):
    """Check archive identity first, then every member and its ZIP CRC."""
    assert isinstance(spec['size'], int) and spec['size'] > 0
    assert re.fullmatch('[0-9a-f]{64}', spec['sha256'])
    assert len(data) == spec['size'], 'BIOS archive size mismatch'
    assert sha(data) == spec['sha256'], 'BIOS archive SHA256 mismatch'
    expected = spec['members']
    assert isinstance(expected, dict) and expected, 'Empty BIOS member manifest'
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        assert len(names) == len(set(names)) == len({name.casefold() for name in names}), 'Duplicate BIOS members'
        assert set(names) == set(expected), 'BIOS member set mismatch'
        for entry in entries:
            safe_relative(entry.filename, basename=True)
            assert not entry.is_dir() and not stat.S_ISLNK(entry.external_attr >> 16), entry.filename
            assert not entry.flag_bits & 1, 'Encrypted BIOS archive'
            member = expected[entry.filename]
            assert set(member) == {'size', 'crc32', 'sha256'}, entry.filename
            assert isinstance(member['size'], int) and member['size'] > 0, entry.filename
            assert re.fullmatch('[0-9a-f]{8}', member['crc32']), entry.filename
            assert re.fullmatch('[0-9a-f]{64}', member['sha256']), entry.filename
            assert entry.file_size == member['size'], entry.filename
            assert f'{entry.CRC:08x}' == member['crc32'], entry.filename
            body = archive.read(entry)
            assert len(body) == member['size'] and sha(body) == member['sha256'], entry.filename
        assert archive.testzip() is None, 'BIOS ZIP CRC failure'


def load_bios(root=ROOT):
    """Return installation target -> original ZIP bytes and the pinned manifest."""
    root = Path(root).resolve()
    manifest = json.loads((root / MANIFEST_PATH).read_text(encoding='utf-8'))
    assert set(manifest) == {'format_version', 'files', 'provenance'}
    assert manifest['format_version'] == 1
    assert set(manifest['files']) == set(ARCHIVES), 'Both pinned BIOS archives are required'
    provenance = manifest['provenance']
    assert provenance['kind'] == 'user_supplied'
    assert provenance['date'] == '2026-10-21'
    assert provenance['independent_redistribution_license_verified'] is False
    assert isinstance(provenance['authorization'], str) and provenance['authorization'].strip()
    assert isinstance(provenance['copyright'], str) and provenance['copyright'].strip()
    blobs = {}
    for name in ARCHIVES:
        safe_relative(name, basename=True)
        spec = manifest['files'][name]
        assert set(spec) == {'source_path', 'install_target', 'size', 'sha256', 'members'}, name
        assert spec['source_path'] == f'assets/bios/{name}', name
        assert spec['install_target'] == f'ARCADE/{name}', name
        path = root / safe_relative(spec['source_path'])
        assert path.resolve().is_relative_to(root) and not path.is_symlink(), name
        data = path.read_bytes()
        validate_archive(data, spec)
        blobs[spec['install_target']] = data
    return blobs, manifest


def bios_install_entries(root=ROOT):
    """The exact additional installer inputs, shared with the packager and tests."""
    entries, manifest = load_bios(root)
    entries['BIOS-MANIFEST.json'] = (Path(root) / MANIFEST_PATH).read_bytes()
    entries['BIOS说明.md'] = BIOS_NOTICE.encode('utf-8')
    return entries, manifest


def main():
    entries, manifest = bios_install_entries()
    report = {
        'ok': True,
        'manifest': MANIFEST_PATH,
        'manifest_sha256': sha((ROOT / MANIFEST_PATH).read_bytes()),
        'BIOS_distributed': True,
        'game_ROM_distributed': False,
        'files': {name: {key: spec[key] for key in ('source_path', 'install_target', 'size', 'sha256')}
                  for name, spec in manifest['files'].items()},
        'member_count': sum(len(spec['members']) for spec in manifest['files'].values()),
        'installer_inputs': {name: sha(data) for name, data in entries.items()},
        'verification': 'Offline archive SHA256, exact member sets, member SHA256/size/CRC32, ZIP CRC and safe paths.'
    }
    out = ROOT / 'build/verification/bios-verification.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
