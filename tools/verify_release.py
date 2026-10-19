"""Check the exact locally built BDA against recorded physical-H1 evidence."""
import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'sdk'))
from h1_bda.header import decode_header,read_c_string
from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,build_icon_resources
from h1_bda.validate import validate_bda

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check-tag')
    a=ap.parse_args()
    version=(ROOT/'VERSION').read_text().strip()
    if a.check_tag:
        assert a.check_tag=='v'+version,'Tag differs from VERSION'
        print(a.check_tag)
        return
    bda=ROOT/'dist/H1Arcade.bda';data=bda.read_bytes()
    m=json.loads(bda.with_suffix('.build.json').read_text())
    evidence=json.loads((ROOT/'docs/evidence/hardware-verification.json').read_text())
    sha=lambda b:hashlib.sha256(b).hexdigest()
    assert validate_bda(bda)['ok'] and sha(data)==m['sha256']==evidence['current_bda_sha256']
    assert m['version']==version and read_c_string(decode_header(data)[0x2c:0x3c])=='街机游戏'
    assert sha(data[PAYLOAD_OFFSET:])==evidence['current_payload_sha256']
    assert data[RESOURCE_OFFSET:PAYLOAD_OFFSET]==build_icon_resources(ROOT/'assets/icon.png')
    deps=json.loads((ROOT/'UPSTREAM.json').read_text())
    assert m['sdk_commit']==deps['h1_sdk']['commit']==evidence['sdk_commit']
    assert m['options']['core']=='a68k' and m['options']['cpu_mhz']==408
    assert not m['options']['test_frames'] and not m['options']['benchmark']
    for name,digest in evidence['runtime_source_sha256'].items():
        assert sha((ROOT/name).read_bytes())==digest,name
    report={'ok':True,'version':version,'sha256':sha(data),
            'payload_sha256':sha(data[PAYLOAD_OFFSET:]),'exact_physical_bda_match':True,
            'runtime_source_match':True,'sdk_commit':m['sdk_commit'],
            'scope':'Local static checks and byte equality to recorded hardware image; this invocation does not run a device or CPU emulator.',
            'distribution':'BDA, installation ZIP and complete corresponding source snapshots, with original third-party notices.'}
    out=ROOT/'build/verification';out.mkdir(parents=True,exist_ok=True)
    (out/'release-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    bda.with_suffix('.bda.sha256').write_text(sha(data)+'  '+bda.name+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
