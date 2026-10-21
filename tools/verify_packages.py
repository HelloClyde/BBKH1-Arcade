"""Verify downloaded release assets with only the Python standard library."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import stat
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RELEASE_EVIDENCE = 'docs/evidence/v0.2.0-release-verification.json'
PHYSICAL_EVIDENCE = 'docs/evidence/pgm-stream-hardware-20261021.json'
TESTED_BDA_SHA256 = '0f3bfd95fe53f331308102a34a64be0218b39054871d73e2af1a72cbae39b2e0'
TESTED_BDA_SIZE = 1295948


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_relative(name, basename=False):
    """Accept one canonical portable ZIP/repository path, never a drive or traversal."""
    assert isinstance(name, str) and name and '\\' not in name and ':' not in name and '\0' not in name, name
    parts = name.split('/')
    assert all(part not in ('', '.', '..') and part == part.rstrip(' .') for part in parts), name
    path = PurePosixPath(name)
    assert not path.is_absolute() and path.as_posix() == name, name
    assert not basename or len(parts) == 1, name
    return name


def checked_members(archive):
    entries = archive.infolist()
    names = [entry.filename for entry in entries]
    assert len(names) == len(set(names)) == len({name.casefold() for name in names}), 'Duplicate archive members'
    for entry in entries:
        safe_relative(entry.filename)
        assert not entry.is_dir() and not stat.S_ISLNK(entry.external_attr >> 16), entry.filename
    assert not archive.testzip(), 'ZIP CRC failure'
    return set(names)


def load_release_evidence(root=ROOT):
    record = json.loads((root / RELEASE_EVIDENCE).read_text(encoding='utf-8'))
    assert record['format_version'] == 1 and record['release_version'] == '0.2.0'
    assert record['bda_sha256'] == TESTED_BDA_SHA256 and record['bda_size'] == TESTED_BDA_SIZE
    assert record['physical_evidence']['path'] == PHYSICAL_EVIDENCE
    assert record['physical_evidence']['tested_bda_sha256'] == TESTED_BDA_SHA256
    raw = (root / safe_relative(record['physical_evidence']['path'])).read_bytes()
    assert sha(raw) == record['physical_evidence']['sha256'], 'Physical evidence changed'
    physical = json.loads(raw)
    assert physical['hardware_verified'] is True
    assert physical['bda_sha256'] == TESTED_BDA_SHA256 and physical['bda_size'] == TESTED_BDA_SIZE
    assert physical['deployment']['completed'] and physical['deployment']['upload']['device_crc_verified']
    assert physical['deployment']['upload']['sha256'] == TESTED_BDA_SHA256
    assert physical['native']['bda_sha256'] == TESTED_BDA_SHA256
    assert physical['native']['state']['state'] == 3 and physical['native']['state']['error'] == 0
    for key, cases, case_index, error_index in (
        ('sprite', 3072, 2, 3), ('copy', 256, 2, 3), ('bus', 1024, 2, 3),
        ('idle', 1024, 5, 6), ('z80_hle', 2048, 5, 6)
    ):
        result = physical['native'][key]
        assert result[case_index] == cases and result[error_index] == 0, key
    for key, cases, case_index, error_index in (
        ('branch', 1024, 5, 6), ('banking', 352, 3, 4), ('bg', 512, 2, 3),
        ('dbra', 1028, 6, 7), ('audio_fast', 404, 2, 3)
    ):
        result = physical['additional_native'][key]
        assert result[case_index] == cases and result[error_index] == 0, key
    frozen = physical['frozen_pipeline']
    assert frozen['bda_sha256'] == TESTED_BDA_SHA256 and frozen['state'] == 2
    assert frozen['loops'] == 96 and frozen['prepare_failures'] == 0
    assert all(frozen[variant + '_diff_' + mode] == 0
               for variant in ('full', 'stream_c', 'stream_asm') for mode in ('4_3', 'raw', 'full'))
    for key, profiled in (('profiled_player_actions', True), ('unprofiled_player_actions', False)):
        window = physical[key]
        assert window['bda_sha256'] == TESTED_BDA_SHA256 and window['profile'] is profiled
        assert window['device_seconds'] >= 20 and window['logic_fps'] >= 59.5 and window['display_fps'] >= 29.5
        assert window['start']['error'] == window['end']['error'] == 0
        assert window['stream_after']['fallbacks'] == window['stream_before']['fallbacks']
    assert physical['settings']['cpu_mhz'] == 408 and physical['settings']['skip'] == 1
    assert physical['settings']['synthesis_hz'] == 8000
    assert physical['exit']['desktop']['menu_idle'] == 1 and physical['exit']['desktop']['error'] == 0
    assert physical['exit']['clock'][2:4] == [2, 0]
    assert record['sdk_commit'] == record['dependencies']['h1_sdk']['commit']
    for name, digest in record['runtime_source_sha256'].items():
        safe_relative(name)
        assert name.startswith(('src/', 'tools/')) and re.fullmatch('[0-9a-f]{64}', digest), name
    return record, physical


def verify(directory, tag=None, commit=None):
    directory = directory.resolve()
    info = json.loads((directory / 'BUILD-INFO.json').read_text(encoding='utf-8'))
    record, physical = load_release_evidence()
    assert info['version'] == record['release_version']
    if tag:
        assert info['tag'] == tag and tag == 'v' + info['version']
    if commit:
        assert info['application_commit'] == commit
    names = {safe_relative(info[key]['filename'], basename=True) for key in ('bda', 'installer', 'source')} | {'BUILD-INFO.json'}
    assert len(names) == 4
    assert {path.name for path in directory.iterdir()} == names | {'SHA256SUMS.txt', 'RELEASE-NOTES.md'}, 'Unexpected release files'
    assert all(path.is_file() and not path.is_symlink() for path in directory.iterdir()), 'Non-file release entry'
    sums = {}
    for row in (directory / 'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
        digest, name = row.split('  ', 1)
        safe_relative(name, basename=True)
        assert name not in sums and re.fullmatch('[0-9a-f]{64}', digest), name
        path = directory / name
        assert path.resolve().is_relative_to(directory), name
        sums[name] = digest
        assert sha(path.read_bytes()) == digest, name
    assert set(sums) == names
    for key in ('bda', 'installer', 'source'):
        assert sums[info[key]['filename']] == info[key]['sha256'], key
    bda = (directory / info['bda']['filename']).read_bytes()
    assert sha(bda) == info['bda']['sha256'] == record['bda_sha256']
    assert len(bda) == info['bda']['size'] == record['bda_size']
    # Both evidence references are declared by BUILD-INFO and checked against the fixed record.
    assert safe_relative(info['bda']['evidence']) == RELEASE_EVIDENCE
    assert safe_relative(info['bda']['physical_evidence']) == record['physical_evidence']['path']
    check = info['verification']
    assert check['ok'] and check['version'] == info['version']
    assert check['sha256'] == sha(bda) and check['payload_sha256'] == record['payload_sha256']
    assert check['exact_physical_bda_match'] and check['runtime_source_match']
    assert check['release_evidence'] == info['bda']['evidence']
    assert check['physical_evidence'] == info['bda']['physical_evidence']
    assert check['pgm_hardware_verified'] is True and check['neogeo_gameplay_regression_rerun'] is False
    assert check['sdk_commit'] == record['sdk_commit'] and check['validation_scope'] == record['verification_scope']
    assert info['dependencies'] == record['dependencies']
    assert not info['ROM_or_commercial_BIOS_distributed']
    with zipfile.ZipFile(directory / info['installer']['filename']) as archive:
        members = checked_members(archive)
        manifest = json.loads(archive.read('INSTALL-MANIFEST.json'))
        assert members == set(manifest['files']) | {'INSTALL-MANIFEST.json'}
        assert manifest['commit'] == info['application_commit'] and manifest['version'] == info['version']
        for name, digest in manifest['files'].items():
            safe_relative(name)
            assert sha(archive.read(name)) == digest, name
        assert archive.read('应用/程序/H1Arcade.bda') == bda
        assert archive.read('PC工具/gfx.dll')[:2] == b'MZ'
        with zipfile.ZipFile(io.BytesIO(archive.read('PC工具/gngeo_data.zip'))) as drivers:
            driver_names = checked_members(drivers)
            assert len(driver_names) == 11 and all(name.endswith('.drv') for name in driver_names)
    with zipfile.ZipFile(directory / info['source']['filename']) as archive:
        members = checked_members(archive)
        manifests = [name for name in members if name.endswith('/SOURCE-MANIFEST.json')]
        assert len(manifests) == 1, 'Missing or ambiguous source manifest'
        manifest_name = manifests[0]
        prefix = manifest_name[:-len('SOURCE-MANIFEST.json')]
        manifest = json.loads(archive.read(manifest_name))
        assert members == {prefix + name for name in manifest['files']} | {manifest_name}
        assert manifest['commit'] == info['application_commit'] and manifest['tag'] == info['tag']
        assert manifest['version'] == info['version'] and manifest['submodules_included']
        for name, digest in manifest['files'].items():
            safe_relative(name)
            assert sha(archive.read(prefix + name)) == digest, name
        for name in (info['bda']['evidence'], info['bda']['physical_evidence']):
            assert archive.read(prefix + name) == (ROOT / name).read_bytes(), name
        assert json.loads(archive.read(prefix + 'UPSTREAM.json')) == record['dependencies']
        assert archive.read(prefix + 'VERSION').decode().strip() == info['version']
        for name, digest in record['runtime_source_sha256'].items():
            assert sha(archive.read(prefix + name)) == digest, name
        assert sha(archive.read(prefix + 'assets/icon.png')) == record['icon_sha256']
        upstream = json.loads(archive.read(prefix + 'UPSTREAM-SOURCE-MANIFEST.json'))
        for key, dependency in info['dependencies'].items():
            snapshot = upstream['dependencies'][key]
            assert snapshot['commit'] == dependency['commit'] and snapshot['path'] == dependency['path']
            assert snapshot['files']
            dependency_prefix = safe_relative(dependency['path']) + '/'
            for name, digest in snapshot['files'].items():
                safe_relative(name)
                assert name.startswith(dependency_prefix), name
                assert sha(archive.read(prefix + name)) == digest, name
        assert not any(name.lower().endswith(('.bda', '.dll', '.exe', '.bin', '.elf', '.o', '.h1neo', '.h1pgm', '.rom', '.raw', '.nand')) for name in members)
    return {'ok': True, 'version': info['version'], 'commit': info['application_commit'],
            'bda_sha256': sha(bda), 'assets': sorted(names), 'complete_source_snapshots': True,
            'release_evidence': info['bda']['evidence'], 'physical_evidence': info['bda']['physical_evidence'],
            'pgm_hardware_verified': physical['hardware_verified'], 'neogeo_gameplay_regression_rerun': False}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--directory', type=Path, default=Path('dist/release'))
    ap.add_argument('--tag')
    ap.add_argument('--commit')
    args = ap.parse_args()
    print(json.dumps(verify(args.directory, args.tag, args.commit), indent=2))


if __name__ == '__main__':
    main()
