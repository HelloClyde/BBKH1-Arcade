/* Included after the real drawing functions in the build copy of video.c.
 * This runs before cartridge loading, on the physical H1, with original data. */
#if H1_CPU_SELFTEST && (H1_FAST_RENDER || H1_MXU_RENDER)
#include "h1_sdk.h"
static unsigned h1_render_hash(const void *data,unsigned bytes){
 const unsigned char *p=data;unsigned hash=2166136261u;
 for(unsigned i=0;i<bytes;i++)hash=(hash^p[i])*16777619u;return hash;
}
#if H1_VISIBLE_RENDER
static uint16_t h1_visible_reference[320*224];
static unsigned h1_visible_compare(unsigned capture){
 unsigned n=0;
 for(unsigned y=0;y<224;y++){
  const uint16_t *p=(const uint16_t*)((const uint8_t*)buffer->pixels+(y+16)*buffer->pitch)+16;
  for(unsigned x=0;x<320;x++){
   if(capture)h1_visible_reference[y*320+x]=p[x];
   else n+=h1_visible_reference[y*320+x]!=p[x];
  }
 }
 return n;
}
#endif
/* Called only from the owning app's pause loop. No guest CPU/audio advances. */
void h1_render_benchmark(void){
 unsigned loops=h1_render_bench.request,saved=h1_render_fast;
#if H1_SPEED_CORE
 unsigned saved_compact=h1_render_compact;
#endif
#if H1_VISIBLE_RENDER
 unsigned saved_visible=h1_render_visible;
#endif
 h1_render_bench.request=0;h1_render_bench.state=1;
 if(!loops||loops>1000){h1_render_bench.state=3;return;}
 unsigned input=h1_render_hash(memory.vid.ram,sizeof memory.vid.ram)^h1_render_hash(current_pc_pal,4096*4);
 h1_render_bench_only=1;
 h1_render_bench.loops=loops*2;h1_render_bench.original_ticks=h1_render_bench.fast_ticks=0;
 h1_render_bench.pixel_differences=0;
 for(unsigned pass=0;pass<4;pass++){
  unsigned fast=pass==1||pass==2;
#if H1_SPEED_CORE
  h1_render_fast=1;h1_render_visible=2;h1_render_compact=fast;
#elif H1_VISIBLE_RENDER
  h1_render_fast=1;h1_render_visible=fast?2:0;
#else
  h1_render_fast=fast;
#endif
  draw_screen();
  unsigned start=h1_raw_tick_80hz();
  for(unsigned i=0;i<loops;i++){draw_screen();if(!(i&15))h1_arcade_check_stop();}
  unsigned elapsed=h1_raw_tick_80hz()-start;
  unsigned hash;
#if H1_VISIBLE_RENDER
  h1_render_bench.pixel_differences+=h1_visible_compare(pass==0);
  /* Hash actual output, row by row, independently of the saved reference. */
  unsigned actual=2166136261u;
  for(unsigned y=16;y<240;y++){
   const uint8_t *p=(const uint8_t*)buffer->pixels+y*buffer->pitch+32;
   for(unsigned x=0;x<640;x++)actual=(actual^p[x])*16777619u;
  }
  hash=actual;
#else
  hash=h1_render_hash(buffer->pixels,buffer->pitch*buffer->h);
#endif
  if(fast){h1_render_bench.fast_ticks+=elapsed;h1_render_bench.fast_hash=hash;}
  else{h1_render_bench.original_ticks+=elapsed;h1_render_bench.original_hash=hash;}
 }
 h1_render_bench.input_unchanged=input==(h1_render_hash(memory.vid.ram,sizeof memory.vid.ram)^h1_render_hash(current_pc_pal,4096*4));
#if H1_SPEED_CORE
 h1_render_compact=saved_compact;
#endif
#if H1_VISIBLE_RENDER
 h1_render_visible=saved_visible;
#endif
 h1_render_fast=saved;h1_render_bench_only=0;screen_flip();h1_render_bench.state=2;
}
int h1_render_selftest(void){
 static unsigned short reference[352*256+16],optimized[352*256+16];
 static unsigned graphics[128],palette[4096];
 typedef void(*draw_fn)(unsigned,int,int,int,int,int,int,int,unsigned char*);
 typedef void(*line_fn)(unsigned,int,int,int,int,int,int,unsigned char*);
 draw_fn draws[]={draw_tile,draw_tile_50,draw_tile_25};
 line_fn lines[]={draw_scanline_tile,draw_scanline_tile_50,draw_scanline_tile_25};
 SDL_Surface surface={.pixels=0,.pitch=704,.w=352,.h=256},*saved_buffer=buffer;
 unsigned *saved_pal=current_pc_pal;char saved_y[17],*saved_x=dda_x_skip,xskip[16];
 memcpy(saved_y,dda_y_skip,sizeof saved_y);buffer=&surface;unsigned seed=4740,failed=0;
 for(unsigned test=0;test<384+(H1_MXU_RENDER?512:0);test++){
  unsigned mode=test%3,g=test/3,zx=g&1?16:1+(g/2)%15,zy=1+(g/2)%16,xf=(g>>5)&1,yf=(g>>6)&1;
  if(test>=384){g=test-384;mode=0;zx=zy=16;xf=(g>>1)&1;yf=(g>>2)&1;}
  for(unsigned i=0;i<128;i++){
   seed=seed*1664525u+1013904223u;unsigned value=seed;
   if(g%6==0)value=0;if(g%6==1)value=0x11111111u;if(g%6==2)value|=0x11111111u;
   graphics[i]=value;
  }
  if(g%6==4)for(unsigned i=0;i<128;i+=2)graphics[i+1]=0u-graphics[i];
  for(unsigned i=0;i<4096;i++){seed=seed*1664525u+1013904223u;palette[i]=seed;}
  memory.rom.tiles.p=(uint8_t*)graphics;memory.nb_of_tiles=4;current_pc_pal=palette;
  dda_y_skip[0]=0;for(unsigned i=1;i<zy;i++)dda_y_skip[i]=i*16/zy-(i-1)*16/zy;
  for(unsigned i=0;i<16;i++)xskip[i]=i<zx;dda_x_skip=xskip;
  unsigned tile=(test>>1)&3,color=(test*37)&255,x=16+(test&15);
  for(unsigned scan=0;scan<2;scan++){
   memset(reference,test,sizeof reference);memcpy(optimized,reference,sizeof reference);
   h1_render_fast=0;
   if(scan)lines[mode](tile,zy-1,x,20,zx,color,xf,(unsigned char*)(reference+8));
   else draws[mode](tile,x,20,zx,zy,color,xf,yf,(unsigned char*)(reference+8));
   h1_render_fast=1;
#if H1_MXU_RENDER
   h1_render_begin();
#endif
   if(scan)lines[mode](tile,zy-1,x,20,zx,color,xf,(unsigned char*)(optimized+8));
   else draws[mode](tile,x,20,zx,zy,color,xf,yf,(unsigned char*)(optimized+8));
   ++h1_render_stats.tests;
   if(memcmp(reference,optimized,sizeof reference)){
    ++h1_render_stats.failures;failed=1;h1_arcade_log("RENDER_TEST_FAIL case=%u scan=%u mode=%u zx=%u zy=%u",test,scan,mode,zx,zy);break;
   }
  }
  if(failed)break;
 }
 buffer=saved_buffer;current_pc_pal=saved_pal;dda_x_skip=saved_x;memcpy(dda_y_skip,saved_y,sizeof saved_y);
 memset(&memory,0,sizeof memory);h1_render_fast=H1_FAST_RENDER||H1_MXU_RENDER;
 if(!failed)h1_arcade_log("RENDER_TEST_OK cases=%u full_framebuffer=1",h1_render_stats.tests);
 return !failed;
}
#endif
