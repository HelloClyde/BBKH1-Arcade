/* Host-only mathematical stand-in. Actual MXU instructions run on physical H1. */
int h1_sprite_tile_opaque(const unsigned *gfx){
 for(unsigned word=0;word<32;word++)for(unsigned x=0;x<8;x++)if(!((gfx[word]>>(x*4))&15))return 0;
 return 1;
}
unsigned h1_test_sprite_key=0x80000000u;
unsigned h1_sprite_opaque_key(const unsigned *gfx){return h1_sprite_tile_opaque(gfx)?h1_test_sprite_key:~0u;}
void h1_mxu_tile16_palette(void *target,const unsigned *gfx,const unsigned *pal,unsigned flags){
 uint16_t *dst=target;
 for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++){
  unsigned sx=flags&1?15-x:x;
  dst[(flags&2?15-y:y)*352+x]=(uint16_t)pal[(gfx[y*2+sx/8]>>(28-(sx%8)*4))&15];
 }
}
void h1_mxu_tile16_rgb(void *target,const void *source,unsigned flags){
 uint16_t *dst=target;const uint16_t *src=source;
 for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)
  dst[(flags&2?15-y:y)*352+x]=src[y*16+(flags&1?15-x:x)];
}
void h1_mxu_tile16_opaque(void *target,const unsigned *gfx,const unsigned *pairs,unsigned flags){
 uint16_t *dst=target;
 for(unsigned y=0;y<16;y++)for(unsigned pair=0;pair<8;pair++){
  unsigned word=flags&1?1-pair/4:pair/4,shift=flags&1?(pair%4)*8:24-(pair%4)*8;
  unsigned b=(gfx[y*2+word]>>shift)&255,dy=flags&2?15-y:y;
  dst[dy*352+pair*2]=(uint16_t)pairs[b];dst[dy*352+pair*2+1]=pairs[b]>>16;
 }
}
void h1_mxu_tile16(void *target,const unsigned *gfx,const unsigned *pairs,unsigned flags,const unsigned *keep){
 uint16_t *dst=target;
 for(unsigned y=0;y<16;y++)for(unsigned pair=0;pair<8;pair++){
  unsigned word=flags&1?1-pair/4:pair/4,shift=flags&1?(pair%4)*8:24-(pair%4)*8;
  unsigned b=(gfx[y*2+word]>>shift)&255,dy=flags&2?15-y:y;
  for(unsigned lane=0;lane<2;lane++){
   unsigned i=dy*352+pair*2+lane,new_color=(pairs[b]>>(lane*16))&65535,k=(keep[b]>>(lane*16))&65535;
   dst[i]=(uint16_t)(new_color+dst[i]*k);
  }
 }
}
