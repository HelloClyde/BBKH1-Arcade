"""Create audited H1 build copies; leave upstream checkouts untouched."""
from pathlib import Path
import math,os,re,shutil,subprocess
from fast_render import patch_template,direct_tiles
from video_patch import patch_video
ROOT=Path(__file__).resolve().parents[1]

def replace(source,old,new):
    if source.count(old)!=1:raise RuntimeError('upstream patch context changed: '+old[:90])
    return source.replace(old,new,1)

def prepare(build,fast_render=False,tile_direct=False,mxu_render=False,palette32=False,audio_fast=False,visible_render=False,speed_core=False):
    core=build/'core'
    shutil.copytree(ROOT/'third_party/gngeo/src',core,dirs_exist_ok=True)
    if mxu_render:
        import mxu_render as render_patch
        p=core/'video_template.h';p.write_text(render_patch.patch_template(p.read_text()))
    elif fast_render:
        p=core/'video_template.h';p.write_text(patch_template(p.read_text()))
    elif tile_direct:
        p=core/'video_template.h';p.write_text(direct_tiles(p.read_text()))
    p=core/'mamez80/z80.h';p.write_text('/* H1 change: use standard unsigned type. */\n'+p.read_text().replace('extern uint z80_stateDataSize','extern unsigned z80_stateDataSize'))
    p=core/'mamez80/z80.c';s=p.read_text().replace('uint z80_stateDataSize','unsigned z80_stateDataSize')
    s=s.replace('#define BIG_FLAGS_ARRAY \t1','#define BIG_FLAGS_ARRAY \t0 /* H1: bounded static flag tables */')
    if speed_core:
        s=replace(s,'int z80_execute(int cycles)\n#endif\n{',
            '#include "z80_hle.h"\nint z80_execute(int cycles)\n#endif\n{')
        s=replace(s,'\t\t_PPC = _PCD;\n\t\t//CALL_MAME_DEBUG;',
            '\t\tif(Z80_ICOUNT>0 && _PCD>=0x122 && _PCD<=0x13b && h1_z80_hle && h1_z80_poll_valid){h1_z80_poll_run();if(Z80_ICOUNT<=0)break;}\n\t\t_PPC = _PCD;\n\t\t//CALL_MAME_DEBUG;')
    p.write_text('/* H1 change: bounded tables and standard unsigned type. */\n'+s)
    # Bare metal uses explicit CPU scheduling and our own checked package/cache.
    p=core/'video.c';s=patch_video(p.read_text())
    if mxu_render:s=render_patch.patch_video(s)
    if visible_render:
        from visible_render import patch_video as visible_patch
        s=visible_patch(s)
    a=s.index('uint8_t *get_cached_sprite_ptr(');b=s.index('static void fix_value_init',a)
    s=s[:a]+'/* H1 change: checked raw-page cache, no unchecked compressed reads. */\n#include "h1_port.h"\nuint8_t *get_cached_sprite_ptr(uint32_t tile){return h1_sprite_page(tile);}\n\n'+s[b:]
    if fast_render or mxu_render:s+='\n#include "render_selftest.h"\n'
    if palette32:
        import palette32 as rgb
        (core/'video_template32.h').write_text(rgb.template((ROOT/'third_party/gngeo/src/video_template.h').read_text()))
        s=rgb.video(s)
    p.write_text(s)
    p=core/'memory.c';s=p.read_text()
    start=s.index('Uint8 mem68k_fetch_memcrd_byte(');end=s.index('Uint16 mem68k_fetch_memcrd_word',start)
    s=s[:start]+s[start:end].replace('if(addr & 1)', 'if(!(addr & 1)) /* H1: data is on the odd byte lane */')+s[end:]
    s=s.replace('  memory.memcard[addr >> 1] = data;', '  if(addr & 1)memory.memcard[addr >> 1] = data; /* H1: odd byte lane */')
    if palette32:s=rgb.memory(s)
    p.write_text(s)
    p=core/'sound.h';p.write_text(p.read_text().replace('#define SAMPLE_RATE          44100','#define SAMPLE_RATE          22050'))
    p=core/'timer.c';s=p.read_text().replace('(0.01666)','(0.016896)')
    s=replace(s,'timer_count >= timers[i].time && timers[i].del_it == 0',
              'timers[i].del_it == 0 && timer_count >= timers[i].time')
    if speed_core:
        s=replace(s,'double timer_count;','''double timer_count;
/* Only bypass provably distant deadline comparisons. The original double
 * clock, deadline and expiry comparison are retained without rounding changes. */
#include "speed_core.h"
volatile unsigned h1_timer_fast=1;
volatile h1_timer_fast_stats_t h1_timer_fast_stats={0x31544d48,1};
static unsigned h1_timer_remaining[3];
static int h1_timer_init=1;
static unsigned h1_timer_distance(double duration){
 if(nb_interlace!=264 || timer_count<0 || timer_count>65536 ||
    !(duration>=0.00032 && duration<1.0))return 0;
 unsigned n=(unsigned)(duration*15625.0);
 return n>4?n-4:0;
}
'''.replace(' if(nb_interlace',' extern int nb_interlace;\n if(nb_interlace'))
        s=replace(s,'      timers[i].time = timer_count + duration;',
            '      timers[i].time = timer_count + duration;\n      h1_timer_remaining[i]=h1_timer_distance(duration);')
        s=replace(s,'  static int init = 1;','')
        s=s.replace('if(init)', 'if(h1_timer_init)').replace('    init = 0;','    h1_timer_init = 0;')
        s=replace(s,'    if(timers[i].del_it == 0 && timer_count >= timers[i].time) {',
            '''    if(timers[i].del_it)continue;
    if(h1_timer_remaining[i]){
      --h1_timer_remaining[i];
      if(h1_timer_fast)continue;
    }
    if(timer_count >= timers[i].time) {''')
        s+='\n#include "timer_fast_selftest.h"\n'
    p.write_text(s)
    # Eliminate libm at startup: emit exactly the upstream integer table values.
    p=core/'ym2610/ym2610.c';s=p.read_text()
    s=s.replace('#include <math.h>','/* H1 change: generated integer tables replace libm initialization. */')
    a=s.index('\tfor (x = 0; x < TL_RES_LEN; x++)',s.index('static void OPNInitTable'))
    b=s.index('\t/* build LFO PM modulation table */',a)
    tl=[]
    for x in range(256):
        n=int(math.floor(65536/math.pow(2,(x+1)/256)))>>4
        n=((n>>1)+(n&1))<<2
        tl.append(n)
    sin=[]
    for i in range(1024):
        m=math.sin((i*2+1)*math.pi/1024)
        n=int(2*8*math.log(1/abs(m))/math.log(2)/(128/1024/4))
        n=(n>>1)+(n&1)
        sin.append(n*2+(m<0))
    # ENV_STEP is 128/ENV_LEN, where ENV_LEN=1024.
    tables='static const int h1_tl[256]={'+','.join(map(str,tl))+'};\n'
    tables+='static const unsigned h1_sin[1024]={'+','.join(map(str,sin))+'};\n'
    tables+='for(x=0;x<256;x++){for(i=0;i<13;i++){tl_tab[x*2+i*512]=h1_tl[x]>>i;tl_tab[x*2+1+i*512]=-(h1_tl[x]>>i);}}\nmemcpy(sin_tab,h1_sin,sizeof sin_tab);\n'
    s=s[:a]+tables+s[b:]
    # Render audio up to each Z80 write before changing YM registers.
    s=s.replace('YM2610UpdateRequest();','h1_audio_sync();')
    s=s.replace('void ym2610_mkstate(gzFile *gzf, int mode)', 'void ym2610_mkstate(gzFile gzf, int mode)')
    s='#include "h1_port.h"\n'+s
    if audio_fast:s+='\n#include "ym_audio_fast.h"\n'
    p.write_text(s)
    if audio_fast:
        p=core/'ym2610_interf.c';s=p.read_text()
        s=replace(s,'int rate = SAMPLE_RATE;','int rate = h1_audio_source_rate;')
        p.write_text('#include "audio_fast.h"\n'+s)
    # Guard bank copies including short M1 images. Core init reads >=64 KiB.
    p=core/'mamez80_interf.c';s=p.read_text()
    if speed_core:
        s='#include "speed_core.h"\n'+s
        s=replace(s,'    z80_set_irq_callback(mame_z80_irq_callback);',
            '    z80_set_irq_callback(mame_z80_irq_callback);\n    h1_z80_hle_init();')
        p.write_text(s)
    # This port pads M1 to 512 KiB, matching the Z80 bank address range.
    # Only the core/generator is built. Nested test/example trees also exceed
    # Windows path limits in a source ZIP extracted under a longer folder.
    m=build/'musashi';shutil.copytree(ROOT/'third_party/musashi',m,dirs_exist_ok=True,ignore=shutil.ignore_patterns('.git','test','example'))
    p=m/'m68kcpu.c';s=p.read_text()
    s=s.replace('#include "m68kfpu.c"','/* H1: 68000-only build, no FPU. */')
    s=s.replace('#include "m68kmmu.h"','/* H1: 68000-only build, no MMU. */')
    p.write_text(s)
    host=Path(os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc'))
    exe=build/'m68kmake.exe'
    env=dict(os.environ);env['PATH']=str(host.parent)+os.pathsep+env.get('PATH','')
    subprocess.run([str(host),'-O2','-static',str(m/'m68kmake.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe),str(m),str(m/'m68k_in.c')],check=True)
    return core,m
