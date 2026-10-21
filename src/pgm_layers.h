/* PGM tile-span composition. GPL-2.0-or-later.
 * Include after the scanline background/text implementations in pgm.c.
 * Descriptor reads and decoded-cache lookup are shared by a vertical tile
 * span. A background span ends whenever row scroll changes, preserving the
 * hardware's independent per-scanline scroll values.
 */
volatile unsigned h1_pgm_bg_mxu_mode=1;
#if defined(__mips__) && H1_MXU
extern void h1_pgm_bg_mxu_span(uint32_t *,const uint8_t *,const uint32_t *,unsigned,unsigned,unsigned);
#endif
/* Preload four indices and their four palette values before any store. This
 * exposes independent MIPS loads and removes the per-pixel transparency
 * branch on rows the decoded cache has proved completely opaque. */
static inline void background_opaque_row(uint32_t *dst,const uint8_t *src,const uint32_t *pal,unsigned width,unsigned reverse){
 if(reverse){
  unsigned k=0,blocks=width&~3u;
  for(;k<blocks;k+=4){
   int offset=-(int)k;
   unsigned a=src[offset],b=src[offset-1],c=src[offset-2],d=src[offset-3];
   uint32_t ca=pal[a],cb=pal[b],cc=pal[c],cd=pal[d];
   dst[k]=ca;dst[k+1]=cb;dst[k+2]=cc;dst[k+3]=cd;
  }
  for(;k<width;k++)dst[k]=pal[src[-(int)k]];
 }else{
  while(width>=4){
   unsigned a=src[0],b=src[1],c=src[2],d=src[3];
   uint32_t ca=pal[a],cb=pal[b],cc=pal[c],cd=pal[d];
   dst[0]=ca;dst[1]=cb;dst[2]=cc;dst[3]=cd;src+=4;dst+=4;width-=4;
  }
  for(unsigned k=0;k<width;k++)dst[k]=pal[src[k]];
 }
}
static void background_opaque_span_c(uint32_t *dst,const uint8_t *src,const uint32_t *pal,unsigned width,unsigned height,unsigned flags){
 int stride=(flags&2)?-32:32;
 for(unsigned y=0;y<height;y++)background_opaque_row(dst+y*448,src+(int)y*stride,pal,width,flags&1);
}
static inline void background_opaque_span(uint32_t *dst,const uint8_t *src,const uint32_t *pal,unsigned width,unsigned height,unsigned flags){
#if defined(__mips__) && H1_MXU
 if(h1_pgm_bg_mxu_mode){h1_pgm_bg_mxu_span(dst,src,pal,width,height,flags);return;}
#endif
 background_opaque_span_c(dst,src,pal,width,height,flags);
}
static void background_fast(void){
 unsigned sy=word(vreg+0x2000),sx=word(vreg+0x3000),size=parts[PGM_TILES].size;
 for(unsigned y=0;y<224;){
  unsigned yy=(y+sy)&2047,row=yy&31,offset=(sx+word(rows+y*2))&2047;
  unsigned height=32-row;if(height>224-y)height=224-y;
  for(unsigned k=1;k<height;k++)if(((sx+word(rows+(y+k)*2))&2047)!=offset){height=k;break;}
  for(unsigned x=0;x<448;){
   unsigned xx=(x+offset)&2047,at=((yy>>5)*64+(xx>>5))*4;
   unsigned tile=word(bg+at),attr=word(bg+at+2);if(tile>0x7ff)tile+=0x1000;
   unsigned rx=xx&31,width=32-rx;if(width>448-x)width=448-x;
   unsigned base=tile*640;
   if(base<size){
    unsigned valid=(size-base)/20;const uint8_t *data=tile_data(tile);
    unsigned slot=tile&(TILE_SLOTS-1),nonempty=~0u,opaque=0;
    if(tile_row_masks){nonempty=tile_row_masks[slot*2];opaque=tile_row_masks[slot*2+1];}
    const uint32_t *pal=colors+0x400+((attr&0x3e)<<4);
    uint32_t *dst=pixels+y*448+x;
    for(unsigned dy=0;dy<height;){
     unsigned ry=(attr&0x80)?31-row-dy:row+dy;
     if(ry>=valid||!(nonempty&(1u<<ry))){++dy;dst+=448;continue;}
     const uint8_t *src=data+ry*32;
     if(opaque&(1u<<ry)){
      unsigned count=1;
      for(;dy+count<height;count++){
       unsigned next=(attr&0x80)?ry-count:ry+count;
       if(next>=valid||!(opaque&(1u<<next)))break;
      }
      background_opaque_span(dst,src+((attr&0x40)?31-rx:rx),pal,width,count,(attr>>6)&3);
      dy+=count;dst+=count*448;continue;
     }else if(attr&0x40){for(unsigned k=0;k<width;k++){unsigned v=src[31-rx-k];if(v!=31)dst[k]=pal[v];}}
     else for(unsigned k=0;k<width;k++){unsigned v=src[rx+k];if(v!=31)dst[k]=pal[v];}
     ++dy;dst+=448;
    }
   }
   x+=width;
  }
  y+=height;
 }
}
static const uint8_t *text_data_fast(unsigned tile){
 unsigned slot=tile&511;uint8_t *data=text_pixels+slot*72;
 if(text_tags[slot]!=tile){
  uint8_t packed[32];unsigned bytes=parts[PGM_TILES].size-tile*32;if(bytes>32)bytes=32;
  memset(packed,255,sizeof packed);rom_copy(PGM_TILES,tile*32,packed,bytes);
  for(unsigned y=0;y<8;y++){unsigned mask=0;for(unsigned x=0;x<8;x++){
   unsigned v=(packed[y*4+x/2]>>((x&1)*4))&15;data[y*8+x]=v;if(v!=15)mask|=1u<<x;
  }data[64+y]=mask;}
  text_tags[slot]=tile;
 }
 return data;
}
static void text_layer_fast(void){
 unsigned sy=word(vreg+0x5000),sx=word(vreg+0x6000),size=parts[PGM_TILES].size;
 for(unsigned y=0;y<224;){
  unsigned yy=(y+sy)&255,row=yy&7,height=8-row;if(height>224-y)height=224-y;
  for(unsigned x=0;x<448;){
   unsigned xx=(x+sx)&511,at=((yy>>3)*64+(xx>>3))*4;
   unsigned tile=word(tx+at),attr=word(tx+at+2),rx=xx&7,width=8-rx;if(width>448-x)width=448-x;
   if(tile>0xbfff)tile=tile-0xc000+0x20000;
   unsigned base=tile*32;
   if(base<size){
    unsigned valid=(size-base)/4;const uint8_t *data=text_data_fast(tile);
    const uint32_t *pal=colors+0x800+((attr&0x3e)<<3);
    uint32_t *dst=pixels+y*448+x;
    for(unsigned dy=0;dy<height;dy++,dst+=448){
     unsigned ry=(attr&0x80)?7-row-dy:row+dy;if(ry>=valid||!data[64+ry])continue;
     const uint8_t *src=data+ry*8;
     if(attr&0x40){for(unsigned k=0;k<width;k++){unsigned v=src[7-rx-k];if(v!=15)dst[k]=pal[v];}}
     else for(unsigned k=0;k<width;k++){unsigned v=src[rx+k];if(v!=15)dst[k]=pal[v];}
    }
   }
   x+=width;
  }
  y+=height;
 }
}
