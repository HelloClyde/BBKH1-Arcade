/* Native PGM sprite-row assembly tests. GPL-2.0-or-later.
 * These fixtures use generated inputs and call scalar/MXU row kernels.
 * Run on the H1 after the ordinary startup checks, before loading a game. */
#include <stdint.h>

extern unsigned h1_pgm_sprite_row(uint32_t *,const uint8_t *,const uint32_t *,unsigned);
extern unsigned h1_pgm_sprite_map(uint32_t *,const uint8_t *,const uint32_t *,const uint16_t *,unsigned);
static int copy_selftest(void);
extern unsigned h1_pgm_sprite_rev(uint32_t *,const uint8_t *,const uint32_t *,unsigned);
extern unsigned h1_pgm_sprite_mxu_row(uint32_t *,const uint8_t *,const uint32_t *,unsigned);
extern unsigned h1_pgm_sprite_mxu_map(uint32_t *,const uint8_t *,const uint32_t *,const uint16_t *,unsigned);
extern unsigned h1_pgm_sprite_mxu_rev(uint32_t *,const uint8_t *,const uint32_t *,unsigned);

volatile struct {
 unsigned magic,version,tests,failures,kind,n,index,expected,actual;
} h1_pgm_sprite_tests={.magic=0x31535047u,.version=1}; /* GPS1 */

static unsigned random_value(unsigned *seed){
 *seed=*seed*1664525u+1013904223u;return *seed;
}

static int failed(unsigned kind,unsigned n,unsigned index,unsigned want,unsigned got){
 ++h1_pgm_sprite_tests.failures;
 h1_pgm_sprite_tests.kind=kind;h1_pgm_sprite_tests.n=n;
 h1_pgm_sprite_tests.index=index;h1_pgm_sprite_tests.expected=want;
 h1_pgm_sprite_tests.actual=got;return 0;
}

