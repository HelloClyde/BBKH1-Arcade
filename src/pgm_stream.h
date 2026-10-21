/* PGM scanline composition and immediate RGB32 LCD output.
 * Include after pgm_layers.h, pgm_clear.h and pgm_present_fast.h. The caller
 * declares pgm_stream_ready and the static lifecycle prototypes before draw.
 * No full-frame RGB surface is read or written by this path. Layers compose
 * an L1-sized source row and nearest-neighbour output samples it immediately;
 * the caller preserves the original frame's sprite snapshot.
 * GPL-2.0-or-later.
 */
#ifndef H1_PGM_STREAM_H
#define H1_PGM_STREAM_H

#include "pgm_stream_sprites.h"
#include "pgm_stream_layers.h"

volatile unsigned h1_pgm_stream_mode=1;
volatile struct {
 unsigned magic,version,prepares,prepared,frames,fallbacks,resets;
 unsigned source_rows,output_rows,reused_rows,lcd_pixels,full_repaints;
} h1_pgm_stream_stats={0x31534750,1};

static void pgm_stream_reset(void){
 pgm_stream_ready=0;++h1_pgm_stream_stats.resets;
}

static int pgm_stream_prepare(void){
 pgm_stream_ready=0;
 if(!h1_pgm_stream_mode)return 0;
 ++h1_pgm_stream_stats.prepares;phase(5);
 int sprites_ok=pgm_stream_sprites_prepare();
 int layers_ok=pgm_stream_layers_prepare();
 if(!sprites_ok||!layers_ok){++h1_pgm_stream_stats.fallbacks;phase(0);return 0;}
 pgm_stream_ready=1;++h1_pgm_stream_stats.prepared;phase(0);return 1;
}

static void pgm_stream_close(void){
 pgm_stream_reset();pgm_stream_sprites_close();pgm_stream_layers_close();
}

static void pgm_stream_present(uint32_t *native,unsigned mode){
 phase(6);
 uint32_t *lcd=native;uintptr_t address=(uintptr_t)native;
 unsigned cached=address>=0xa0000000u&&address<0xa4000000u&&!(address&255u)&&480u*272u*4u<=0xa4000000u-address;
 if(cached)lcd=(uint32_t*)((address&0x1fffffffu)|0x80000000u);
 unsigned w=mode==0?362:mode==1?448:480,h=mode==1?224:272;
 unsigned left=(480-w)/2,top=(272-h)/2;
 unsigned full=!h1_pgm_margin_mode||!pgm_present_valid||native!=pgm_present_target||mode!=pgm_present_last_mode;
 static uint16_t columns[480];static unsigned last_width;
 if(w!=448&&last_width!=w){
  /* Byte offsets let MIPS add each lookup directly to the row base,
   * removing a per-output-pixel shift without enlarging the map. */
  for(unsigned x=0;x<w;x++)columns[x]=(x*448/w)*4;
  last_width=w;
 }
 if(full&&(top||left))memset(lcd,0,(top*480+left)*4);
 uint32_t row[448] __attribute__((aligned(32)));
 uint32_t *dst=lcd+top*480+left;
 unsigned sy=0,fraction=0,last=~0u;
 for(unsigned y=0;y<h;y++,dst+=480){
  if(sy==last){
   pgm_present_copy(dst,dst-480,w);
  }else{
   phase(13);background_clear_fill(row,448,colors[0x3ff]);
   phase(10);pgm_stream_sprites_row(sy,row,1);
   phase(4);pgm_stream_background_row(sy,row);
   phase(10);pgm_stream_sprites_row(sy,row,0);
   phase(11);pgm_stream_text_row(sy,row);
   phase(6);
   if(w==448){pgm_present_copy(dst,row,448);}
#if defined(__mips__)
   else if(h1_pgm_scale_mode){
    if(w==362)h1_pgm_scale36232(dst,row);else h1_pgm_scale48032(dst,row);
   }
#endif
   else{
    const uint8_t *source=(const uint8_t *)row;
    unsigned x=0;
    for(;x+4<=w;x+=4){
     uint32_t a=*(const uint32_t *)(source+columns[x]),b=*(const uint32_t *)(source+columns[x+1]);
     uint32_t c=*(const uint32_t *)(source+columns[x+2]),d=*(const uint32_t *)(source+columns[x+3]);
     dst[x]=a;dst[x+1]=b;dst[x+2]=c;dst[x+3]=d;
    }
    for(;x<w;x++)dst[x]=*(const uint32_t *)(source+columns[x]);
   }
   last=sy;
  }
  if(full&&left){
   unsigned gap=y+1<h?480-w:480-left-w;
   memset(dst+w,0,gap*4);
  }
  fraction+=224;if(fraction>=h){fraction-=h;sy++;}
 }
 if(full&&top)memset(lcd+(top+h)*480,0,(272-top-h)*480*4);
#if defined(__mips__)
 /* Same verified 32-byte cache line publication and persistent black
  * margins as present_fast. Layers never call firmware while XR registers
  * are live; no interrupt or global cache configuration is changed. */
 if(((uintptr_t)lcd&0xe0000000u)==0x80000000u){
  __asm__ volatile("sync":::"memory");
  if(full||w==480){
   uintptr_t a=(uintptr_t)lcd&~31u,end=(uintptr_t)(lcd+480*272);
   for(;a<end;a+=32)__asm__ volatile("cache 0x15,0(%0)"::"r"(a):"memory");
  }else for(unsigned y=0;y<h;y++){
   uint32_t *active=lcd+(top+y)*480+left;
   uintptr_t a=(uintptr_t)active&~31u,end=(uintptr_t)(active+w);
   for(;a<end;a+=32)__asm__ volatile("cache 0x15,0(%0)"::"r"(a):"memory");
  }
  __asm__ volatile("sync":::"memory");
 }
#endif
 pgm_present_target=native;pgm_present_last_mode=mode;pgm_present_valid=1;
 ++h1_pgm_stream_stats.frames;h1_pgm_stream_stats.source_rows+=224;
 h1_pgm_stream_stats.output_rows+=h;h1_pgm_stream_stats.reused_rows+=h-224;
 h1_pgm_stream_stats.lcd_pixels+=w*h;if(full)++h1_pgm_stream_stats.full_repaints;
 phase(0);
}

#endif
