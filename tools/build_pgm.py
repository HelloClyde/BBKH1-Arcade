"""Build the experimental PGM/Neo Geo preset without replacing the v0.1.2 dist BDA."""
from pathlib import Path
import argparse,os,shutil,subprocess,sys
from build_release import PRESET,BUILD_NAME
ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--toolchain',default=os.environ.get('H1_GNU_BIN',str(ROOT/'.tools/toolchain/bin')))
    ap.add_argument('--host-cc',default=os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc'))
    a=ap.parse_args()
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tools/build.py'),*PRESET,'--pgm',
                    '--toolchain',a.toolchain,'--host-cc',a.host_cc],check=True,cwd=ROOT)
    dist=ROOT/'dist';dist.mkdir(exist_ok=True)
    for suffix in ('.bda','.build.json'):
        shutil.copy2(ROOT/'build'/(BUILD_NAME+'-pgm')/('H1Arcade'+suffix),dist/('H1ArcadePGM'+suffix))
    print('Experimental PGM output: '+str(dist/'H1ArcadePGM.bda'))

if __name__=='__main__':main()
