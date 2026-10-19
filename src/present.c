/* Exact RGB565 scaling with optional unchanged-row elision. GPL-2.0-or-later. */
#include "present.h"
#include <string.h>
#ifndef H1_DIRTY_PRESENT
#define H1_DIRTY_PRESENT 0
#endif
#ifndef H1_MXU
#define H1_MXU 0
#endif
#ifndef H1_SPEED_CORE
#define H1_SPEED_CORE 0
#endif
#ifndef H1_SCALE_OUTPUT
#define H1_SCALE_OUTPUT 0
#endif
volatile unsigned h1_present_dirty=H1_DIRTY_PRESENT;
volatile unsigned h1_present_mxu=H1_MXU;
volatile unsigned h1_present_direct=H1_SPEED_CORE;
volatile unsigned h1_present_direct_pixels;
volatile unsigned h1_present_cached=H1_SPEED_CORE;
volatile unsigned h1_present_cached_pixels;
volatile unsigned h1_present_scale=H1_SCALE_OUTPUT;
volatile struct {unsigned magic,version,calls,pixels,tests,failures;} h1_scale_stats={0x314c4353,1};
volatile h1_mxu_stats_t h1_mxu_stats={0x3155584d,1};
volatile h1_present_bench_t h1_present_bench={0x31504248,H1_SCALE_OUTPUT?4:H1_SPEED_CORE?3:1};
volatile struct {unsigned magic,version,frames,written,unchanged,invalidations,pixels_written,pixels_unchanged;} h1_present_stats={0x31535250,2};
static uint32_t colors[65536],row[480];
static uint16_t columns[480],edges[21],previous[224][320];
static uint8_t valid[224];
static unsigned ready,map_width,last_mode,last_dirty,last_mxu,last_direct,last_cached,last_scaled;
static uint32_t *last_target;
#if H1_MXU
extern void h1_mxu_rgb565(uint32_t*,const uint16_t*,unsigned);
extern unsigned h1_mxu_control(void);
static uint32_t converted[320];
#if H1_SCALE_OUTPUT
extern void h1_mxu_scale362(uint32_t*,const uint16_t*,unsigned);
#endif
#endif
void h1_present_reset(void){last_target=0;memset(valid,0,sizeof valid);}
/* Same D-cache hit writeback/invalidate operation and conservative 16-byte
 * stride already used by H1Debug on this exact device. LCD DMA reads RAM,
 * so cached writes are explicitly published before returning each range. */
