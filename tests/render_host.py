"""Compare the actual original and specialized GnGeo C drawing templates."""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from fast_render import patch_template,direct_tiles

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--tile-direct',action='store_true');ap.add_argument('--mxu-render',action='store_true');args=ap.parse_args()
    out=ROOT/('build/render-host-mxu' if args.mxu_render else 'build/render-host');out.mkdir(parents=True,exist_ok=True)
    source=ROOT/'third_party/gngeo/src/video_template.h';original=source.read_text();patched=direct_tiles(original) if args.tile_direct else patch_template(original)
    if args.mxu_render:
        from mxu_render import patch_template as mxu_template
        patched=mxu_template(original)
    (out/'reference.h').write_text(original);(out/'specialized.h').write_text(patched)
    wrapper='''#include <stdint.h>
#include <string.h>
#include "h1_render.h"
#define H1_PROFILE 1
volatile unsigned h1_render_fast=1;
volatile h1_render_stats_t h1_render_stats={0x31524452,1};
static struct { struct {struct {unsigned char *p;} tiles;} rom;unsigned nb_of_tiles;} memory;
static struct {unsigned pitch;int w,h;} surface={704,352,256},*buffer=&surface;
static unsigned *current_pc_pal;
static char full_y_skip[16]={0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},dda_y_skip[17],xskip[16],*dda_x_skip=xskip;
static uint16_t alpha_blend(uint16_t dest,uint16_t src,uint8_t a){
 uint8_t dr=((dest&0xf800)>>11)<<3,dg=((dest&0x7e0)>>5)<<2,db=(dest&31)<<3;
 uint8_t sr=((src&0xf800)>>11)<<3,sg=((src&0x7e0)>>5)<<2,sb=(src&31)<<3;
 dr=(((sr-dr)*a)>>8)+dr;dg=(((sg-dg)*a)>>8)+dg;db=(((sb-db)*a)>>8)+db;
 return ((dr>>3)<<11)|((dg>>2)<<5)|(db>>3);
}
#define BLEND16_50(a,b) ((((a)&0xf7de)>>1)+(((b)&0xf7de)>>1))
#define BLEND16_25(a,b) alpha_blend(a,b,63)
'''
    for prefix,template in (('ref','reference.h'),('fast','specialized.h')):
        for mode,put in enumerate(('dst=src','dst=BLEND16_50(src,dst)','dst=BLEND16_25(src,dst)')):
            if args.mxu_render and prefix=='fast' and mode==0:wrapper+='#define H1_NORMAL_TILE 1\n'
            wrapper+=f'#define RENAME(name) {prefix}{mode}_##name\n#define PUTPIXEL(dst,src) {put}\n#include "{template}"\n'
            if args.mxu_render and prefix=='fast' and mode==0:wrapper+='#undef H1_NORMAL_TILE\n'
    wrapper+='''
typedef void (*draw_fn)(unsigned,int,int,int,int,int,int,int,unsigned char*);
typedef void (*line_fn)(unsigned,int,int,int,int,int,int,unsigned char*);
int compare(unsigned mode,unsigned zx,unsigned zy,unsigned xf,unsigned yf,unsigned seed,unsigned scan){
 static uint16_t a[352*256+16],b[352*256+16];static unsigned gfx[128],pal[4096];
 unsigned rng=seed;
 for(unsigned i=0;i<128;i++){
  rng=rng*1664525u+1013904223u;unsigned value=rng;
  if(seed%6==0)value=0;
  if(seed%6==1)value=0x11111111u;
  if(seed%6==2)value|=0x11111111u;
  if(seed%6==4)value=i&1?0u-rng:((rng=rng*1664525u+1013904223u));
  gfx[i]=value;
 }
 if(seed%6==4)for(unsigned i=0;i<128;i+=2)gfx[i+1]=0u-gfx[i];
 for(unsigned i=0;i<4096;i++){rng=rng*1664525u+1013904223u;pal[i]=rng;}
 for(unsigned i=0;i<352*256+16;i++)a[i]=(uint16_t)(i*331u+seed);
 memcpy(b,a,sizeof a);memory.rom.tiles.p=(unsigned char*)gfx;memory.nb_of_tiles=4;current_pc_pal=pal;
 dda_y_skip[0]=0;for(unsigned i=1;i<zy;i++)dda_y_skip[i]=i*16/zy-(i-1)*16/zy;
 for(unsigned i=0;i<16;i++)xskip[i]=i<zx;
 draw_fn refs[]={ref0_draw,ref1_draw,ref2_draw},fasts[]={fast0_draw,fast1_draw,fast2_draw};
 line_fn refl[]={ref0_draw_scanline,ref1_draw_scanline,ref2_draw_scanline},fastl[]={fast0_draw_scanline,fast1_draw_scanline,fast2_draw_scanline};
 unsigned tile=(seed>>8)&3,color=(seed>>10)&255,x=16+(seed&15),y=20;
 h1_render_fast=seed%11!=0;
 HOST_RENDER_BEGIN();
 if(scan){
  refl[mode](tile,zy-1,x,y,zx,color,xf,(unsigned char*)(a+8));
  fastl[mode](tile,zy-1,x,y,zx,color,xf,(unsigned char*)(b+8));
 }else{
  refs[mode](tile,x,y,zx,zy,color,xf,yf,(unsigned char*)(a+8));
  fasts[mode](tile,x,y,zx,zy,color,xf,yf,(unsigned char*)(b+8));
 }
 return memcmp(a,b,sizeof a)==0;
}
'''
    if args.mxu_render:
        wrapper=wrapper.replace('volatile unsigned h1_render_fast=1;','').replace('volatile h1_render_stats_t h1_render_stats={0x31524452,1};','')
        wrapper='#define HOST_RENDER_BEGIN() h1_render_begin()\n'+wrapper
        wrapper+=(ROOT/'tests/mxu_render_host_stub.h').read_text()
    else:wrapper='#define HOST_RENDER_BEGIN() ((void)0)\n'+wrapper
    c=out/'compare.c';c.write_text(wrapper);cc=Path(os.environ.get('HOST_CC','gcc'))
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    dllpath=out/'render.dll'
    extra=['-DH1_MXU_RENDER=1','-DH1_FAST_RENDER=0',str(ROOT/'src/render.c')] if args.mxu_render else []
    subprocess.run([str(cc),'-O3','-fno-strict-aliasing','-shared','-static-libgcc','-I'+str(ROOT/'src'),*extra,str(c),'-o',str(dllpath)],env=env,check=True)
    dll=ctypes.CDLL(str(dllpath));cases=0
    for mode in range(3):
        for zx in range(1,17):
            for zy in range(1,17):
                for xf in range(2):
                    for yf in range(2):
                        for scan in (0,1):
                            seed=cases*1664525+4740
                            assert dll.compare(mode,zx,zy,xf,yf,seed&0xffffffff,scan),(mode,zx,zy,xf,yf,seed,scan)
                            cases+=1
    if args.mxu_render:
        for i in range(4096):
            assert dll.compare(0,16,16,(i>>1)&1,(i>>2)&1,(i*1664525+4740)&0xffffffff,0),i
            cases+=1
    stats=(ctypes.c_uint*3).in_dll(dll,'h1_render_stats')
    if args.mxu_render:assert (ctypes.c_uint*4).in_dll(dll,'h1_tile_mxu_stats')[2]>0
    else:assert args.tile_direct or stats[2]>0
    report={'ok':True,'environment':'host C pixel comparison, no CPU/device emulation','tile_direct_only':args.tile_direct,'full_buffer_cases':cases,
            'blend_modes':[0,50,25],'x_zoom':list(range(1,17)),'y_zoom':list(range(1,17)),
            'both_flips':True,'transparent_rows':True,'opaque_rows':True,'odd_x':True,'optimization_toggle':not args.tile_direct,
            'fast_opaque_rows':stats[2],'upstream_template_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
            'patched_template_sha256':hashlib.sha256(patched.encode()).hexdigest(),
            'patch_script_sha256':hashlib.sha256((ROOT/'tools/fast_render.py').read_bytes()).hexdigest()}
    if args.mxu_render:report.update(mxu_render=True,actual_mxu_instructions_executed=False,
        environment='actual C dispatch/cache/templates on host with mathematical leaf stand-in; native leaf separately tested on physical H1',
        tiles=(ctypes.c_uint*4).in_dll(dll,'h1_tile_mxu_stats')[2],
        mxu_patch_sha256=hashlib.sha256((ROOT/'tools/mxu_render.py').read_bytes()).hexdigest())
    (out/('verification-tiles.json' if args.tile_direct else 'verification.json')).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
