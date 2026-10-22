"""Verify downloaded release assets with only the Python standard library."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import stat
import zipfile
from release_bios import ARCHIVES, MANIFEST_PATH, bios_install_entries, validate_archive

ROOT = Path(__file__).resolve().parents[1]
RELEASE_EVIDENCE = 'docs/evidence/v0.2.2-release-verification.json'
INSTALLATION_EVIDENCE = 'docs/evidence/arcade-startup-installation-20261022.json'
TESTED_BDA_SHA256 = '4663b6c25059c31b5a05820292d1fc6572235a2a965023200b09a24a2c34cae2'
TESTED_BDA_SIZE = 1240380
HISTORICAL_EVIDENCE = 'docs/evidence/pgm-stream-hardware-20261021.json'
HISTORICAL_EVIDENCE_SHA256 = '826d2200b8b7a5d569dd0ff9d8efb7707db991b37c663271991dc92c8edbc5d6'


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
    assert record['format_version'] == 1 and record['release_version'] == '0.2.2'
    assert record['bda_sha256'] == TESTED_BDA_SHA256 and record['bda_size'] == TESTED_BDA_SIZE
    assert 'physical_evidence' not in record, 'Current release records installation, not runtime acceptance'
    assert record['installation_evidence']['path'] == INSTALLATION_EVIDENCE
    assert record['installation_evidence']['installed_bda_sha256'] == TESTED_BDA_SHA256
    raw = (root / safe_relative(record['installation_evidence']['path'])).read_bytes()
    assert sha(raw) == record['installation_evidence']['sha256'], 'Installation evidence changed'
    installation = json.loads(raw)
    assert installation['format_version'] == 1
    assert installation['installation_verified'] is True
    assert installation['runtime_verified'] is False and installation['hardware_verified'] is False
    assert installation['user_runtime_validation_pending'] is True
    assert installation['bda_sha256'] == TESTED_BDA_SHA256 and installation['bda_size'] == TESTED_BDA_SIZE
    assert installation['deployment']['completed'] and installation['deployment']['upload']['device_crc_verified']
    assert installation['deployment']['upload']['sha256'] == TESTED_BDA_SHA256
    assert installation['deployment']['run_requested'] is False
    assert installation['deployment']['application_started'] is False
    assert installation['build_options'] == {'cpu_selftest': True, 'startup_selftest': False}
    assert record['build_option_values']['cpu_selftest'] is True
    assert record['build_option_values']['startup_selftest'] is False
    assert installation['desktop_after']['menu_idle'] == 1 and installation['desktop_after']['error'] == 0
    # Earlier CPU/MXU checks and the 60/30 FPS window describe the older BDA only.
    historical = installation['historical_native_evidence']
    assert historical['path'] == HISTORICAL_EVIDENCE
    assert historical['sha256'] == HISTORICAL_EVIDENCE_SHA256
    assert historical['applies_to_current_bda'] is False
    blob = (root / safe_relative(historical['path'])).read_bytes()
    assert sha(blob) == historical['sha256'], 'Historical evidence changed'
    old = json.loads(blob)
    assert old['hardware_verified'] is True and old['bda_sha256'] != TESTED_BDA_SHA256
    assert record['sdk_commit'] == record['dependencies']['h1_sdk']['commit']
    for name, digest in record['runtime_source_sha256'].items():
        safe_relative(name)
        assert name.startswith(('src/', 'tools/')) and re.fullmatch('[0-9a-f]{64}', digest), name
    return record, installation


def verify(directory, tag=None, commit=None):
    directory = directory.resolve()
    info = json.loads((directory / 'BUILD-INFO.json').read_text(encoding='utf-8'))
    record, installation = load_release_evidence()
    bios_entries, bios_manifest = bios_install_entries()
    assert info['version'] == record['release_version']
    if tag:
        assert info['tag'] == tag and tag == 'v' + info['version']
    if commit:
        assert info['application_commit'] == commit
    names = {safe_relative(info[key]['filename'], basename=True) for key in ('bda', 'installer', 'source')} | {'BUILD-INFO.json'}
    assert len(names) == 4
    actual_names = {path.name for path in directory.iterdir()}
    # Actions carries the notes file; GitHub publishes its contents as the release body.
    assert actual_names in (names | {'SHA256SUMS.txt'},
                            names | {'SHA256SUMS.txt', 'RELEASE-NOTES.md'}), 'Unexpected release files'
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
    assert safe_relative(info['bda']['installation_evidence']) == record['installation_evidence']['path']
    assert info['bda']['exact_device_file_match'] is True
    assert info['bda']['exact_physical_bda_match'] is False
    assert 'physical_evidence' not in info['bda']
    check = info['verification']
    assert check['ok'] and check['version'] == info['version']
    assert check['sha256'] == sha(bda) and check['payload_sha256'] == record['payload_sha256']
    assert check['exact_physical_bda_match'] is False and check['runtime_source_match'] is True
    assert check['exact_device_file_match'] is True and check['runtime_hardware_verified'] is False
    assert check['installation_verified'] is True and 'physical_evidence' not in check
    assert check['user_runtime_validation_pending'] is True
    assert check['release_evidence'] == info['bda']['evidence']
    assert check['installation_evidence'] == info['bda']['installation_evidence']
    assert check['pgm_hardware_verified'] is False and check['neogeo_gameplay_regression_rerun'] is False
    assert check['pgm_gameplay_regression_rerun'] is False
    assert check['startup_picker_regression_rerun'] is False and check['native_selftests_rerun'] is False
    assert check['startup_selftest'] is False and check['cpu_selftest'] is True
    assert check['historical_native_evidence'] == installation['historical_native_evidence']
    assert check['sdk_commit'] == record['sdk_commit'] and check['validation_scope'] == record['verification_scope']
    assert info['runtime_preset'] == {'cpu_mhz': 408, 'display': '4:3', 'skip': 1, 'sound_hz': 8000}
    assert info['dependencies'] == record['dependencies']
    assert info['BIOS_distributed'] is True and info['game_ROM_distributed'] is False
    assert 'ROM_or_commercial_BIOS_distributed' not in info
    assert info['bios'] == bios_manifest
    with zipfile.ZipFile(directory / info['installer']['filename']) as archive:
        members = checked_members(archive)
        manifest = json.loads(archive.read('INSTALL-MANIFEST.json'))
        assert members == set(manifest['files']) | {'INSTALL-MANIFEST.json'}
        assert manifest['commit'] == info['application_commit'] and manifest['version'] == info['version']
        for name, digest in manifest['files'].items():
            safe_relative(name)
            assert sha(archive.read(name)) == digest, name
        assert archive.read('应用/程序/H1Arcade.bda') == bda
        for name, data in bios_entries.items():
            assert archive.read(name) == data, name
        assert {name for name in members if name.startswith('ARCADE/')} == {
            'ARCADE/放置游戏文件.txt', 'ARCADE/pgm.zip', 'ARCADE/neogeo.zip'
        }, 'Unexpected game data in installer'
        assert {name for name in members if name.lower().endswith(('.zip', '.7z', '.rar', '.tar', '.gz', '.tgz'))} == {
            'ARCADE/pgm.zip', 'ARCADE/neogeo.zip', 'PC工具/gngeo_data.zip'
        }, 'Unexpected archive in installer'
        assert not any(name.lower().endswith(('.h1neo', '.h1pgm', '.rom', '.raw', '.nand', '.bin')) for name in members)
        for name in ARCHIVES:
            spec = bios_manifest['files'][name]
            validate_archive(archive.read(spec['install_target']), spec)
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
        for name in (info['bda']['evidence'], info['bda']['installation_evidence'],
                     installation['historical_native_evidence']['path']):
            assert archive.read(prefix + name) == (ROOT / name).read_bytes(), name
        assert json.loads(archive.read(prefix + 'UPSTREAM.json')) == record['dependencies']
        assert archive.read(prefix + 'VERSION').decode().strip() == info['version']
        assert archive.read(prefix + MANIFEST_PATH) == bios_entries['BIOS-MANIFEST.json']
        for name in ARCHIVES:
            spec = bios_manifest['files'][name]
            data = archive.read(prefix + spec['source_path'])
            assert data == bios_entries[spec['install_target']], name
            validate_archive(data, spec)
        assert {name[len(prefix):] for name in members if name.lower().endswith(('.zip', '.7z', '.rar', '.tar', '.gz', '.tgz'))} == {
            'assets/bios/pgm.zip', 'assets/bios/neogeo.zip', 'third_party/gngeo/gngeo.dat/gngeo_data.zip'
        }, 'Unexpected archive in corresponding sources'
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
            'release_evidence': info['bda']['evidence'], 'installation_evidence': info['bda']['installation_evidence'],
            'exact_device_file_match': True, 'installation_verified': True,
            'runtime_hardware_verified': False,
            'user_runtime_validation_pending': True,
            'pgm_hardware_verified': False, 'neogeo_gameplay_regression_rerun': False,
            'pgm_gameplay_regression_rerun': False,
            'startup_picker_regression_rerun': False, 'native_selftests_rerun': False,
            'BIOS_distributed': True, 'game_ROM_distributed': False,
            'bios_sha256': {name: spec['sha256'] for name, spec in bios_manifest['files'].items()}}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--directory', type=Path, default=Path('dist/release'))
    ap.add_argument('--tag')
    ap.add_argument('--commit')
    args = ap.parse_args()
    print(json.dumps(verify(args.directory, args.tag, args.commit), indent=2))


if __name__ == '__main__':
    main()
