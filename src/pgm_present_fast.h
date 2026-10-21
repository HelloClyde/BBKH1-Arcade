/* PGM RGB32 presenter. Included after pixels, phase() and the MXU prototypes.
 * The application owns every LCD pixel. Margins are rebuilt after reset,
 * target or mode changes, then remain black during steady game rendering.
 * Scaling gathers straight into the cached LCD alias. Only the 48 repeated
 * vertical rows need a second copy; no temporary 480-pixel scanline is used.
 */
#ifndef H1_PGM_PRESENT_FAST_H
#define H1_PGM_PRESENT_FAST_H

/* Debug A/B switch: zero keeps the generic four-pixel indexed C gather. */
/* The L1 scanline source lets the fixed-offset kernel win the native
 * complete-pipeline comparison. Retain generic gather for explicit A/B. */
volatile unsigned h1_pgm_scale_mode=1;
/* Zero retains a complete margin rewrite and full-LCD publication per call. */
volatile unsigned h1_pgm_margin_mode=1;
static uint32_t *pgm_present_target;
static unsigned pgm_present_last_mode,pgm_present_valid;
void h1_pgm_present_reset(void){pgm_present_valid=0;pgm_present_target=0;pgm_stream_reset();}
#if defined(__mips__)
extern void h1_pgm_scale36232(uint32_t *,const uint32_t *);
extern void h1_pgm_scale48032(uint32_t *,const uint32_t *);
#endif

static void pgm_present_copy(uint32_t *dst,const uint32_t *src,unsigned n){
#if H1_MXU
 unsigned bulk=n&~7u;
 if(bulk){h1_mxu_copy32(dst,src,bulk);h1_pgm_perf.mxu_pixels+=bulk;}
 for(unsigned x=bulk;x<n;x++)dst[x]=src[x];
#else
 memcpy(dst,src,n*4);
#endif
}

static void present_fast(uint32_t *native,unsigned mode){
 phase(6);
 uint32_t *lcd=native;uintptr_t address=(uintptr_t)native;
 unsigned cached=address>=0xa0000000u&&address<0xa4000000u&&!(address&255u)&&480u*272u*4u<=0xa4000000u-address;
 if(cached)lcd=(uint32_t*)((address&0x1fffffffu)|0x80000000u);
 unsigned w=mode==0?362:mode==1?448:480,h=mode==1?224:272;
 unsigned left=(480-w)/2,top=(272-h)/2;
 unsigned full=!h1_pgm_margin_mode||!pgm_present_valid||native!=pgm_present_target||mode!=pgm_present_last_mode;
 static uint16_t columns[480];static unsigned last_width;
 if(w!=448&&last_width!=w){
  for(unsigned x=0;x<w;x++)columns[x]=x*448/w;
  last_width=w;
 }
 /* The black gap from one active line to the next is contiguous. Combining
  * it halves the margin memset calls without omitting any pixels. */
 if(full&&(top||left))memset(lcd,0,(top*480+left)*4);
 uint32_t *dst=lcd+top*480+left;
 unsigned sy=0,fraction=0,last=~0u;
 for(unsigned y=0;y<h;y++,dst+=480){
  if(sy==last){
   pgm_present_copy(dst,dst-480,w);
  }else{
   const uint32_t *src=pixels+sy*448;
   if(w==448){
    pgm_present_copy(dst,src,448);
   }
#if defined(__mips__)
   else if(h1_pgm_scale_mode){
    if(w==362)h1_pgm_scale36232(dst,src);else h1_pgm_scale48032(dst,src);
   }
#endif
   else{
    unsigned x=0;
    /* Four independent indexed loads expose the MIPS load delay to the
     * compiler, followed by four sequential word stores to the LCD. */
    for(;x+4<=w;x+=4){
     uint32_t a=src[columns[x]],b=src[columns[x+1]];
     uint32_t c=src[columns[x+2]],d=src[columns[x+3]];
     dst[x]=a;dst[x+1]=b;dst[x+2]=c;dst[x+3]=d;
    }
    for(;x<w;x++)dst[x]=src[columns[x]];
   }
   last=sy;
  }
  if(full&&left){
   unsigned gap=y+1<h?480-w:480-left-w;
   memset(dst+w,0,gap*4);
  }
  /* floor(y*224/h), without a divide or reciprocal multiply per row. */
  fraction+=224;
  if(fraction>=h){fraction-=h;sy++;}
 }
 if(full&&top)memset(lcd+(top+h)*480,0,(272-top-h)*480*4);
#if defined(__mips__)
 /* This H1's saved native Config1=0x3e63318a has DL=4: 32-byte D-cache
  * lines (16 KiB, four ways). See h1-ps1/build/hardware/fastmap-probe/
  * mmu-inventory.json in the workspace. The old presenter retains its
  * conservative 16-byte sweep for A/B; no interrupt state is changed. */
 if(((uintptr_t)lcd&0xe0000000u)==0x80000000u){
  __asm__ volatile("sync":::"memory");
  if(full||w==480){
   uintptr_t a=(uintptr_t)lcd&~31u,end=(uintptr_t)(lcd+480*272);
   for(;a<end;a+=32)__asm__ volatile("cache 0x15,0(%0)"::"r"(a):"memory");
  }else for(unsigned y=0;y<h;y++){
   uint32_t *row=lcd+(top+y)*480+left;
   uintptr_t a=(uintptr_t)row&~31u,end=(uintptr_t)(row+w);
   for(;a<end;a+=32)__asm__ volatile("cache 0x15,0(%0)"::"r"(a):"memory");
  }
  __asm__ volatile("sync":::"memory");
 }
#endif
 pgm_present_target=native;pgm_present_last_mode=mode;pgm_present_valid=1;
 phase(0);
}

#endif
