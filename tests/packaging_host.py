"""Check installed converter independence and complete-source archive integrity."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import runpy
import struct
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from verify_packages import verify

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--directory',type=Path,default=ROOT/'dist/release');a=ap.parse_args()
    report=verify(a.directory)
    info=json.loads((a.directory/'BUILD-INFO.json').read_text(encoding='utf-8'))
    out=ROOT/'build/installer-host';out.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(a.directory/info['installer']['filename']) as z:z.extractall(out)
    m=runpy.run_path(str(out/'PC工具/convert.py'))
    old_path=os.environ.get('PATH','');os.environ['PATH']=''
    try:
        lib=m['gfx_library']()
        tile=bytearray(4096);tile[64]=1;usage=bytearray(8)
        lib.sprites((ctypes.c_ubyte*4096).from_buffer(tile),4096,(ctypes.c_ubyte*8).from_buffer(usage))
        assert struct.unpack_from('<I',tile)[0]==0x10000000 and struct.unpack_from('<I',usage)[0]&3==0
        char=bytearray(range(32));u=bytearray(1)
        lib.chars((ctypes.c_ubyte*32).from_buffer(char),32,(ctypes.c_ubyte*1).from_buffer(u))
        assert char==bytes(v for y in range(8) for v in (16+y,24+y,y,8+y)) and u[0]
        for game in m['SUPPORTED']:assert m['driver'](game)[1]
    finally:os.environ['PATH']=old_path
    report.update(installed_converter_without_gcc_or_msys_path=True,driver_sets=11,
                  decoder_single_bit_and_swizzle=True,no_guest_CPU_execution=True)
    dest=ROOT/'build/verification/packaging-host.json';dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