static void lcd_writeback(void *start,unsigned bytes){
#if defined(__mips__)
 uintptr_t a=(uintptr_t)start&~15u,end=((uintptr_t)start+bytes+15u)&~15u;
 __asm__ volatile("sync":::"memory");
 for(;a<end;a+=16)__asm__ volatile("cache 0x15,0(%0)"::"r"(a):"memory");
 __asm__ volatile("sync":::"memory");
#else
 (void)start;(void)bytes;
#endif
}
static int same_block(const uint16_t *a,const uint16_t *b){
 /* The renderer's visible rows and this cache are word aligned. */
 const uint32_t *x=(const uint32_t*)a,*y=(const uint32_t*)b;
 for(unsigned i=0;i<8;i+=4)
  if((x[i]^y[i])|(x[i+1]^y[i+1])|(x[i+2]^y[i+2])|(x[i+3]^y[i+3]))return 0;
 return 1;
}
void h1_present_rgb565(uint32_t *out,const uint16_t *source,unsigned pitch,unsigned mode,unsigned clear){
 uint32_t *native_target=out;uintptr_t address=(uintptr_t)out;
 unsigned cached=h1_present_cached&&address>=0xa0000000u&&address<0xa4000000u&&
  !(address&255u)&&480u*272u*4u<=0xa4000000u-address;
 if(cached)out=(uint32_t*)((address&0x1fffffffu)|0x80000000u);
 unsigned dirty=h1_present_dirty!=0;
 unsigned mxu=H1_MXU&&h1_present_mxu!=0;
 unsigned w=mode==2?480:mode==1?320:362,h=mode==1?224:272;
 /* Native size has one output row per source row. The bounded MXU leaf
  * writes converted pixels to the same aligned LCD RAM as the scalar copy,
  * avoiding intermediate row traffic. Scaled modes retain row reuse. */
 unsigned direct=h1_present_direct&&mxu&&w==320&&
                 (((uintptr_t)source|(uintptr_t)out|pitch)&3u)==0;
 unsigned scaled=H1_SCALE_OUTPUT&&h1_present_scale&&mxu&&w==362&&
                 (((uintptr_t)source|(uintptr_t)out|pitch)&3u)==0;
 unsigned ox=(480-w)/2,oy=(272-h)/2;
 if(!ready){for(unsigned p=0;p<65536;p++)colors[p]=((p&0xf800)<<8)|((p&0x7e0)<<5)|((p&31)<<3);ready=1;}
 if(clear||native_target!=last_target||mode!=last_mode||dirty!=last_dirty||mxu!=last_mxu||direct!=last_direct||cached!=last_cached||scaled!=last_scaled){
  memset(out,0,480*272*4);if(cached)lcd_writeback(out,480*272*4);
  memset(valid,0,sizeof valid);++h1_present_stats.invalidations;
 }
 last_target=native_target;last_mode=mode;last_dirty=dirty;last_mxu=mxu;last_direct=direct;last_cached=cached;
 last_scaled=scaled;
 if(map_width!=w){
  for(unsigned x=0;x<w;x++)columns[x]=x*320/w;
  for(unsigned b=0;b<=20;b++)edges[b]=(b*16*w+319)/320;
  map_width=w;
 }
 unsigned last=~0u,changed=0;
 for(unsigned y=0;y<h;y++){
  unsigned sy=y*224/h;
  if(sy!=last){
   const uint16_t *src=(const uint16_t*)((const uint8_t*)source+sy*pitch);
   changed=0xfffff;
   if(dirty&&valid[sy]){
    changed=0;
    for(unsigned b=0;b<20;b++)if(!same_block(src+b*16,previous[sy]+b*16))changed|=1u<<b;
   }
   if(changed){
#if H1_SCALE_OUTPUT && H1_MXU
    if(scaled){
     h1_mxu_scale362(out+(y+oy)*480+ox,src,changed);
     ++h1_scale_stats.calls;
    }else
#endif
    {
#if H1_MXU
    unsigned vector=mxu&&((uintptr_t)src&3u)==0;
    if(vector&&w!=320){
     unsigned first=0,last_block=20;
     while(!(changed&(1u<<first)))++first;
     while(!(changed&(1u<<(last_block-1))))--last_block;
     unsigned n=(last_block-first)*16;
     h1_mxu_rgb565(converted+first*16,src+first*16,n);
     ++h1_mxu_stats.calls;h1_mxu_stats.pixels+=n;
    }
#endif
    for(unsigned b=0;b<20;){
     if(!(changed&(1u<<b))){++b;continue;}
     unsigned x=edges[b];while(b<20&&(changed&(1u<<b)))++b;
     unsigned end=edges[b];
#if H1_MXU
     if(vector){
      if(w==320){
       h1_mxu_rgb565(direct?out+(y+oy)*480+ox+x:row+x,src+x,end-x);
       if(direct)h1_present_direct_pixels+=end-x;
       ++h1_mxu_stats.calls;h1_mxu_stats.pixels+=end-x;
      }else{
       for(;x+3<end;x+=4){row[x]=converted[columns[x]];row[x+1]=converted[columns[x+1]];
        row[x+2]=converted[columns[x+2]];row[x+3]=converted[columns[x+3]];}
       for(;x<end;x++)row[x]=converted[columns[x]];
      }
      continue;
     }
#endif
     for(;x+3<end;x+=4){row[x]=colors[src[columns[x]]];row[x+1]=colors[src[columns[x+1]]];
      row[x+2]=colors[src[columns[x+2]]];row[x+3]=colors[src[columns[x+3]]];}
     for(;x<end;x++)row[x]=colors[src[columns[x]]];
    }
    }
    if(dirty){memcpy(previous[sy],src,640);valid[sy]=1;}
   }
  }
  unsigned written=0;
  if(changed){
   for(unsigned b=0;b<20;){
    if(!(changed&(1u<<b))){++b;continue;}
    unsigned x=edges[b];while(b<20&&(changed&(1u<<b)))++b;
    unsigned count=edges[b]-x;
    if(scaled){
     /* Duplicate vertical rows reuse the preceding LCD row. Unique rows
      * were already converted/scaled straight into this destination. */
     if(sy==last)memcpy(out+(y+oy)*480+ox+x,out+(y+oy-1)*480+ox+x,count*4);
     h1_scale_stats.pixels+=count;
    }else if(!direct)memcpy(out+(y+oy)*480+ox+x,row+x,count*4);
    if(cached){lcd_writeback(out+(y+oy)*480+ox+x,count*4);h1_present_cached_pixels+=count;}
    written+=count;
   }
   ++h1_present_stats.written;
  }else ++h1_present_stats.unchanged;
  h1_present_stats.pixels_written+=written;h1_present_stats.pixels_unchanged+=w-written;
  last=sy;
 }
 ++h1_present_stats.frames;
}

