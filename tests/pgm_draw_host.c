/* Generated graphics differential test; no guest CPU or device emulation. */
#define H1_SDK_H
#define H1_MXU 0
static unsigned h1_raw_tick_80hz(void){return 0;}
#include "../src/pgm.c"
#include <assert.h>
#include <stdarg.h>
void h1_arcade_escape(int e){fprintf(stderr,"PGM error %d\n",e);abort();}
void h1_arcade_log(const char *s,...){(void)s;}
static unsigned seed=4740;
static unsigned rng(void){seed=seed*1664525u+1013904223u;return seed;}
int main(void){
 romfile=tmpfile();assert(romfile);uint8_t *raw=malloc(3*1024*1024);
 for(unsigned i=0;i<3*1024*1024;i++)raw[i]=rng()>>24;
 memset(raw,255,32);
 parts[PGM_TILES]=(pgm_part){0,131072,0,0};parts[PGM_COLOR]=(pgm_part){1024*1024,1024*1024,0,0};parts[PGM_MASK]=(pgm_part){2*1024*1024,1024*1024,0,0};
 for(unsigned i=0;i<32;i++){unsigned off=i*2048;store(raw+2*1024*1024+off,i*8192);store(raw+2*1024*1024+off+2,0);}
 assert(fwrite(raw,1,3*1024*1024,romfile)==3*1024*1024);free(raw);
 cache=malloc(SLOTS*4096);memset(tags,255,sizeof tags);
 tile_pixels=malloc(TILE_SLOTS*1024);tile_tags=malloc(TILE_SLOTS*4);memset(tile_tags,255,TILE_SLOTS*4);
 text_pixels=malloc(512*72);text_tags=malloc(512*4);memset(text_tags,255,512*4);
 sprite_capacity=1024*1024;sprite_pixels=malloc(sprite_capacity);sprite_tags=malloc(SPRITE_SLOTS*sizeof *sprite_tags);memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);
 bg=calloc(1,0x4000);tx=calloc(1,0x2000);rows=calloc(1,0x800);vreg=calloc(1,0x10000);
 uint32_t *actual=malloc(448*224*4);uint16_t *expected=malloc(448*224*2);
 colors=malloc(4096*4);colors565=malloc(4096*2);
 for(unsigned i=0;i<4096;i++){colors565[i]=rng();colors[i]=expand(colors565[i]);}
 for(unsigned frame=0;frame<160;frame++){
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
 }
 pixels=actual;present_check();for(unsigned i=0;i<3;i++)assert(h1_pgm_draw_probe.present_differences[i]==0);
 printf("160 generated frames exactly match reference, including zoom/flip/clipping and partial last tile; 3 presenter modes match. Sprite hits=%u misses=%u tile hits=%u misses=%u\n",h1_pgm_perf.sprite_hits,h1_pgm_perf.sprite_misses,h1_pgm_perf.tile_hits,h1_pgm_perf.tile_misses);
 return 0;
}
