"""Compatibility entry point for the default Neo Geo/PGM release preset."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--toolchain',default=os.environ.get('H1_GNU_BIN',str(ROOT/'.tools/toolchain/bin')))
    ap.add_argument('--host-cc',default=os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc'))
    a=ap.parse_args()
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tools/build_release.py'),
                    '--toolchain',a.toolchain,'--host-cc',a.host_cc],check=True,cwd=ROOT)
    dist=ROOT/'dist';dist.mkdir(exist_ok=True)
    for suffix in ('.bda','.build.json'):
        shutil.copy2(dist/('H1Arcade'+suffix),dist/('H1ArcadePGM'+suffix))
    print('Default release and PGM compatibility output: '+str(dist/'H1ArcadePGM.bda'))

if __name__=='__main__':main()
