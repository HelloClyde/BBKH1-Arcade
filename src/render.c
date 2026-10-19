/* Optional GnGeo row specialization controls. GPL-2.0-or-later. */
#include "h1_render.h"
#include <stdint.h>
#include <string.h>
#ifndef H1_MXU_RENDER
#define H1_MXU_RENDER 0
#endif
volatile unsigned h1_render_fast=H1_FAST_RENDER||H1_MXU_RENDER;
volatile unsigned h1_render_bench_only;
volatile unsigned h1_render_visible=H1_VISIBLE_RENDER?2:0;
volatile h1_render_stats_t h1_render_stats={0x31524452,1};
volatile h1_render_bench_t h1_render_bench={0x31425248,H1_SPEED_CORE?3:H1_VISIBLE_RENDER?2:1};
volatile h1_tile_mxu_stats_t h1_tile_mxu_stats={0x31544d58,2};
/* Aligned, cached RGB565 RAM, fixed 352-pixel pitch. No LCD or MXU state. */
void h1_render_clear_visible(void *pixels,unsigned color){
 uint32_t pair=(color&65535u)*65537u;
 uint32_t *p=(uint32_t*)((uint16_t*)pixels+16*352+16);
 for(unsigned y=0;y<224;y++,p+=176){
  for(unsigned x=0;x<160;x+=8){
   p[x]=pair;p[x+1]=pair;p[x+2]=pair;p[x+3]=pair;
   p[x+4]=pair;p[x+5]=pair;p[x+6]=pair;p[x+7]=pair;
  }
 }
}
#if H1_MXU_RENDER
extern void h1_mxu_tile16_palette(void*,const unsigned*,const unsigned*,unsigned);
#if H1_SPEED_CORE
volatile unsigned h1_render_compact=1;
extern void h1_mxu_tile16_palette_original(void*,const unsigned*,const unsigned*,unsigned);
#endif
extern int h1_sprite_tile_opaque(const unsigned*);
void h1_render_begin(void){}
int h1_render_tile16(void *dst,const unsigned *gfx,const unsigned *pal,unsigned color,unsigned flags){
 (void)color;
 if(!h1_sprite_tile_opaque(gfx))return 0;
#if H1_SPEED_CORE
 if(!h1_render_compact)h1_mxu_tile16_palette_original(dst,gfx,pal,flags);
 else
#endif
 h1_mxu_tile16_palette(dst,gfx,pal,flags);
 ++h1_tile_mxu_stats.tiles;
 return 1;
}
#endif