#if H1_MXU && H1_CPU_SELFTEST
int h1_mxu_selftest(void){
 static uint16_t input[320];static uint32_t actual[322];
 unsigned seed=4740;
 for(unsigned test=0;test<512;test++){
  unsigned n=test<256?256:4+4*(test%80);
  for(unsigned i=0;i<n;i++){
   seed=seed*1664525u+1013904223u;
   input[i]=test<256?(uint16_t)(test*256+i):(uint16_t)seed;
  }
  actual[0]=actual[n+1]=0x1234abcd;
  unsigned control=h1_mxu_control(),before,after;
  __asm__ volatile("mfc0 %0,$12":"=r"(before));
  h1_mxu_rgb565(actual+1,input,n);
  __asm__ volatile("mfc0 %0,$12":"=r"(after));
  unsigned failed=before!=after||control!=h1_mxu_control()||actual[0]!=0x1234abcd||actual[n+1]!=0x1234abcd;
  for(unsigned i=0;i<n;i++){
   unsigned p=input[i];
   failed|=actual[i+1]!=(((p>>11)*8<<16)|(((p>>5)&63)*4<<8)|((p&31)*8));
  }
  ++h1_mxu_stats.tests;if(failed){++h1_mxu_stats.failures;return 0;}
 }
 return 1;
}
#if H1_SCALE_OUTPUT
int h1_scale_selftest(void){
 static uint16_t input[320];static uint32_t actual[364];
 unsigned seed=362;
 for(unsigned test=0;test<512;test++){
  unsigned mask=test<256?0xfffff:test<276?1u<<(test-256):test==276?0:test==277?0x55555:test==278?0xaaaaa:(seed>>4)&0xfffff;
  for(unsigned i=0;i<320;i++){
   seed=seed*1664525u+1013904223u;
   input[i]=test<256?(uint16_t)(test*256+i):(uint16_t)seed;
  }
  for(unsigned i=0;i<364;i++)actual[i]=0x1234abcd;
  unsigned control=h1_mxu_control(),before,after;
  __asm__ volatile("mfc0 %0,$12":"=r"(before));
  h1_mxu_scale362(actual+1,input,mask);
  __asm__ volatile("mfc0 %0,$12":"=r"(after));
  unsigned failed=before!=after||control!=h1_mxu_control()||actual[0]!=0x1234abcd||actual[363]!=0x1234abcd;
  for(unsigned x=0;x<362;x++){
   unsigned sx=x*320/362,p=input[sx];
   unsigned expected=(mask&(1u<<(sx/16)))?(((p>>11)*8<<16)|(((p>>5)&63)*4<<8)|((p&31)*8)):0x1234abcd;
   failed|=actual[x+1]!=expected;
  }
  ++h1_scale_stats.tests;if(failed){++h1_scale_stats.failures;return 0;}
 }
 return 1;
}
#endif
#endif
