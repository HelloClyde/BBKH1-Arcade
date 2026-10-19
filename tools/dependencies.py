"""Verify pinned git submodules or the exact snapshots in a release source ZIP."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def verify_dependencies(root=ROOT):
    deps=json.loads((root/'UPSTREAM.json').read_text(encoding='utf-8'))
    manifest_path=root/'UPSTREAM-SOURCE-MANIFEST.json'
    if manifest_path.exists():
        manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
        for key,info in deps.items():
            snapshot=manifest['dependencies'][key]
            assert snapshot['commit']==info['commit'] and snapshot['path']==info['path'],key
            assert snapshot['files'],key
            for name,digest in snapshot['files'].items():
                path=root/name
                assert path.resolve().is_relative_to((root/info['path']).resolve()),name
                assert hashlib.sha256(path.read_bytes()).hexdigest()==digest,name
    else:
        for key,info in deps.items():
            target=(root/info['path']).resolve()
            git=['git','-c',f'safe.directory={target.as_posix()}','-C',str(target)]
            actual=subprocess.check_output([*git,'rev-parse','HEAD'],text=True).strip()
            assert actual==info['commit'],key+' commit mismatch'
            assert not subprocess.check_output([*git,'status','--porcelain','--untracked-files=no'],text=True).strip(),key+' upstream modified'
    return deps
