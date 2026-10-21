"""Check real pinned BIOS packaging inputs and reject damaged temporary copies."""
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from release_bios import ARCHIVES, MANIFEST_PATH, bios_install_entries, load_bios, sha, validate_archive


def rejected(label, operation, results):
    try:
        operation()
    except (AssertionError, FileNotFoundError, zipfile.BadZipFile, RuntimeError, ValueError):
        results.append(label)
    else:
        raise AssertionError('Damaged BIOS was accepted: ' + label)


def main():
    blobs, manifest = load_bios()
    entries, same_manifest = bios_install_entries()
    assert manifest == same_manifest
    assert set(blobs) == {'ARCADE/pgm.zip', 'ARCADE/neogeo.zip'}
    assert set(entries) == set(blobs) | {'BIOS-MANIFEST.json', 'BIOS说明.md'}
    assert json.loads(entries['BIOS-MANIFEST.json']) == manifest
    # These are the actual additional entries consumed by package_release.py.
    packed = io.BytesIO()
    with zipfile.ZipFile(packed, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries.items():
            archive.writestr(name, data)
    with zipfile.ZipFile(io.BytesIO(packed.getvalue())) as archive:
        assert archive.testzip() is None
        for name in ARCHIVES:
            spec = manifest['files'][name]
            data = archive.read(spec['install_target'])
            assert data == (ROOT / spec['source_path']).read_bytes()
            validate_archive(data, spec)

    failures = []
    with tempfile.TemporaryDirectory(prefix='h1-bios-release-') as temp:
        fixture = Path(temp)
        (fixture / 'assets/bios').mkdir(parents=True)
        (fixture / MANIFEST_PATH).write_bytes(entries['BIOS-MANIFEST.json'])
        for name in ARCHIVES:
            spec = manifest['files'][name]
            path = fixture / spec['source_path']
            raw = blobs[spec['install_target']]
            path.write_bytes(raw)
        assert load_bios(fixture) == (blobs, manifest)
        for name in ARCHIVES:
            spec = manifest['files'][name]
            path = fixture / spec['source_path']
            raw = blobs[spec['install_target']]
            path.unlink()
            rejected(name + ':missing_archive', lambda: load_bios(fixture), failures)
            altered = bytearray(raw)
            altered[len(altered) // 2] ^= 1
            path.write_bytes(altered)
            rejected(name + ':modified_byte', lambda: load_bios(fixture), failures)
            path.write_bytes(raw)

            # Recompute the outer identity so the exact member-set check itself is exercised.
            fewer = io.BytesIO()
            with zipfile.ZipFile(io.BytesIO(raw)) as source, zipfile.ZipFile(fewer, 'w') as archive:
                for entry in source.infolist()[1:]:
                    archive.writestr(entry.filename, source.read(entry))
            short_spec = copy.deepcopy(spec)
            short_spec['size'] = len(fewer.getvalue())
            short_spec['sha256'] = sha(fewer.getvalue())
            rejected(name + ':missing_member', lambda: validate_archive(fewer.getvalue(), short_spec), failures)

            member_name = next(iter(spec['members']))
            for field, value in (('crc32', 'ffffffff'), ('sha256', 'f' * 64),
                                 ('size', spec['members'][member_name]['size'] + 1)):
                wrong_spec = copy.deepcopy(spec)
                wrong_spec['members'][member_name][field] = value
                rejected(name + ':member_' + field, lambda: validate_archive(raw, wrong_spec), failures)

        # An added game ZIP and a traversal install target are not accepted by the fixed schema.
        invalid = copy.deepcopy(manifest)
        invalid['files']['kov.zip'] = copy.deepcopy(invalid['files']['pgm.zip'])
        (fixture / MANIFEST_PATH).write_text(json.dumps(invalid), encoding='utf-8')
        rejected('extra_game_archive', lambda: load_bios(fixture), failures)
        invalid = copy.deepcopy(manifest)
        invalid['files']['pgm.zip']['install_target'] = '../pgm.zip'
        (fixture / MANIFEST_PATH).write_text(json.dumps(invalid), encoding='utf-8')
        rejected('unsafe_install_target', lambda: load_bios(fixture), failures)

    # The checked-in assets are unchanged: the destructive cases touched only the temporary copies.
    assert load_bios() == (blobs, manifest)
    report = {
        'ok': True, 'archive_count': len(ARCHIVES),
        'member_count': sum(len(spec['members']) for spec in manifest['files'].values()),
        'installer_inputs': {name: sha(data) for name, data in entries.items()},
        'rejected_cases': failures, 'rejected_count': len(failures),
        'offline': True, 'no_guest_CPU_execution': True,
    }
    out = ROOT / 'build/verification/release-bios-host.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
