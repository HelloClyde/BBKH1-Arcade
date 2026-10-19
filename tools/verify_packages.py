"""Verify downloaded release assets with only the Python standard library."""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import zipfile

def sha(data):return hashlib.sha256(data).hexdigest()

def checked_members(z):
    names=z.namelist();assert len(names)==len(set(names))
    for name in names:
        assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts,name
    assert not z.testzip()
    return set(names)

def verify(directory,tag=None,commit=None):
    info=json.loads((directory/'BUILD-INFO.json').read_text(encoding='utf-8'))
    if tag:assert info['tag']==tag and tag=='v'+info['version']
    if commit:assert info['application_commit']==commit
    names={info[k]['filename'] for k in ('bda','installer','source')}|{'BUILD-INFO.json'}
    sums={}
    for row in (directory/'SHA256SUMS.txt').read_text().splitlines():
        digest,name=row.split('  ',1);assert name not in sums and '/' not in name and '\\' not in name
        sums[name]=digest;assert sha((directory/name).read_bytes())==digest,name
    assert set(sums)==names
    bda=(directory/info['bda']['filename']).read_bytes()
    assert sha(bda)==info['bda']['sha256'] and len(bda)==info['bda']['size']
    assert sha(bda)==info['verification']['sha256'] and info['verification']['exact_physical_bda_match']
    evidence=Path(__file__).resolve().parents[1]/'docs/evidence/hardware-verification.json'
    assert sha(bda)==json.loads(evidence.read_text(encoding='utf-8'))['current_bda_sha256']
    assert not info['ROM_or_commercial_BIOS_distributed']
    with zipfile.ZipFile(directory/info['installer']['filename']) as z:
        members=checked_members(z);m=json.loads(z.read('INSTALL-MANIFEST.json'))
        assert members==set(m['files'])|{'INSTALL-MANIFEST.json'}
        assert m['commit']==info['application_commit'] and m['version']==info['version']
        for name,digest in m['files'].items():assert sha(z.read(name))==digest,name
        assert z.read('应用/程序/H1Arcade.bda')==bda
        assert z.read('PC工具/gfx.dll')[:2]==b'MZ'
        with zipfile.ZipFile(io.BytesIO(z.read('PC工具/gngeo_data.zip'))) as drivers:
            assert len(drivers.namelist())==11 and all(n.endswith('.drv') for n in drivers.namelist())
    with zipfile.ZipFile(directory/info['source']['filename']) as z:
        members=checked_members(z)
        manifest=next(n for n in members if n.endswith('/SOURCE-MANIFEST.json'))
        prefix=manifest[:-len('SOURCE-MANIFEST.json')];m=json.loads(z.read(manifest))
        assert members=={prefix+n for n in m['files']}|{manifest}
        assert m['commit']==info['application_commit'] and m['tag']==info['tag'] and m['submodules_included']
        for name,digest in m['files'].items():assert sha(z.read(prefix+name))==digest,name
        upstream=json.loads(z.read(prefix+'UPSTREAM-SOURCE-MANIFEST.json'))
        for key,dependency in info['dependencies'].items():
            snapshot=upstream['dependencies'][key];assert snapshot['commit']==dependency['commit']
            for name,digest in snapshot['files'].items():assert sha(z.read(prefix+name))==digest,name
        assert not any(n.lower().endswith(('.bda','.dll','.exe','.bin','.elf','.o','.h1neo','.rom','.raw','.nand')) for n in members)
    return {'ok':True,'version':info['version'],'commit':info['application_commit'],
            'bda_sha256':sha(bda),'assets':sorted(names),'complete_source_snapshots':True}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--directory',type=Path,default=Path('dist/release'))
    ap.add_argument('--tag');ap.add_argument('--commit');a=ap.parse_args()
    print(json.dumps(verify(a.directory,a.tag,a.commit),indent=2))

if __name__=='__main__':main()
