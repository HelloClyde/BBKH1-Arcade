/* Whole-frame/streaming differential graphics tests. No guest CPU executes.
 * Reuse the existing generated ROM/cache setup and independent packed-ROM
 * reference renderer without duplicating its fixture initialization. */
#include <stdlib.h>
static unsigned stream_text_fail_realloc;
static void *stream_text_realloc(void *p,size_t size){return stream_text_fail_realloc?0:realloc(p,size);}
static size_t stream_sprite_fail_malloc_size;
static void *stream_sprite_malloc(size_t size){return stream_sprite_fail_malloc_size&&size==stream_sprite_fail_malloc_size?0:malloc(size);}
#define malloc stream_sprite_malloc
#define realloc stream_text_realloc
#define main pgm_draw_existing_main
#include "pgm_draw_host.c"
#undef main
#undef realloc
#undef malloc
#include "../src/pgm_stream.h"

static unsigned stream_scenes,stream_lcd_passes,stream_fallback_modes,stream_wrap_modes,stream_snapshot_modes,stream_mode_switches;
static unsigned stream_sparse_text_blocks;
static unsigned stream_text_oom_modes;
static unsigned stream_sprite_pool_cases,stream_sprite_pool_rows;
static uint16_t *stream_reference565;
static uint32_t *stream_frame,*stream_lcd_reference,*stream_first,*stream_second;
#define STREAM_LCD_WORDS (480u*272u)
static void stream_lcd_check(const uint32_t *allocation){
 assert(allocation[0]==0x12345678&&allocation[STREAM_LCD_WORDS+1]==0x89abcdef);
 for(unsigned i=0;i<STREAM_LCD_WORDS;i++)if(allocation[i+1]!=stream_lcd_reference[i]){
  fprintf(stderr,"stream scene=%u LCD x=%u y=%u actual=%08x expected=%08x\n",stream_scenes,i%480,i/480,allocation[i+1],stream_lcd_reference[i]);abort();
 }
 ++stream_lcd_passes;
}
static void stream_case(unsigned expect_prepare,unsigned force_wrap,unsigned snapshot){
 reference_pixels=stream_reference565;draw_reference();pixels=stream_frame;
 draw();same_pixels(stream_frame,stream_reference565);
 for(unsigned mode=0;mode<3;mode++){
  present_reference(stream_lcd_reference,mode);
  memset(stream_first+1,0xa5,STREAM_LCD_WORDS*4);h1_pgm_present_reset();
  if(force_wrap){sprite_used=sprite_capacity-16;memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);}
  int prepared=pgm_stream_prepare();assert(prepared==(int)expect_prepare);
  if(prepared)assert(pgm_stream_text_plan_ready);
  if(!prepared){++stream_fallback_modes;draw();}
  if(force_wrap){assert(prepared&&sprite_used<sprite_capacity-16);++stream_wrap_modes;}
  uint16_t saved_sprites[0x500];
  if(snapshot){assert(prepared);memcpy(saved_sprites,sprites,sizeof sprites);memset(sprites,0,sizeof sprites);++stream_snapshot_modes;}
  h1_pgm_present(stream_first+1,mode);stream_lcd_check(stream_first);
  /* A second present uses the prepared rows and persistent margins. */
  h1_pgm_present(stream_first+1,mode);stream_lcd_check(stream_first);
  if(snapshot)memcpy(sprites,saved_sprites,sizeof sprites);
  /* Menu-style overwrite on the same target must restore every pixel via
   * the actual frontend refresh entry, including its stream reset hook. */
  memset(stream_first+1,0x5a,STREAM_LCD_WORDS*4);h1_pgm_refresh(stream_first+1,mode);
  assert(pgm_stream_ready==expect_prepare);stream_lcd_check(stream_first);
  /* The mode is unchanged, but an unrelated framebuffer needs new margins. */
  memset(stream_second+1,0x3c,STREAM_LCD_WORDS*4);h1_pgm_present(stream_second+1,mode);stream_lcd_check(stream_second);
  h1_pgm_present(stream_first+1,mode);stream_lcd_check(stream_first);
  /* Streaming leaves the full RGB allocation intact; no intermediate
   * framebuffer is secretly updated to make the LCD comparison pass. */
  same_pixels(stream_frame,stream_reference565);
 }
 ++stream_scenes;
}
static void stream_random_scene(unsigned frame){
 parts[PGM_TILES].size=frame%5==0?131053:131072;
 memset(tile_tags,255,TILE_SLOTS*4);memset(text_tags,255,512*4);++bg_epoch;
 for(unsigned i=0;i<0x4000;i+=4){store(bg+i,i%19==0?204:rng()%32);store(bg+i+2,rng()&255);}
 for(unsigned i=0;i<0x2000;i+=4){store(tx+i,i%3?rng()%4096:0);store(tx+i+2,rng()&255);}
 for(unsigned i=0;i<0x800;i+=2)store(rows+i,frame%3==0?0:frame%3==1?(i/32)*17:rng());
 for(unsigned i=0;i<0x10000;i+=2)store(vreg+i,rng());
 memset(sprites,0,sizeof sprites);
 for(unsigned i=0;i<24;i++){
  uint16_t *s=sprites+i*5;
  s[0]=((int)(rng()%800)-180)&2047;s[0]|=(rng()%16)<<11;s[0]|=((rng()>>24)&1)<<15;
  s[1]=((int)(rng()%500)-120)&1023;s[1]|=(rng()%16)<<11;s[1]|=((rng()>>24)&1)<<15;
  s[2]=rng()&0x7f80;if(i<8)s[2]|=0x80;else s[2]&=~0x80;
  s[3]=i*1024;s[4]=((1+rng()%4)<<9)|(1+rng()%32);
 }
 if(!(frame&3))memset(vreg+0x1000,0,17*4);
 if(frame%7==0){
  /* An invalid foreground descriptor still ends the low-priority prefix;
   * later descriptors carrying the priority bit now belong to the high layer. */
  sprites[7]&=~0x80;sprites[9]=8;sprites[12]|=0x80;
 }
 h1_pgm_sprite_mode=frame&3;h1_pgm_bg_mxu_mode=(frame>>1)&1;
}
static void stream_write_tile(unsigned tile,unsigned cropped){
 uint8_t packed[640];memset(packed,0,sizeof packed);
 for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){
  unsigned value=cropped==1?31:cropped==2&&x==0?31:3,bit=x*5,at=y*20+(bit>>3),shift=bit&7;
  packed[at]|=value<<shift;if(shift>3)packed[at+1]|=value>>(8-shift);
 }
 assert(!fseek(romfile,tile*640,SEEK_SET)&&fwrite(packed,1,sizeof packed,romfile)==sizeof packed);fflush(romfile);
 memset(tags,255,sizeof tags);memset(tile_tags,255,TILE_SLOTS*4);memset(text_tags,255,512*4);++bg_epoch;
}
static void stream_collision_scene(void){
 parts[PGM_TILES].size=700000;memset(rows,0,0x800);memset(vreg,0,0x10000);memset(tx,0,0x2000);memset(sprites,0,sizeof sprites);
 for(unsigned at=0;at<0x4000;at+=4){store(bg+at,1);store(bg+at+2,0);}
 stream_write_tile(1,0);stream_write_tile(1025,1);
 for(unsigned y=0;y<64;y++)store(bg+(y*64+13)*4,1025);
 stream_case(1,0,0);assert(tile_tags[1]==1);
 /* A mixed tile cropped to a single transparent pixel must not pin a
  * conflicting slot; preparation restores the visible tile after eviction. */
 stream_write_tile(1025,2);store(vreg+0x3000,1);
 for(unsigned y=0;y<64;y++){store(bg+(y*64+13)*4,1);store(bg+(y*64+14)*4,1025);}
 stream_case(1,0,0);assert(tile_tags[1]==1);
 /* Two different visible tiles in one direct-mapped slot require fallback. */
 store(vreg+0x3000,0);
 for(unsigned y=0;y<64;y++)store(bg+(y*64+13)*4,1025);
 ++bg_epoch;stream_case(0,0,0);
}
static void stream_text_collision_scene(void){
 stream_random_scene(80);memset(tx,0,0x2000);store(vreg+0x5000,0);store(vreg+0x6000,0);
 /* Two opaque font tiles share a direct-mapped cache slot. Alternating
  * visible HUD blocks must keep their own indices after the later decode;
  * the fully transparent tile 0 must produce no text plan allocation. */
 uint8_t font[32];
 for(unsigned t=0;t<2;t++){
  memset(font,t?0x77:0x33,sizeof font);
  assert(!fseek(romfile,(20+t*512)*32,SEEK_SET)&&fwrite(font,1,sizeof font,romfile)==sizeof font);
 }
 fflush(romfile);memset(tags,255,sizeof tags);memset(tile_tags,255,TILE_SLOTS*4);memset(text_tags,255,512*4);++bg_epoch;
 for(unsigned i=0;i<16;i++){
  unsigned at=((2+(i/8)*4)*64+8+(i%8)*4)*4;
  store(tx+at,20+(i&1)*512);store(tx+at+2,(i&3)*0x40);
 }
 stream_case(1,0,0);assert(pgm_stream_text_block_used==16);
 stream_sparse_text_blocks=pgm_stream_text_block_used;
}
static void stream_text_allocation_fallback(void){
 reference_pixels=stream_reference565;draw_reference();pixels=stream_frame;draw();same_pixels(stream_frame,stream_reference565);
 for(unsigned mode=0;mode<3;mode++){
  present_reference(stream_lcd_reference,mode);h1_pgm_present_reset();
  free(pgm_stream_text_blocks);pgm_stream_text_blocks=0;pgm_stream_text_capacity=0;
  stream_text_fail_realloc=1;
  assert(pgm_stream_prepare()&&!pgm_stream_text_plan_ready);
  stream_text_fail_realloc=0;
  memset(stream_first+1,0x5a,STREAM_LCD_WORDS*4);h1_pgm_present(stream_first+1,mode);stream_lcd_check(stream_first);
  ++stream_text_oom_modes;
 }
}
static void stream_sprite_pool_scene(unsigned count){
 memset(sprites,0,sizeof sprites);memset(vreg+0x1000,0,17*4);h1_pgm_sprite_mode=3;
 for(unsigned i=0;i<count;i++){
  uint16_t *s=sprites+i*5;s[0]=i*32;s[1]=0;s[2]=0;
  /* In the three-sprite fixture, A/B/A use boff 0/2048/0. All
   * descriptors have width 16 and height 4, so their hash slots collide. */
  s[3]=(count==3&&i==1)?1024:0;s[4]=(1<<9)|4;
 }
}
static void stream_sprite_pool_verify(unsigned count,const uint8_t a[64],const uint8_t b[64]){
 uint32_t actual[WIDTH+2],expected[WIDTH];
 for(unsigned y=0;y<HEIGHT;y++){
  actual[0]=0x12345678;actual[WIDTH+1]=0x89abcdef;
  for(unsigned x=0;x<WIDTH;x++)actual[x+1]=expected[x]=0x12000000u+y*WIDTH+x;
  if(y<4)for(unsigned i=0;i<count;i++){
   const uint8_t *src=(count==3&&i==1)?b:a;
   for(unsigned x=0;x<16;x++)if(src[y*16+x]!=255)expected[i*32+x]=colors[src[y*16+x]];
  }
  pgm_stream_sprites_row(y,actual+1,1);pgm_stream_sprites_row(y,actual+1,0);
  assert(actual[0]==0x12345678&&actual[WIDTH+1]==0x89abcdef);
  for(unsigned x=0;x<WIDTH;x++)if(actual[x+1]!=expected[x]){
   fprintf(stderr,"sprite pool case=%u x=%u y=%u actual=%08x expected=%08x\n",stream_sprite_pool_cases,x,y,actual[x+1],expected[x]);abort();
  }
  ++stream_sprite_pool_rows;
 }
 ++stream_sprite_pool_cases;
}
static void stream_sprite_pool_regression(void){
 uint8_t a[64],b[64];const unsigned bytes=64;
 decoded_sprite *saved=malloc(SPRITE_SLOTS*sizeof *saved);assert(saved&&sprite_capacity>256);
 assert((((0u>>2)^(17u)^(4u*31))&(SPRITE_SLOTS-1))==(((2048u>>2)^(17u)^(4u*31))&(SPRITE_SLOTS-1)));
 memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);sprite_used=0;
 memcpy(a,sprite_data(0,1,4),sizeof a);memcpy(b,sprite_data(2048,1,4),sizeof b);

 /* A/A are already cached. A near-full pool must not reset merely because
  * the sum of all visible sprite sizes exceeds its remaining 16 bytes. */
 stream_sprite_pool_scene(2);memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);sprite_used=0;sprite_data(0,1,4);
 sprite_used=sprite_capacity-16;
 unsigned before=sprite_used,misses=h1_pgm_perf.sprite_misses,hits=h1_pgm_perf.sprite_hits;
 memcpy(saved,sprite_tags,SPRITE_SLOTS*sizeof *saved);
 assert(pgm_stream_sprites_prepare());assert(sprite_used==before);
 assert(h1_pgm_perf.sprite_misses==misses&&h1_pgm_perf.sprite_hits==hits+2);
 assert(!memcmp(saved,sprite_tags,SPRITE_SLOTS*sizeof *saved));
 assert(pgm_stream_sprite_plans[0].decoded==sprite_pixels&&pgm_stream_sprite_plans[1].decoded==sprite_pixels);
 stream_sprite_pool_verify(2,a,b);

 /* A hits, B misses, then A misses because B evicted its tag. Exactly
  * 128 free bytes fit both decodes without resetting the first pinned A. */
 stream_sprite_pool_scene(3);memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);sprite_used=0;sprite_data(0,1,4);
 sprite_used=sprite_capacity-2*bytes;before=sprite_used;
 misses=h1_pgm_perf.sprite_misses;hits=h1_pgm_perf.sprite_hits;
 assert(pgm_stream_sprites_prepare());assert(sprite_used==sprite_capacity);
 assert(h1_pgm_perf.sprite_misses==misses+2&&h1_pgm_perf.sprite_hits==hits+1);
 assert(pgm_stream_sprite_plans[0].decoded==sprite_pixels);
 assert(pgm_stream_sprite_plans[1].decoded==sprite_pixels+before);
 assert(pgm_stream_sprite_plans[2].decoded==sprite_pixels+before+bytes);
 stream_sprite_pool_verify(3,a,b);

 /* 112 free bytes could fit B, but not the later A miss. Reset BEFORE the
  * first descriptor, then decode A/B/A into three distinct pinned spans.
  * Comparing every descriptor only with the original tag set is unsafe. */
 memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);sprite_used=0;sprite_data(0,1,4);sprite_used=sprite_capacity-112;
 misses=h1_pgm_perf.sprite_misses;hits=h1_pgm_perf.sprite_hits;
 assert(pgm_stream_sprites_prepare());assert(sprite_used==3*bytes);
 assert(h1_pgm_perf.sprite_misses==misses+3&&h1_pgm_perf.sprite_hits==hits);
 for(unsigned i=0;i<3;i++)assert(pgm_stream_sprite_plans[i].decoded==sprite_pixels+i*bytes);
 stream_sprite_pool_verify(3,a,b);

 /* Fail only the reusable virtual-tag allocation. Plans/maps already
  * exist, so no other allocation is needed before this failure point. */
 stream_sprite_pool_scene(2);memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);sprite_used=0;sprite_data(0,1,4);
 sprite_used=sprite_capacity-16;before=sprite_used;
 free(pgm_stream_sprite_virtual_tags);pgm_stream_sprite_virtual_tags=0;
 memcpy(saved,sprite_tags,SPRITE_SLOTS*sizeof *saved);
 misses=h1_pgm_perf.sprite_misses;hits=h1_pgm_perf.sprite_hits;
 stream_sprite_fail_malloc_size=SPRITE_SLOTS*sizeof *pgm_stream_sprite_virtual_tags;
 assert(!pgm_stream_sprites_prepare());assert(!pgm_stream_sprite_count&&!pgm_stream_sprite_behind);
 assert(sprite_used==before&&!memcmp(saved,sprite_tags,SPRITE_SLOTS*sizeof *saved));
 assert(h1_pgm_perf.sprite_misses==misses&&h1_pgm_perf.sprite_hits==hits);
 stream_sprite_fail_malloc_size=0;
 assert(pgm_stream_sprites_prepare());assert(sprite_used==before);
 assert(!memcmp(saved,sprite_tags,SPRITE_SLOTS*sizeof *saved));
 stream_sprite_pool_verify(2,a,b);free(saved);
}
int main(void){
 assert(pgm_draw_existing_main()==0);
 stream_reference565=malloc(WIDTH*HEIGHT*2);stream_frame=malloc(WIDTH*HEIGHT*4);
 stream_lcd_reference=malloc(STREAM_LCD_WORDS*4);
 stream_first=malloc((STREAM_LCD_WORDS+2)*4);stream_second=malloc((STREAM_LCD_WORDS+2)*4);
 assert(stream_reference565&&stream_frame&&stream_lcd_reference&&stream_first&&stream_second);
 stream_first[0]=stream_second[0]=0x12345678;stream_first[STREAM_LCD_WORDS+1]=stream_second[STREAM_LCD_WORDS+1]=0x89abcdef;
 h1_pgm_layers_mode=1;h1_pgm_clear_mode=0;h1_pgm_present_mode=1;h1_pgm_margin_mode=1;h1_pgm_scale_mode=0;h1_pgm_stream_mode=1;
 for(unsigned frame=0;frame<64;frame++){stream_random_scene(frame);stream_case(1,0,0);}
 stream_random_scene(68);stream_case(1,1,0);stream_case(1,0,1);
 /* Three individually valid giant sprites exceed the shared decoded pool;
  * the immediate renderer remains valid while a pinned stream must decline. */
 stream_random_scene(72);memset(sprites,0,sizeof sprites);memset(vreg+0x1000,0,17*4);
 for(unsigned i=0;i<3;i++){uint16_t *s=sprites+i*5;s[0]=i*32;s[1]=i*16;s[2]=i?0:0x80;s[3]=i*1024;s[4]=(63<<9)|511;}
 stream_case(0,0,0);
 stream_collision_scene();
 stream_random_scene(76);h1_pgm_stream_mode=0;stream_case(0,0,0);h1_pgm_stream_mode=1;
 stream_text_collision_scene();
 /* Mode changes themselves must invalidate margins on a prepared stream,
  * without an explicit reset between the different display modes. */
 static const unsigned sequence[]={0,2,1,0,1,2,2,0};
 for(unsigned margin=0;margin<2;margin++){
  h1_pgm_margin_mode=margin;h1_pgm_present_reset();assert(pgm_stream_prepare());
  for(unsigned i=0;i<sizeof sequence/sizeof *sequence;i++){
   present_reference(stream_lcd_reference,sequence[i]);h1_pgm_present(stream_first+1,sequence[i]);
   stream_lcd_check(stream_first);++stream_mode_switches;
  }
 }
 stream_text_allocation_fallback();
 stream_sprite_pool_regression();
 pgm_stream_close();assert(!pgm_stream_ready&&!pgm_stream_layers&&!pgm_stream_sprite_plans&&!pgm_stream_sprite_virtual_tags);pgm_stream_close();
 printf("PGM_STREAM_RESULT {\"random_scenes\":64,\"scenes\":%u,\"lcd_comparisons\":%u,\"fallback_modes\":%u,\"wrap_modes\":%u,\"snapshot_modes\":%u,\"mode_switch_checks\":%u,\"sparse_text_blocks\":%u,\"sparse_text_copied_indices_bytes\":%u,\"text_allocation_fallback_modes\":%u,\"sprite_pool_cases\":%u,\"sprite_pool_row_comparisons\":%u,\"display_modes\":3}\n",stream_scenes,stream_lcd_passes,stream_fallback_modes,stream_wrap_modes,stream_snapshot_modes,stream_mode_switches,stream_sparse_text_blocks,stream_sparse_text_blocks*64,stream_text_oom_modes,stream_sprite_pool_cases,stream_sprite_pool_rows);
 return 0;
}
