"""Compare the actual C presenter with a full redraw on complete LCD frames."""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,random,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--optimize',choices=('2','3'),default='2')
    ap.add_argument('--mxu',action='store_true',help='check presenter dispatch with a scalar stand-in; actual instructions are checked only on H1')
    args=ap.parse_args()
    out=ROOT/'build/present-host';out.mkdir(parents=True,exist_ok=True)
    wrapper=out/'reference.c'
    wrapper.write_text('''#include <stdint.h>
#include <string.h>
void reference(uint32_t *out,const uint16_t *src,unsigned pitch,unsigned mode){
 unsigned width=mode==0?362:mode==1?320:480,height=mode==1?224:272;
 memset(out,0,480*272*4);
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
  const uint16_t *line=(const uint16_t*)((const uint8_t*)src+(y*224/height)*pitch);
  unsigned p=line[x*320/width];
  out[(y+(272-height)/2)*480+x+(480-width)/2]=((p>>11)*8<<16)|(((p>>5)&63)*4<<8)|((p&31)*8);
 }
}
''',encoding='utf-8')
    if args.mxu:
        with wrapper.open('a',encoding='utf-8') as f:f.write('''
void h1_mxu_rgb565(uint32_t *out,const uint16_t *src,unsigned n){
 for(unsigned i=0;i<n;i++){unsigned p=src[i];out[i]=((p>>11)*8<<16)|(((p>>5)&63)*4<<8)|((p&31)*8);}
}
void h1_mxu_scale362(uint32_t *out,const uint16_t *src,unsigned mask){
 for(unsigned x=0;x<362;x++){
  unsigned sx=x*320/362,p=src[sx];
  if(mask&(1u<<(sx/16)))out[x]=((p>>11)*8<<16)|(((p>>5)&63)*4<<8)|((p&31)*8);
 }
}
''')
    compiler=Path(os.environ.get('HOST_CC','gcc'))
    env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    lib=out/('present-'+('mxu-' if args.mxu else '')+'o'+args.optimize+'.dll')
    subprocess.run([str(compiler),'-O'+args.optimize,'-fno-strict-aliasing','-shared','-static-libgcc','-DH1_DIRTY_PRESENT=1',
                    '-DH1_MXU='+str(int(args.mxu)),'-DH1_CPU_SELFTEST=0',
                    '-DH1_SCALE_OUTPUT='+str(int(args.mxu)),
                    '-I'+str(ROOT/'src'),str(ROOT/'src/present.c'),str(wrapper),'-o',str(lib)],check=True,env=env)
    dll=ctypes.CDLL(str(lib));u16,u32=ctypes.c_uint16,ctypes.c_uint32
    source=(u16*(352*256))();visible=ctypes.cast(ctypes.byref(source,(16*352+16)*2),ctypes.POINTER(u16))
    a=(u32*(480*272+8))();b=(u32*(480*272+8))();expected=(u32*(480*272))()
    targets=[ctypes.cast(ctypes.byref(x,16),ctypes.POINTER(u32)) for x in (a,b)]
    dirty=ctypes.c_uint.in_dll(dll,'h1_present_dirty');rng=random.Random(4740)
    mxu=ctypes.c_uint.in_dll(dll,'h1_present_mxu')
    direct=ctypes.c_uint.in_dll(dll,'h1_present_direct')
    scaled=ctypes.c_uint.in_dll(dll,'h1_present_scale')
    for i in range(352*256):source[i]=i&65535
    for y in range(224):
        for x in range(320):source[(y+16)*352+x+16]=(y*320+x)&65535
    assert len({source[(y+16)*352+x+16] for y in range(224) for x in range(320)})==65536
    cases=0
    for mode in (0,1,2):
        for frame in range(80):
            for j in range(0 if frame%3==0 else 12):
                y=rng.randrange(224);x=rng.randrange(320);source[(y+16)*352+x+16]=rng.randrange(65536)
            target=targets[(frame//7)%2];dirty.value=int(frame%19!=0)
            mxu.value=int(args.mxu and frame%13!=0)
            direct.value=int(frame%11!=0)
            scaled.value=int(frame%9!=0)
            for arr in (a,b):
                for i in list(range(4))+list(range(480*272+4,480*272+8)):arr[i]=0x1234abcd
            if frame%17==0:
                ctypes.memset(target,0xee,480*272*4);dll.h1_present_reset()
            dll.h1_present_rgb565(target,visible,704,mode,int(frame%23==0))
            dll.reference(expected,visible,704,mode)
            assert ctypes.string_at(target,480*272*4)==bytes(expected),(mode,frame)
            for arr in (a,b):
                assert all(arr[i]==0x1234abcd for i in list(range(4))+list(range(480*272+4,480*272+8)))
            cases+=1
    stats=(u32*8).in_dll(dll,'h1_present_stats');assert stats[1]==2 and stats[4]>0 and stats[7]>0
    report={'ok':True,'environment':'host C pixel comparison, no CPU/device emulation','optimization':'O'+args.optimize,'full_frame_cases':cases,
            'all_65536_rgb565_values':True,'modes':[0,1,2],'pointer_switches':True,'dirty_toggle':True,
            'external_reset':True,'bounds_canaries':True,'unchanged_rows_elided':stats[4],'unchanged_pixels_elided':stats[7],
            'source_sha256':hashlib.sha256((ROOT/'src/present.c').read_bytes()).hexdigest()}
    report['mxu_dispatch']=args.mxu
    report['direct_destination_toggle']=True
    report['fused_4x3_toggle']=True
    report['actual_mxu_instructions_executed']=False
    filename='verification.json' if args.optimize=='2' else 'verification-o3.json'
    if args.mxu:filename='verification-mxu-o'+args.optimize+'.json'
    (out/filename).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
