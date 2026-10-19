"""Compatibility entry point for the complete corresponding source archive."""
import argparse
from source_bundle import ROOT, git, complete_source

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--tag',required=True);a=ap.parse_args()
    version=(ROOT/'VERSION').read_text().strip();assert a.tag=='v'+version
    commit=git(ROOT,'rev-parse','HEAD').decode().strip()
    assert git(ROOT,'rev-parse',a.tag+'^{commit}').decode().strip()==commit
    assert not git(ROOT,'status','--porcelain','--untracked-files=normal').strip()
    out=ROOT/'dist/release';out.mkdir(parents=True,exist_ok=True)
    print(complete_source(out/f'BBKH1-Arcade-{a.tag}-source.zip',commit,a.tag))

if __name__=='__main__':main()