int h1_pgm_sprite_selftest(void){
 /* Local fixtures avoid consuming the limited BDA loading window. The app
  * reserves 512 KiB of stack. Compare the whole destination, including all
  * guards; n=448 still has at least four before and nine after the row. */
 uint32_t actual[464],expected[464],palette[32],saved_palette[32];
 uint8_t source[1024],saved_source[1024];
 uint16_t mapping[464],saved_mapping[464];
#if H1_MXU
 unsigned total=3072;
#else
 unsigned total=1536;
#endif
 h1_pgm_sprite_tests.tests=h1_pgm_sprite_tests.failures=0;
 h1_pgm_sprite_tests.kind=h1_pgm_sprite_tests.n=h1_pgm_sprite_tests.index=0;
 h1_pgm_sprite_tests.expected=h1_pgm_sprite_tests.actual=0;
 /* Each kernel has 512 cases. Lengths 0..448 appear at least once in each;
  * the remaining cases repeat short/tail/maximal lengths with new data. */
 for(unsigned test=0;test<total;test++){
  unsigned group=test/512,mxu=group>=3,kind_base=mxu?16:0;
  unsigned mapped=group%3==1,reversed=group%3==2,fixture=test&511u;
  unsigned n=fixture<=448?fixture:(unsigned[]){0,1,2,3,4,7,8,15,16,31,32,447,448}[(fixture-449)%13];
  unsigned src_start=4+(fixture&3)+(reversed?1007:0),dst_start=4+((fixture>>2)&3);
  unsigned map_start=4+((fixture>>4)&3),seed=0x47404740u^(test*0x9e3779b9u);
  /* Sources include full opaque, full transparent, alternating, long runs,
   * and random transparency. Every nontransparent index is in 0..31. */
  for(unsigned i=0;i<1024;i++){
   unsigned r=random_value(&seed),mode=fixture%5;
   unsigned transparent=mode==0?0:mode==1?1:mode==2?(i&1):mode==3?((i>>4)&1):((r>>24)<96);
   source[i]=saved_source[i]=(uint8_t)(transparent?255:r&31);
  }
  /* Change palette for every invocation, including repeated map/source
   * shapes, so a stale preconverted color or cached palette is detected. */
  for(unsigned i=0;i<32;i++)palette[i]=saved_palette[i]=random_value(&seed)^(i*0x01010101u);
  for(unsigned i=0;i<464;i++){
   actual[i]=expected[i]=random_value(&seed)^0x5aa55aa5u;
   unsigned local=(i+464-map_start)%464,mode=(fixture>>1)%5;
   unsigned index=mode==0?local:mode==1?1007-local:mode==2?317+(local>>2)%32:
                  mode==3?((local%448)*1008u/448u):local<2?local*1007:random_value(&seed)%1008;
   mapping[i]=saved_mapping[i]=(uint16_t)index;
  }
  unsigned count=0;
  for(unsigned i=0;i<n;i++){
   unsigned at=mapped?mapping[map_start+i]:i;
   unsigned color=source[reversed?src_start-i:src_start+at];
   if(color!=255){expected[dst_start+i]=palette[color];++count;}
  }
  unsigned got;
#if defined(__mips__) && H1_MXU
  extern unsigned h1_mxu_control(void);
  unsigned control=0,before=0,after;
  if(mxu){control=h1_mxu_control();__asm__ volatile("mfc0 %0,$12":"=r"(before));}
#endif
#if H1_MXU
  if(mxu)got=reversed?h1_pgm_sprite_mxu_rev(actual+dst_start,source+src_start,palette,n):
                     mapped?h1_pgm_sprite_mxu_map(actual+dst_start,source+src_start,palette,mapping+map_start,n):
                            h1_pgm_sprite_mxu_row(actual+dst_start,source+src_start,palette,n);
  else
#endif
  got=reversed?h1_pgm_sprite_rev(actual+dst_start,source+src_start,palette,n):
                    mapped?h1_pgm_sprite_map(actual+dst_start,source+src_start,palette,mapping+map_start,n):
                           h1_pgm_sprite_row(actual+dst_start,source+src_start,palette,n);
  ++h1_pgm_sprite_tests.tests;
#if defined(__mips__) && H1_MXU
  if(mxu){
   __asm__ volatile("mfc0 %0,$12":"=r"(after));
   unsigned current=h1_mxu_control();
   if(current!=control)return failed(32,n,fixture,control,current);
   if(after!=before)return failed(33,n,fixture,before,after);
  }
#endif
  /* Failure kinds: 1/2 output (guards included), 3/4 opaque count,
   * 5/6 source modification, 7 map modification, 8 palette modification.
   * Reverse kernel uses 9 output, 10 count, 11 source, 12 map, 13 palette.
   * MXU failures add 16 to the corresponding scalar kind; 32/33 report
   * MXU control / CP0 Status changes. */
  if(got!=count)return failed(kind_base+(reversed?10:mapped?4:3),n,0,count,got);
  for(unsigned i=0;i<464;i++)if(actual[i]!=expected[i])return failed(kind_base+(reversed?9:mapped?2:1),n,i,expected[i],actual[i]);
  for(unsigned i=0;i<1024;i++)if(source[i]!=saved_source[i])return failed(kind_base+(reversed?11:mapped?6:5),n,i,saved_source[i],source[i]);
  for(unsigned i=0;i<464;i++)if(mapping[i]!=saved_mapping[i])return failed(kind_base+(reversed?12:7),n,i,saved_mapping[i],mapping[i]);
  for(unsigned i=0;i<32;i++)if(palette[i]!=saved_palette[i])return failed(kind_base+(reversed?13:8),n,i,saved_palette[i],palette[i]);
 }
 return copy_selftest();
}

volatile struct {unsigned magic,version,tests,failures,index,expected,actual;} h1_pgm_copy_tests={0x31435047,1};
static int copy_selftest(void){
#if defined(__mips__) && H1_MXU
 extern void h1_mxu_copy32(uint32_t *,const uint32_t *,unsigned);
 extern unsigned h1_mxu_control(void);
 uint32_t src[480],dst[488];unsigned seed=4740;
 for(unsigned test=0;test<256;test++){
  unsigned n=8+8*(test%60),start=1+(test&3),before,after,control=h1_mxu_control();
  for(unsigned i=0;i<480;i++)src[i]=random_value(&seed);
  for(unsigned i=0;i<488;i++)dst[i]=0x5aa55aa5;
  __asm__ volatile("mfc0 %0,$12":"=r"(before));
  h1_mxu_copy32(dst+start,src,n);
  __asm__ volatile("mfc0 %0,$12":"=r"(after));
  ++h1_pgm_copy_tests.tests;
  if(before!=after||control!=h1_mxu_control()){++h1_pgm_copy_tests.failures;return 0;}
  for(unsigned i=0;i<488;i++){
   unsigned expected=i>=start&&i<start+n?src[i-start]:0x5aa55aa5;
   if(dst[i]!=expected){++h1_pgm_copy_tests.failures;h1_pgm_copy_tests.index=i;h1_pgm_copy_tests.expected=expected;h1_pgm_copy_tests.actual=dst[i];return 0;}
  }
 }
#endif
 return 1;
}
