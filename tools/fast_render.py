"""Optional build-copy changes to GnGeo's drawing template."""

def direct_tiles(original):
    assert original.count('  tileno = tileno % memory.nb_of_tiles;')==2
    return original.replace('  tileno = tileno % memory.nb_of_tiles;',
                            '  /* H1 caller has already checked the global tile; cache-local index stays in range. */')

def patch_template(original):
    head='''/* H1: exact opaque-row path; transparent and zoomed rows keep upstream. */
#include "h1_render.h"
static inline int RENAME(opaque_row)(const unsigned *gfx,unsigned short *dst,const unsigned *pal,int flip){
 unsigned a=gfx[0],b=gfx[1];
 if((((a-0x11111111u)&~a)|((b-0x11111111u)&~b))&0x88888888u)return 0;
#if H1_PROFILE
 ++h1_render_stats.opaque_rows;
#endif
 if(flip){
'''
    for flip in (True,False):
        for x in range(16):
            source=15-x if flip else x;word='a' if source<8 else 'b';shift=28-(source%8)*4
            head+=f'  PUTPIXEL(dst[{x}],pal[({word}>>{shift})&15]);\n'
        head+=' }else{\n' if flip else ' }\n return 1;\n}\n'
    result=direct_tiles(original)
    token='          if(gfxdata[1] || gfxdata[0]) {'
    assert result.count(token)==4
    parts=result.split(token);result=parts[0]
    for i,tail in enumerate(parts[1:]):
        result+=f'          if(h1_render_fast && RENAME(opaque_row)(gfxdata,br,paldata,{int(i<2)})){{}}\n          else if(gfxdata[1] || gfxdata[0]) {{'+tail
    start=result.index('static inline void RENAME(draw_scanline)')
    token='  if(zx == 16) {';at=result.index(token,start)
    result=result[:at]+'''  if(zx==16 && h1_render_fast && RENAME(opaque_row)(gfxdata,
       (unsigned short*)bmp+line*(buffer->pitch>>1)+sx,paldata,xflip))return;
'''+result[at:]
    return head+'\n'+result
