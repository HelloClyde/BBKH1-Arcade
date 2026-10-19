"""Audited build-copy hooks for MXU1 full-size normal sprite tiles."""
from fast_render import direct_tiles

def patch_template(source):
    source=direct_tiles(source)
    marker='  gfxdata = (unsigned int *)&memory.rom.tiles.p[ tileno << 7];'
    assert source.count(marker)==1
    hook='''
#ifdef H1_NORMAL_TILE
  if(h1_render_fast && zx==16 && zy==16 && buffer->pitch==704){
    if(h1_render_tile16((unsigned short*)bmp+sy*352+sx,gfxdata,paldata,color,
                     (xflip?1:0)|(yflip?2:0)))return;
  }
#endif
'''
    return '#include "h1_render.h"\n'+source.replace(marker,marker+hook,1)

def patch_video(source):
    marker='#define RENAME(name) name##_tile\n'
    assert source.count(marker)==1
    source=source.replace(marker,'#define H1_NORMAL_TILE 1\n'+marker,1)
    marker='#define RENAME(name) name##_tile_50'
    source=source.replace(marker,'#undef H1_NORMAL_TILE\n'+marker,1)
    for signature in ('void draw_screen(void)','void draw_screen_scanline(int start_line, int end_line, int refresh)'):
        marker=signature+'\n{';assert source.count(marker)==1
        source=source.replace(marker,marker+'\n  h1_render_begin();',1)
    return source
