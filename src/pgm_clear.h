/* PGM background-transparent preclear. GPL-2.0-or-later.
 * Include after tile_data() and the decoded row masks in pgm.c.
 * Only pixels the background will leave transparent are initialized. The
 * caller must subsequently draw low-priority sprites and the background,
 * then foreground sprites/text, before presenting the completed frame.
 */
#ifndef H1_PGM_CLEAR_H
#define H1_PGM_CLEAR_H
/* Fields after calls describe the most recent completed debug-enabled call.
 * A "row" is one horizontal tile span, not an entire LCD scanline. Normal
 * drawing has no per-pixel statistics work when enabled is zero. */
volatile struct {
 unsigned magic,version,enabled,calls,opaque_rows,empty_rows,mixed_rows,invalid_rows;
 unsigned opaque_pixels,empty_pixels,mixed_pixels,written_pixels;
} h1_pgm_clear_stats={0x31434750,1};
static inline void background_clear_fill(uint32_t *dst,unsigned width,uint32_t backdrop){
 while(width>=8){
  dst[0]=backdrop;dst[1]=backdrop;dst[2]=backdrop;dst[3]=backdrop;
  dst[4]=backdrop;dst[5]=backdrop;dst[6]=backdrop;dst[7]=backdrop;
  dst+=8;width-=8;
 }
 for(unsigned k=0;k<width;k++)dst[k]=backdrop;
}
static inline unsigned background_clear_mixed(uint32_t *dst,const uint8_t *src,unsigned width,unsigned reverse,uint32_t backdrop,unsigned debug){
 unsigned written=0;
 if(debug){
  if(reverse){for(unsigned k=0;k<width;k++)if(src[-(int)k]==31){dst[k]=backdrop;++written;}}
  else for(unsigned k=0;k<width;k++)if(src[k]==31){dst[k]=backdrop;++written;}
 }else{
  if(reverse){for(unsigned k=0;k<width;k++)if(src[-(int)k]==31)dst[k]=backdrop;}
  else for(unsigned k=0;k<width;k++)if(src[k]==31)dst[k]=backdrop;
 }
 return written;
}
static void background_clear_fast(uint32_t backdrop){
 unsigned sy=word(vreg+0x2000),sx=word(vreg+0x3000),size=parts[PGM_TILES].size;
 unsigned debug=h1_pgm_clear_stats.enabled,opaque_rows=0,empty_rows=0,mixed_rows=0,invalid_rows=0;
 unsigned opaque_pixels=0,empty_pixels=0,mixed_pixels=0,written_pixels=0;
 for(unsigned y=0;y<HEIGHT;){
  unsigned yy=(y+sy)&2047,row=yy&31,offset=(sx+word(rows+y*2))&2047;
  unsigned height=32-row;if(height>HEIGHT-y)height=HEIGHT-y;
  for(unsigned k=1;k<height;k++)if(((sx+word(rows+(y+k)*2))&2047)!=offset){height=k;break;}
  for(unsigned x=0;x<WIDTH;){
   unsigned xx=(x+offset)&2047,at=((yy>>5)*64+(xx>>5))*4;
   unsigned tile=word(bg+at),attr=word(bg+at+2);if(tile>0x7ff)tile+=0x1000;
   unsigned rx=xx&31,width=32-rx;if(width>WIDTH-x)width=WIDTH-x;
   unsigned base=tile*640,valid=0,nonempty=~0u,opaque=0;const uint8_t *data=0;
   if(base+20<=size){
    valid=(size-base)/20;data=tile_data(tile);
    if(tile_row_masks){unsigned slot=tile&(TILE_SLOTS-1);nonempty=tile_row_masks[slot*2];opaque=tile_row_masks[slot*2+1];}
   }
   uint32_t *dst=pixels+y*WIDTH+x;
   for(unsigned dy=0;dy<height;dy++,dst+=WIDTH){
    unsigned ry=(attr&0x80)?31-row-dy:row+dy;
    if(ry>=valid){
     background_clear_fill(dst,width,backdrop);
     if(debug){++empty_rows;++invalid_rows;empty_pixels+=width;written_pixels+=width;}
    }else if(opaque&(1u<<ry)){
     if(debug){++opaque_rows;opaque_pixels+=width;}
    }else if(!(nonempty&(1u<<ry))){
     background_clear_fill(dst,width,backdrop);
     if(debug){++empty_rows;empty_pixels+=width;written_pixels+=width;}
    }else{
     const uint8_t *src=data+ry*32+((attr&0x40)?31-rx:rx);
     unsigned written=background_clear_mixed(dst,src,width,attr&0x40,backdrop,debug);
     if(debug){++mixed_rows;mixed_pixels+=width;written_pixels+=written;}
    }
   }
   x+=width;
  }
  y+=height;
 }
 if(debug){
  h1_pgm_clear_stats.opaque_rows=opaque_rows;h1_pgm_clear_stats.empty_rows=empty_rows;
  h1_pgm_clear_stats.mixed_rows=mixed_rows;h1_pgm_clear_stats.invalid_rows=invalid_rows;
  h1_pgm_clear_stats.opaque_pixels=opaque_pixels;h1_pgm_clear_stats.empty_pixels=empty_pixels;
  h1_pgm_clear_stats.mixed_pixels=mixed_pixels;h1_pgm_clear_stats.written_pixels=written_pixels;
  ++h1_pgm_clear_stats.calls;
 }
}
#endif
