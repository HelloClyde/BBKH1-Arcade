/* Generated-input checks for the native opaque background MXU span.
 * GPL-2.0-or-later. Run on physical H1 before entering the game loop.
 * Host graphics checks substitute the C fallback; no CPU model is used. */
#include <stdint.h>
extern void h1_pgm_bg_mxu_span(uint32_t *,const uint8_t *,const uint32_t *,unsigned,unsigned,unsigned);
volatile struct {
 unsigned magic,version,tests,failures,kind,width,rows,flags,index,expected,actual;
} h1_pgm_bg_tests={0x31424750,1};
static unsigned bg_random(unsigned *seed){*seed=*seed*1664525u+1013904223u;return *seed;}
static int bg_failed(unsigned kind,unsigned width,unsigned rows,unsigned flags,unsigned index,unsigned expected,unsigned actual){
 ++h1_pgm_bg_tests.failures;h1_pgm_bg_tests.kind=kind;h1_pgm_bg_tests.width=width;
 h1_pgm_bg_tests.rows=rows;h1_pgm_bg_tests.flags=flags;h1_pgm_bg_tests.index=index;
 h1_pgm_bg_tests.expected=expected;h1_pgm_bg_tests.actual=actual;return 0;
}
#if defined(__mips__) && H1_MXU
extern unsigned h1_mxu_control(void);
static inline void bg_control(unsigned control){
 register unsigned value __asm__("$v0")=control;
 __asm__ volatile(".word 0x7002042f\n\tnop\n\tnop\n\tnop"::"r"(value):"memory");
}
static unsigned bg_read_xr(unsigned xr){
 register unsigned value __asm__("$v0");
 switch(xr){
 case 0:__asm__ volatile(".word 0x7002006e":"=r"(value));break;
 case 1:__asm__ volatile(".word 0x700200ae":"=r"(value));break;
 case 2:__asm__ volatile(".word 0x700200ee":"=r"(value));break;
 default:__asm__ volatile(".word 0x7002012e":"=r"(value));break;
 }
 return value;
}
static void bg_write_xr(unsigned xr,unsigned input){
 register unsigned value __asm__("$v0")=input;
 switch(xr){
 case 0:__asm__ volatile(".word 0x7002006f"::"r"(value));break;
 case 1:__asm__ volatile(".word 0x700200af"::"r"(value));break;
 case 2:__asm__ volatile(".word 0x700200ef"::"r"(value));break;
 default:__asm__ volatile(".word 0x7002012f"::"r"(value));break;
 }
}
#endif
int h1_pgm_bg_selftest(void){
 /* About 117 KiB of bounded stack fixtures fit the app's 512 KiB stack.
  * Compare every destination word, including untouched rows and guards. */
 uint32_t actual[448*32+16],expected[448*32+16],pal[32],saved_pal[32];
 uint8_t source[32*32+32],saved_source[32*32+32];
 h1_pgm_bg_tests.tests=h1_pgm_bg_tests.failures=0;
 h1_pgm_bg_tests.kind=h1_pgm_bg_tests.width=h1_pgm_bg_tests.rows=h1_pgm_bg_tests.flags=0;
 h1_pgm_bg_tests.index=h1_pgm_bg_tests.expected=h1_pgm_bg_tests.actual=0;
 for(unsigned test=0;test<512;test++){
  unsigned fixture=test>>2,flags=test&3,width=fixture%33,rows=(fixture*13+(fixture/33)*7)%33;
  if(fixture>=120){
   width=(unsigned[]){0,1,2,3,4,31,32,32}[fixture-120];
   rows=(unsigned[]){32,31,4,3,2,1,0,32}[fixture-120];
  }
  unsigned seed=0x4740b600u^(test*0x9e3779b9u),start=4+((test>>3)&3);
  unsigned rx=(test>>2)%((width?32-width:31)+1),ry=(test>>5)%((rows?32-rows:31)+1);
  unsigned sx=(flags&1)?31-rx:rx,sy=(flags&2)?31-ry:ry;
  for(unsigned i=0;i<sizeof source;i++)source[i]=saved_source[i]=(bg_random(&seed)>>24)&31;
  for(unsigned i=0;i<32;i++)pal[i]=saved_pal[i]=bg_random(&seed)^(i*0x01010101u);
  for(unsigned i=0;i<448*32+16;i++)actual[i]=expected[i]=bg_random(&seed)^0xdeadbeefu;
  for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<width;x++){
   unsigned yy=(flags&2)?sy-y:sy+y,xx=(flags&1)?sx-x:sx+x;
   expected[start+y*448+x]=pal[source[16+yy*32+xx]];
  }
#if defined(__mips__) && H1_MXU
  unsigned original_control=h1_mxu_control(),saved_xr[4],seeded_xr[4],after_xr[4];
  bg_control(original_control|1);
  for(unsigned i=0;i<4;i++){saved_xr[i]=bg_read_xr(i);seeded_xr[i]=bg_random(&seed);bg_write_xr(i,seeded_xr[i]);}
  unsigned call_control=(test&4)?original_control|1:original_control&~1u,before_status,after_status;
  bg_control(call_control);__asm__ volatile("mfc0 %0,$12":"=r"(before_status)::"memory");
#endif
  h1_pgm_bg_mxu_span(actual+start,source+16+sy*32+sx,pal,width,rows,flags);
#if defined(__mips__) && H1_MXU
  __asm__ volatile("mfc0 %0,$12":"=r"(after_status)::"memory");
  unsigned after_control=h1_mxu_control();bg_control(original_control|1);
  for(unsigned i=0;i<4;i++){after_xr[i]=bg_read_xr(i);bg_write_xr(i,saved_xr[i]);}
  bg_control(original_control);
#endif
  ++h1_pgm_bg_tests.tests;
#if defined(__mips__) && H1_MXU
  if(call_control!=after_control)return bg_failed(4,width,rows,flags,0,call_control,after_control);
  if(before_status!=after_status)return bg_failed(5,width,rows,flags,0,before_status,after_status);
  for(unsigned i=0;i<4;i++)if(seeded_xr[i]!=after_xr[i])return bg_failed(6,width,rows,flags,i,seeded_xr[i],after_xr[i]);
#endif
  for(unsigned i=0;i<448*32+16;i++)if(actual[i]!=expected[i])return bg_failed(1,width,rows,flags,i,expected[i],actual[i]);
  for(unsigned i=0;i<sizeof source;i++)if(source[i]!=saved_source[i])return bg_failed(2,width,rows,flags,i,saved_source[i],source[i]);
  for(unsigned i=0;i<32;i++)if(pal[i]!=saved_pal[i])return bg_failed(3,width,rows,flags,i,saved_pal[i],pal[i]);
 }
 return 1;
}
