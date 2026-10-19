"""Rebuild the packaged source snapshots with no .git data or upstream downloads."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',required=True,type=Path)
    ap.add_argument('--toolchain',type=Path,default=ROOT/'.tools/toolchain/bin');ap.add_argument('--host-cc',default='gcc');a=ap.parse_args()
    assert not a.output.exists(),'Use a fresh extraction directory'
    info=json.loads((ROOT/'dist/release/BUILD-INFO.json').read_text(encoding='utf-8'))
    a.output.mkdir(parents=True)
    with zipfile.ZipFile(ROOT/'dist/release'/info['source']['filename']) as z:
        for name in z.namelist():assert (a.output/name).resolve().is_relative_to(a.output.resolve())
        z.extractall(a.output)
    source=a.output/('BBKH1-Arcade-v'+info['version'])
    assert not list(source.rglob('.git'))
    for command in ([sys.executable,'-X','utf8','tools/fetch.py'],
                    [sys.executable,'-X','utf8','tools/build_release.py','--toolchain',str(a.toolchain.resolve()),'--host-cc',a.host_cc]):
        subprocess.run(command,cwd=source,check=True)
    actual=hashlib.sha256((source/'dist/H1Arcade.bda').read_bytes()).hexdigest()
    assert actual==info['bda']['sha256']
    report={'ok':True,'bda_sha256':actual,'git_metadata_required':False,
            'upstream_downloads_required':False,'complete_corresponding_source_rebuilt':True}
    out=ROOT/'build/verification/source-archive-build.json'
    out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
