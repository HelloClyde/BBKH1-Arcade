"""Build the physically tested Neo Geo/PGM preset from fixed dependencies."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD_NAME = 'release-a68k-idle-dirty-selftest-o3-lto-bus-reads-tiles-mxu-spritefix-drawmxu4-audio16-visible-speed-scale-cpu408-menu-pgm'
PRESET = ['--core','a68k','--idle','--dirty-present','--cpu-selftest',
          '--optimize','3','--lto','--fast-bus','--bus-reads-only','--tile-direct',
          '--mxu','--mxu-render','--audio-fast','--visible-render','--speed-core',
          '--scale-output','--cpu-mhz','408','--pgm']

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--toolchain',default=os.environ.get('H1_GNU_BIN',str(ROOT/'.tools/toolchain/bin')))
    ap.add_argument('--host-cc',default=os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc'))
    a = ap.parse_args()
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tools/build.py'),*PRESET,
                    '--toolchain',a.toolchain,'--host-cc',a.host_cc],check=True,cwd=ROOT)
    dist = ROOT/'dist';dist.mkdir(exist_ok=True)
    for suffix in ('.bda','.build.json'):
        shutil.copy2(ROOT/'build'/BUILD_NAME/('H1Arcade'+suffix),dist/('H1Arcade'+suffix))
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tools/verify_release.py')],check=True,cwd=ROOT)

if __name__ == '__main__':
    main()
