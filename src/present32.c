/* RGB32 input: exact same nearest-neighbour sampling, no per-pixel conversion. */
#include "present.h"
#include <string.h>
static uint32_t previous32[224][320],row32[480];
static uint16_t columns32[480],edges32[21];
static uint8_t valid32[224];
static uint32_t *last_target32;
static unsigned last_mode32,last_dirty32,map_width32;
static int same32(const uint32_t *a,const uint32_t *b){
 for(unsigned i=0;i<16;i+=4)if((a[i]^b[i])|(a[i+1]^b[i+1])|(a[i+2]^b[i+2])|(a[i+3]^b[i+3]))return 0;
 return 1;
}
void h1_present32_reset(void){last_target32=0;memset(valid32,0,sizeof valid32);}
void h1_present_rgb32(uint32_t *out,const uint32_t *source,unsigned pitch,unsigned mode,unsigned clear){
 unsigned dirty=h1_present_dirty!=0,w=mode==2?480:mode==1?320:362,h=mode==1?224:272;
 unsigned ox=(480-w)/2,oy=(272-h)/2;
 if(clear||out!=last_target32||mode!=last_mode32||dirty!=last_dirty32){memset(out,0,480*272*4);memset(valid32,0,sizeof valid32);}
 last_target32=out;last_mode32=mode;last_dirty32=dirty;
 if(map_width32!=w){for(unsigned x=0;x<w;x++)columns32[x]=x*320/w;
  for(unsigned b=0;b<=20;b++)edges32[b]=(b*16*w+319)/320;map_width32=w;}
 unsigned last=~0u,changed=0;const uint32_t *ready_row=row32;
 for(unsigned y=0;y<h;y++){
  unsigned sy=y*224/h;
  if(sy!=last){
   const uint32_t *src=(const uint32_t*)((const uint8_t*)source+sy*pitch);changed=0xfffff;
   ready_row=w==320?src:row32;
   if(dirty&&valid32[sy]){changed=0;for(unsigned b=0;b<20;b++)if(!same32(src+b*16,previous32[sy]+b*16))changed|=1u<<b;}
   if(changed){
    for(unsigned b=0;b<20;){
     if(!(changed&(1u<<b))){++b;continue;}
     unsigned x=edges32[b];while(b<20&&(changed&(1u<<b)))++b;unsigned end=edges32[b];
     if(w==320)continue;
     for(;x+3<end;x+=4){row32[x]=src[columns32[x]];row32[x+1]=src[columns32[x+1]];row32[x+2]=src[columns32[x+2]];row32[x+3]=src[columns32[x+3]];}
     for(;x<end;x++)row32[x]=src[columns32[x]];
    }
    if(dirty){memcpy(previous32[sy],src,1280);valid32[sy]=1;}
   }
  }
  if(changed)for(unsigned b=0;b<20;){if(!(changed&(1u<<b))){++b;continue;}
   unsigned x=edges32[b];while(b<20&&(changed&(1u<<b)))++b;
   memcpy(out+(y+oy)*480+ox+x,ready_row+x,(edges32[b]-x)*4);}
  last=sy;
 }
}
