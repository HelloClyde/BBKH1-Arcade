"""Check the release BDA against its device-install receipt and fixed build inputs."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'sdk'))
from h1_bda.header import decode_header, read_c_string
from h1_bda.resources import PAYLOAD_OFFSET, RESOURCE_OFFSET, build_icon_resources
from h1_bda.validate import validate_bda
from dependencies import verify_dependencies
from verify_packages import RELEASE_EVIDENCE, load_release_evidence, safe_relative


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check-tag')
    args = ap.parse_args()
    version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
    if args.check_tag:
        assert args.check_tag == 'v' + version, 'Tag differs from VERSION'
        print(args.check_tag)
        return
    record, installation = load_release_evidence(ROOT)
    assert version == record['release_version']
    bda = ROOT / 'dist/H1Arcade.bda'
    data = bda.read_bytes()
    metadata = json.loads(bda.with_suffix('.build.json').read_text(encoding='utf-8'))
    sha = lambda blob: hashlib.sha256(blob).hexdigest()
    assert validate_bda(bda)['ok'], 'Invalid BDA header/resources/checksum'
    assert sha(data) == metadata['sha256'] == record['bda_sha256']
    assert len(data) == metadata['size'] == record['bda_size']
    assert metadata['version'] == version
    assert metadata['title'] == read_c_string(decode_header(data)[0x2c:0x3c]) == record['title']
    assert sha(data[PAYLOAD_OFFSET:]) == record['payload_sha256']
    assert sha((ROOT / 'assets/icon.png').read_bytes()) == metadata['icon_sha256'] == record['icon_sha256']
    assert data[RESOURCE_OFFSET:PAYLOAD_OFFSET] == build_icon_resources(ROOT / 'assets/icon.png')
    dependencies = verify_dependencies(ROOT)
    assert dependencies == metadata['dependencies'] == record['dependencies']
    assert metadata['sdk_commit'] == dependencies['h1_sdk']['commit'] == record['sdk_commit']
    # Tool locations are machine-specific; every effective runtime option is fixed.
    options = {key: value for key, value in metadata['options'].items() if key not in ('toolchain', 'host_cc')}
    assert options == record['build_option_values']
    assert options['pgm'] and options['core'] == 'a68k' and options['cpu_mhz'] == 408
    assert options['cpu_selftest'] is True and options['startup_selftest'] is False
    assert not options['test_frames'] and not options['benchmark'] and not options['profile']
    expected = record['runtime_source_sha256']
    current_src = {p.relative_to(ROOT).as_posix() for p in (ROOT / 'src').rglob('*')
                   if p.is_file() and p.suffix in ('.c', '.h', '.S', '.ld')}
    assert current_src == {name for name in expected if name.startswith('src/')}, 'Local source input set changed'
    for name, digest in expected.items():
        path = ROOT / safe_relative(name)
        assert path.resolve().is_relative_to(ROOT.resolve()), name
        assert sha(path.read_bytes()) == digest == metadata['source_sha256'][name], name
    report = {
        'ok': True, 'version': version, 'sha256': sha(data),
        'payload_sha256': sha(data[PAYLOAD_OFFSET:]), 'exact_physical_bda_match': False,
        'exact_device_file_match': True, 'installation_verified': True,
        'runtime_hardware_verified': False,
        'user_runtime_validation_pending': True,
        'runtime_source_match': True, 'sdk_commit': metadata['sdk_commit'],
        'release_evidence': RELEASE_EVIDENCE,
        'installation_evidence': record['installation_evidence']['path'],
        'historical_native_evidence': installation['historical_native_evidence'],
        'pgm_hardware_verified': False,
        'pgm_gameplay_regression_rerun': False,
        'neogeo_gameplay_regression_rerun': False,
        'startup_picker_regression_rerun': False,
        'native_selftests_rerun': False,
        'startup_selftest': False, 'cpu_selftest': True,
        'validation_scope': record['verification_scope'],
        'scope': 'Local static checks, fixed build-input hashes and equality to the H1 upload/CRC receipt. The current BDA was installed without launching it; startup, gameplay and native checks remain for the user to validate. Earlier physical CPU/MXU and FPS evidence applies only to the older BDA.',
        'distribution': 'BDA, installation ZIP and complete corresponding source snapshots, with original third-party notices.'
    }
    out = ROOT / 'build/verification'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'release-verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    bda.with_suffix('.bda.sha256').write_text(sha(data) + '  ' + bda.name + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
