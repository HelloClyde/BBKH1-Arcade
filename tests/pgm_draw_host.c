/* Generated graphics differential test; no guest CPU or device emulation. */
#define H1_SDK_H
#define H1_MXU 0
static unsigned h1_raw_tick_80hz(void){return 0;}
#include "../src/pgm.c"
#include "../src/pgm_clear.h"
void h1_pgm_bg_mxu_span(uint32_t *dst,const uint8_t *src,const uint32_t *pal,unsigned width,unsigned height,unsigned flags){
 background_opaque_span_c(dst,src,pal,width,height,flags);
}
#include "../src/pgm_bg_test.c"
#include <assert.h>
#include <stdarg.h>
void h1_arcade_escape(int e){fprintf(stderr,"PGM error %d\n",e);abort();}
void h1_arcade_log(const char *s,...){(void)s;}
/* Retained write handlers contain I/O branches. These must never run in this
 * graphics-only fixture; no guest processor or sound device is emulated. */
unsigned char mame_z80mem[65536];
void cpu_z80_nmi(void){assert(!"unexpected Z80 NMI");}
void z80_reset(void *p){(void)p;assert(!"unexpected Z80 reset");}
void z80_set_irq_callback(int(*p)(int)){(void)p;assert(!"unexpected Z80 IRQ setup");}
void ics2115_reset(void){assert(!"unexpected sound reset");}
void ics2115_soundlatch_w(int a,unsigned short v){(void)a;(void)v;assert(!"unexpected sound latch write");}
void h1_pgm_prot_write(unsigned a,unsigned v){(void)a;(void)v;assert(!"unexpected protection write");}
static unsigned seed=4740;
static unsigned rng(void){seed=seed*1664525u+1013904223u;return seed;}
static unsigned cache_scenes,cache_passes,epoch_locations,clear_passes;
static void same_pixels(const uint32_t *actual,const uint16_t *expected){
 for(unsigned i=0;i<WIDTH*HEIGHT;i++)assert(actual[i]==expand(expected[i]));
}
static void clear_compare(uint32_t *actual,const uint16_t *expected){
 /* The original packed-ROM renderer is an independent coverage oracle.
  * Giving it an all-zero palette distinguishes every opaque pixel from
  * the 0xffff sentinel, even when the game's actual palette contains white. */
 static uint16_t *coverage,*zero_palette;
 if(!coverage){coverage=malloc(WIDTH*HEIGHT*2);zero_palette=calloc(4096,2);assert(coverage&&zero_palette);}
 uint16_t *saved_colors=colors565,*saved_reference=reference_pixels;
 colors565=zero_palette;reference_pixels=coverage;
 for(unsigned i=0;i<WIDTH*HEIGHT;i++)coverage[i]=0xffff;
 background_reference();colors565=saved_colors;reference_pixels=saved_reference;
 pixels=actual;for(unsigned i=0;i<WIDTH*HEIGHT;i++)actual[i]=0xdead0000u+(i&65535);
 h1_pgm_clear_stats.enabled=1;background_clear_fast(colors[0x3ff]);h1_pgm_clear_stats.enabled=0;
 unsigned written=0;
 for(unsigned i=0;i<WIDTH*HEIGHT;i++){
  if(coverage[i]==0xffff){assert(actual[i]==colors[0x3ff]);++written;}
  else assert(actual[i]==0xdead0000u+(i&65535));
 }
 assert(h1_pgm_clear_stats.written_pixels==written);
 assert(h1_pgm_clear_stats.opaque_pixels+h1_pgm_clear_stats.empty_pixels+h1_pgm_clear_stats.mixed_pixels==WIDTH*HEIGHT);
 /* Statistics-off execution must perform exactly the same writes. */
 for(unsigned i=0;i<WIDTH*HEIGHT;i++)actual[i]=0xdead0000u+(i&65535);
 unsigned calls=h1_pgm_clear_stats.calls;background_clear_fast(colors[0x3ff]);assert(h1_pgm_clear_stats.calls==calls);
 for(unsigned i=0;i<WIDTH*HEIGHT;i++)assert(actual[i]==(coverage[i]==0xffff?colors[0x3ff]:0xdead0000u+(i&65535)));
 sprite_index=0;sprite_layer(1);background_fast();sprite_layer(0);text_layer_fast();same_pixels(actual,expected);
 ++clear_passes;
}
static void epoch_case(unsigned a,unsigned dirty){
 unsigned before=bg_epoch,value=word(mapped(a));
 h1_pgm_write16(a,value);assert(bg_epoch==before);
 h1_pgm_write16(a,value|0x10000);assert(bg_epoch==before);
 h1_pgm_write16(a,value^0x55aa);assert(bg_epoch==before+dirty);before=bg_epoch;
 for(unsigned lane=0;lane<2;lane++){
  unsigned b=a+lane,byte=mapped(a)[lane^1];
  h1_pgm_write8(b,byte);assert(bg_epoch==before);
  h1_pgm_write8(b,byte|0x100);assert(bg_epoch==before);
  h1_pgm_write8(b,byte^0x5a);assert(bg_epoch==before+dirty);before=bg_epoch;
 }
 h1_pgm_write16(a,value);assert(bg_epoch==before+dirty);
 ++epoch_locations;
}
static void cache_compare(uint32_t *actual,uint16_t *expected){
 reference_pixels=expected;draw_reference();pixels=actual;
 h1_pgm_layers_mode=2;h1_pgm_sprite_mode=3;draw();same_pixels(actual,expected);
 assert(bg_cached_epoch==bg_epoch);
 unsigned hits=h1_pgm_perf.tile_hits,misses=h1_pgm_perf.tile_misses;
 draw();same_pixels(actual,expected);
 assert(hits==h1_pgm_perf.tile_hits&&misses==h1_pgm_perf.tile_misses);
 ++cache_scenes;cache_passes+=2;
 clear_compare(actual,expected);
}
static void cache_regression(uint32_t *actual,uint16_t *expected){
 static const unsigned dirty[]={0x900000,0x900002,0x903ffe,0x907000,0x9077fe,0xa00800,0xa00ffe,0xb02000,0xb03000};
 static const unsigned clean[]={0x800000,0x8ffffe,0x904000,0x905ffe,0xa00000,0xa007fe,0xa01000,0xa011fe,0xb01ffe,0xb02002,0xb02ffe,0xb03002,0xb05000,0xb06000};
 for(unsigned i=0;i<sizeof dirty/sizeof *dirty;i++)epoch_case(dirty[i],1);
 for(unsigned i=0;i<sizeof clean/sizeof *clean;i++)epoch_case(clean[i],0);
 /* Known opaque tile 1 and transparent tile 2 make both cached composition
  * branches observable, including the covered low-priority sprite path. */
 uint8_t raw[1920];memset(raw,0,sizeof raw);
 for(unsigned tile=0;tile<3;tile++)for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){
  unsigned v=(tile==1||(tile==2&&(y==0||(y==2&&(x&1)))))?31:2;
  unsigned bit=x*5,off=tile*640+y*20+(bit>>3),shift=bit&7;
  raw[off]|=v<<shift;if(shift>3)raw[off+1]|=v>>(8-shift);
 }
 assert(!fseek(romfile,640,SEEK_SET)&&fwrite(raw,1,sizeof raw,romfile)==sizeof raw);fflush(romfile);
 memset(tags,255,sizeof tags);memset(tile_tags,255,TILE_SLOTS*4);memset(text_tags,255,512*4);
 memset(rows,0,0x800);memset(vreg,0,0x10000);memset(tx,0,0x2000);memset(sprites,0,sizeof sprites);
 for(unsigned at=0;at<0x4000;at+=4){store(bg+at,1);store(bg+at+2,0);}
 for(unsigned i=0;i<0x900;i++){unsigned value=(i*97)&0x7fff;store(palette+i*2,value);palette_write(0xa00000+i*2,value);}
 sprites[0]=8;sprites[1]=8;sprites[2]=0x80;sprites[3]=0;sprites[4]=(1<<9)|16;
 sprites[5]=40;sprites[6]=40;sprites[7]=0;sprites[8]=1024;sprites[9]=(1<<9)|16;
 ++bg_epoch;cache_compare(actual,expected);assert(bg_opaque);
 uint32_t color=actual[0];
 h1_pgm_write16(0xa00804,word(palette+0x804)^0x7c1f);cache_compare(actual,expected);assert(actual[0]!=color);
 h1_pgm_write8(0xa00804,palette[0x805]^0x40);cache_compare(actual,expected);
 h1_pgm_write8(0xa00805,palette[0x804]^0x1f);cache_compare(actual,expected);
 h1_pgm_write16(0x900000,2);cache_compare(actual,expected);assert(!bg_opaque);
 h1_pgm_write8(0x900007,0x42);cache_compare(actual,expected);
 h1_pgm_write16(0x907000,17);cache_compare(actual,expected);
 h1_pgm_write8(0xb02001,1);cache_compare(actual,expected);
 h1_pgm_write16(0xb03000,19);cache_compare(actual,expected);
 unsigned epoch=bg_epoch;
 h1_pgm_write16(0x904000,20);assert(bg_epoch==epoch);cache_compare(actual,expected);
 h1_pgm_write16(0xa01004,word(palette+0x1004)^0x7fff);assert(bg_epoch==epoch);cache_compare(actual,expected);
 h1_pgm_write8(0xa00001,palette[0]^0x1f);assert(bg_epoch==epoch);cache_compare(actual,expected);
 h1_pgm_write16(0x900000,1);cache_compare(actual,expected);assert(bg_opaque);
 h1_pgm_write16(0x907000,0);h1_pgm_write16(0xb02000,0);h1_pgm_write16(0xb03000,0);
 h1_pgm_write16(0x900000,3);cache_compare(actual,expected);
 assert(tile_row_masks[3*2]==0xfffffffeu&&tile_row_masks[3*2+1]==0xfffffffau);
 h1_pgm_write16(0x900002,0xc0);cache_compare(actual,expected);
 /* Bounds are checked before consulting row opacity, including a final
  * tile whose last partially present packed row must not be drawn. */
 static const unsigned tail[]={19,20,39,40,53,639};
 for(unsigned i=0;i<sizeof tail/sizeof *tail;i++){
  parts[PGM_TILES].size=3*640+tail[i];memset(tile_tags,255,TILE_SLOTS*4);memset(text_tags,255,512*4);
  ++bg_epoch;cache_compare(actual,expected);
 }
}
static unsigned presenter_passes;
static void presenter_regression(void){
 const unsigned count=480*272,modes[]={0,0,1,1,2,2,0,0,2,1,0};
 uint32_t *first=malloc((count+2)*4),*second=malloc((count+2)*4),*reference=malloc(count*4);
 assert(first&&second&&reference);first[0]=second[0]=0x12345678;first[count+1]=second[count+1]=0x89abcdef;
 unsigned saved_present=h1_pgm_present_mode,saved_margin=h1_pgm_margin_mode,saved_scale=h1_pgm_scale_mode;
 for(unsigned present=0;present<2;present++)for(unsigned margin=0;margin<2;margin++)for(unsigned scale=0;scale<2;scale++){
  h1_pgm_present_mode=present;h1_pgm_margin_mode=margin;h1_pgm_scale_mode=scale;
  memset(first+1,0xa5,count*4);h1_pgm_present_reset();
  for(unsigned step=0;step<sizeof modes/sizeof modes[0];step++){
   unsigned mode=modes[step];uint32_t *out=first+1;
   /* Update active pixels while the same target/mode keeps its margins. */
   for(unsigned i=0;i<448*224;i++)pixels[i]=rng();
   present_reference(reference,mode);
   h1_pgm_present(out,mode);assert(!memcmp(reference,out,count*4));++presenter_passes;
   h1_pgm_present(out,mode);assert(!memcmp(reference,out,count*4));++presenter_passes;
   /* A menu dirties the same allocation, including its black side/top
    * margins. Explicit reset must force a complete restoration. */
   memset(out,0x5a,count*4);h1_pgm_present_reset();h1_pgm_present(out,mode);
   assert(!memcmp(reference,out,count*4));++presenter_passes;
   /* Switching the target must also restore it, without an explicit reset. */
   memset(second+1,0x3c,count*4);h1_pgm_present(second+1,mode);
   assert(!memcmp(reference,second+1,count*4));++presenter_passes;
   h1_pgm_present(out,mode);assert(!memcmp(reference,out,count*4));++presenter_passes;
   if(!margin){
    memset(out,0xc3,count*4);h1_pgm_present(out,mode);
    assert(!memcmp(reference,out,count*4));++presenter_passes;
   }
   assert(first[0]==0x12345678&&second[0]==0x12345678&&first[count+1]==0x89abcdef&&second[count+1]==0x89abcdef);
  }
 }
 h1_pgm_present_mode=saved_present;h1_pgm_margin_mode=saved_margin;h1_pgm_scale_mode=saved_scale;
 h1_pgm_present_reset();free(first);free(second);free(reference);
}
int main(void){
 assert(h1_pgm_bg_selftest());
 romfile=tmpfile();assert(romfile);uint8_t *raw=malloc(3*1024*1024);
 for(unsigned i=0;i<3*1024*1024;i++)raw[i]=rng()>>24;
 memset(raw,255,32);
 parts[PGM_TILES]=(pgm_part){0,131072,0,0};parts[PGM_COLOR]=(pgm_part){1024*1024,1024*1024,0,0};parts[PGM_MASK]=(pgm_part){2*1024*1024,1024*1024,0,0};
 for(unsigned i=0;i<32;i++){unsigned off=i*2048;store(raw+2*1024*1024+off,i*8192);store(raw+2*1024*1024+off+2,0);}
 assert(fwrite(raw,1,3*1024*1024,romfile)==3*1024*1024);free(raw);
 cache=malloc(SLOTS*4096);memset(tags,255,sizeof tags);
 tile_pixels=malloc(TILE_SLOTS*(1024+8));tile_row_masks=(uint32_t*)(tile_pixels+TILE_SLOTS*1024);
 tile_tags=malloc(TILE_SLOTS*4);memset(tile_tags,255,TILE_SLOTS*4);
 text_pixels=malloc(512*72);text_tags=malloc(512*4);memset(text_tags,255,512*4);
 sprite_capacity=1024*1024;sprite_pixels=malloc(sprite_capacity);sprite_tags=malloc(SPRITE_SLOTS*sizeof *sprite_tags);memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);
 bg=calloc(1,0x4000);tx=calloc(1,0x2000);rows=calloc(1,0x800);vreg=calloc(1,0x10000);palette=calloc(1,0x1200);ram=calloc(1,0x20000);
 uint32_t *actual=malloc(448*224*4);uint16_t *expected=malloc(448*224*2);
 colors=malloc(4096*4);colors565=malloc(4096*2);
 for(unsigned i=0;i<4096;i++){colors565[i]=rng();colors[i]=expand(colors565[i]);}
 for(unsigned frame=0;frame<160;frame++){
  ++bg_epoch;
  for(unsigned i=0;i<0x4000;i+=4){store(bg+i,(i%19==0)?204:rng()%32);store(bg+i+2,rng()&255);}
  for(unsigned i=0;i<0x2000;i+=4){store(tx+i,i%3?rng()%4096:0);store(tx+i+2,rng()&255);}
  for(unsigned i=0;i<0x800;i+=2)store(rows+i,rng());
  for(unsigned i=0;i<0x10000;i+=2)store(vreg+i,rng());
  memset(sprites,0,sizeof sprites);
  for(unsigned i=0;i<24;i++){
   uint16_t *s=sprites+i*5;unsigned gx=rng()&1,gy=rng()&1;
   s[0]=((int)(rng()%800)-180)&2047;s[0]|=(rng()%16)<<11;s[0]|=gx<<15;
   s[1]=((int)(rng()%500)-120)&1023;s[1]|=(rng()%16)<<11;s[1]|=gy<<15;
   s[2]=(rng()&0x7f80);if(i<8)s[2]|=0x80;else s[2]&=~0x80;
   s[3]=(i%32)*1024;s[4]=((1+rng()%4)<<9)|(1+rng()%32);
  }
  /* Include identity zoom explicitly: random 32-bit zoom masks almost
   * never exercise the contiguous source path. */
  if(!(frame&3))memset(vreg+0x1000,0,17*4);
  if(frame==159){sprites[0]=0; sprites[1]=0; sprites[2]=0; sprites[3]=0; sprites[4]=(63<<9)|511;}
  reference_pixels=expected;draw_reference();pixels=actual;h1_pgm_sprite_mode=0;draw();
  unsigned n=0;for(unsigned i=0;i<448*224;i++)if(actual[i]!=expand(expected[i])){if(n++<3)fprintf(stderr,"frame=%u x=%u y=%u %06x/%06x\n",frame,i%448,i/448,actual[i],expand(expected[i]));}
  if(n){
   fprintf(stderr,"different pixels=%u\n",n);return 1;
  }
  /* Same scene a second time must reuse decoded entries without changing
   * their palette-independent indices, even after direct-map collisions. */
  for(unsigned mode=1;mode<4;mode++){h1_pgm_sprite_mode=mode;draw();for(unsigned i=0;i<448*224;i++)assert(actual[i]==expand(expected[i]));}
  if(frame<32){
   /* Exercise the grouping paths with constant, piecewise and arbitrary
    * rowscroll. Check all layer paths with the same graphics/reference. */
   if(frame%3!=2)for(unsigned i=0;i<0x800;i+=2)store(rows+i,frame%3?(i/32)*17:0);
   ++bg_epoch;reference_pixels=expected;draw_reference();pixels=actual;h1_pgm_sprite_mode=3;h1_pgm_bg_mxu_mode=frame&1;
   for(unsigned mode=0;mode<3;mode++){h1_pgm_layers_mode=mode;draw();same_pixels(actual,expected);}
   h1_pgm_layers_mode=2;
   clear_compare(actual,expected);
  }
 }
 cache_regression(actual,expected);
 pixels=actual;present_check();for(unsigned i=0;i<3;i++)assert(h1_pgm_draw_probe.present_differences[i]==0);
 presenter_regression();printf("%u presenter passes match across all modes, target changes, steady output and explicit same-target UI invalidation; guards intact.\n",presenter_passes);
 printf("160 generated frames / 736 renderer passes exactly match reference, including all 3 layer modes, rowscroll grouping, zoom/flip/clipping and partial last tile; %u cache-invalidation scenes / %u passes and %u word/byte epoch locations match; %u selective-clear passes preserve all opaque pixels and match final reference frames, debug on/off coverage matches; 512 opaque-span C fallback fixtures match (native MXU checks run separately on H1); 3 presenter modes match. Sprite hits=%u misses=%u tile hits=%u misses=%u\n",cache_scenes,cache_passes,epoch_locations,clear_passes,h1_pgm_perf.sprite_hits,h1_pgm_perf.sprite_misses,h1_pgm_perf.tile_hits,h1_pgm_perf.tile_misses);
 return 0;
}
