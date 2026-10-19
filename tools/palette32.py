"""Build-copy RGB32 renderer; keep RGB565 reference and exact blend rounding."""
import re
from fast_render import direct_tiles

def template(source):
    source=direct_tiles(source)
    source=source.replace('unsigned short','uint32_t').replace('buffer->pitch >> 1','buffer->pitch >> 2')
    source=source.replace('current_pc_pal','h1_pc_pal32')
    marker='  gfxdata = (unsigned int *)&memory.rom.tiles.p[ tileno << 7];'
    assert source.count(marker)==1
    source=source.replace(marker,marker+'''
#ifdef H1_NORMAL_TILE32
  if(h1_render_fast && zx==16 && zy==16 && buffer->pitch==1408 && h1_sprite_tile_opaque(gfxdata)){
    h1_mxu_tile16_palette32((uint32_t*)bmp+sy*352+sx,gfxdata,paldata,(xflip?1:0)|(yflip?2:0));
    ++h1_palette32_stats.tiles;
    return;
  }
#endif
''',1)
    return source

def video(source):
    # The input already includes the corrected sprite positioning and MXU16 hooks.
    start=source.index('static inline void draw_fix_char(')
    end=source.index('\n#include "render_selftest.h"')
    extra=source[start:source.index('\nvoid init_video(',start)]
    for name in ('draw_screen_scanline','draw_screen','draw_fix_char','draw_scanline_tile_50','draw_scanline_tile_25','draw_scanline_tile','draw_tile_50','draw_tile_25','draw_tile'):
        extra=re.sub(r'\b'+name+r'\b',name+'32',extra)
    extra=extra.replace('unsigned short','uint32_t').replace('current_pc_pal','h1_pc_pal32')
    extra=extra.replace('  h1_render_begin();','')
    prefix='''
#include "palette32.h"
#include "present.h"
extern unsigned h1_mxu_control(void);
static uint32_t *h1_pc_pal32;
static inline uint32_t h1_blend32_50(uint32_t a,uint32_t b){return h1_expand565(BLEND16_50(h1_pack565(a),h1_pack565(b)));}
static inline uint32_t h1_blend32_25(uint32_t a,uint32_t b){return h1_expand565(BLEND16_25(h1_pack565(a),h1_pack565(b)));}
#define H1_NORMAL_TILE32 1
#define RENAME(name) name##_tile32
#define PUTPIXEL(dst,src) dst=src
#include "video_template32.h"
#undef H1_NORMAL_TILE32
#define RENAME(name) name##_tile_5032
#define PUTPIXEL(dst,src) dst=h1_blend32_50(src,dst)
#include "video_template32.h"
#define RENAME(name) name##_tile_2532
#define PUTPIXEL(dst,src) dst=h1_blend32_25(src,dst)
#include "video_template32.h"
'''
    original=source[:end].replace('void draw_screen(void)','static void draw_screen16(void)').replace('void draw_screen_scanline(int start_line, int end_line, int refresh)','static void draw_screen_scanline16(int start_line, int end_line, int refresh)')
    wrapper='''
void draw_screen(void){
 h1_palette32_surface(h1_palette32_enabled);
 h1_pc_pal32=h1_palette32_current();
 if(h1_palette32_enabled)draw_screen32();else draw_screen16();
}
void draw_screen_scanline(int start_line,int end_line,int refresh){
 h1_palette32_surface(h1_palette32_enabled);
 h1_pc_pal32=h1_palette32_current();
 if(h1_palette32_enabled)draw_screen_scanline32(start_line,end_line,refresh);
 else draw_screen_scanline16(start_line,end_line,refresh);
}
'''
    return original+prefix+extra+wrapper+source[end:]+'\n#include "palette32_selftest.h"\n'

def memory(source):
    # Match actual stores, including transparent-entry magenta and both banks.
    pattern=r'(?m)^(\s*)((?:pc_pal[12]|current_pc_pal)\[[^\n=]+\]) = ([^;\n]+);'
    source,n=re.subn(pattern,lambda m:m[1]+'h1_palette32_store(&'+m[2]+', '+m[3]+');',source)
    assert n==6,n
    return '#include "palette32.h"\n'+source
