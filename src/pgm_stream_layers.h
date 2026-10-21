/* PGM background/text plans for a single cached RGB32 scanline.
 * GPL-2.0-or-later. Include after pgm_layers.h in pgm.c. The CPU must be
 * stopped between prepare and the final rendered row; scroll, tile RAM and
 * palettes therefore remain stable throughout a prepared frame.
 */
#ifndef H1_PGM_STREAM_LAYERS_H
#define H1_PGM_STREAM_LAYERS_H
#define PGM_STREAM_BG_SPANS 16u
typedef struct {
 const uint8_t *src;
 const uint32_t *pal;
 uint16_t left;
 uint8_t width,reverse;
 unsigned opaque;
} pgm_stream_bg_span;
typedef struct {
 uint8_t count[HEIGHT];
 uint32_t active[TILE_SLOTS];
 pgm_stream_bg_span spans[HEIGHT][PGM_STREAM_BG_SPANS];
 uint16_t text_head[HEIGHT];
 unsigned text_sx,text_sy;
} pgm_stream_layers_plan;
/* About 62 KiB including the pin table is heap allocated. No frame-sized
 * arrays or tile pin table consume the fixed BDA loading window. */
static pgm_stream_layers_plan *pgm_stream_layers;
static unsigned pgm_stream_layers_ready;

/* Cache only visible nonempty text tile blocks. Each block owns its
 * 64 index bytes, so text direct-map collisions never invalidate a pointer.
 * Per-row linked indices avoid a fixed 224x57 array or scanning all blocks.
 * Target MIPS layout: 92 B/block; initial 32 blocks use 2944 heap bytes. */
typedef struct {
 const uint32_t *pal;
 uint16_t left;
 uint8_t top,width,rx,ry,flags,reserved;
 uint16_t next[8];
 uint8_t indices[64];
} pgm_stream_text_block;
static pgm_stream_text_block *pgm_stream_text_blocks;
static unsigned pgm_stream_text_capacity,pgm_stream_text_block_used,pgm_stream_text_plan_ready;
static int pgm_stream_text_plan_prepare(void){
 pgm_stream_text_plan_ready=0;pgm_stream_text_block_used=0;
 pgm_stream_layers_plan *plan=pgm_stream_layers;
 memset(plan->text_head,255,sizeof plan->text_head);
 unsigned sx=plan->text_sx,sy=plan->text_sy,size=parts[PGM_TILES].size;
 for(unsigned y=0;y<HEIGHT;){
  unsigned yy=(y+sy)&255,ty=yy&7,height=8-ty;if(height>HEIGHT-y)height=HEIGHT-y;
  for(unsigned x=0;x<WIDTH;){
   unsigned xx=(x+sx)&511,at=((yy>>3)*64+(xx>>3))*4;
   unsigned tile=word(tx+at),attr=word(tx+at+2),rx=xx&7,width=8-rx;if(width>WIDTH-x)width=WIDTH-x;
   if(tile>0xbfff)tile=tile-0xc000+0x20000;
   unsigned base=tile*32;
   if(base+4<=size){
    const uint8_t *data=text_data_fast(tile);
    unsigned left=(attr&0x40)?8-rx-width:rx,clip=((1u<<width)-1)<<left,active=0;
    for(unsigned dy=0;dy<height;dy++){
     unsigned ry=(attr&0x80)?7-ty-dy:ty+dy;
     if(base+ry*4+4<=size&&(data[64+ry]&clip))active|=1u<<dy;
    }
    if(active){
     if(pgm_stream_text_block_used==pgm_stream_text_capacity){
      unsigned capacity=pgm_stream_text_capacity?pgm_stream_text_capacity*2:32;
      pgm_stream_text_block *grown=realloc(pgm_stream_text_blocks,capacity*sizeof *grown);
      /* Text allocation failure retains the original immediate text path;
       * it does not turn an otherwise valid background stream into fallback. */
      if(!grown)return 0;
      pgm_stream_text_blocks=grown;pgm_stream_text_capacity=capacity;
     }
     unsigned index=pgm_stream_text_block_used++;
     pgm_stream_text_block *block=pgm_stream_text_blocks+index;
     block->pal=colors+0x800+((attr&0x3e)<<3);block->left=x;block->top=y;block->width=width;
     block->rx=rx;block->ry=(attr&0x80)?7-ty:ty;block->flags=(attr>>6)&3;
     memcpy(block->indices,data,64);
     for(unsigned dy=0;dy<height;dy++)if(active&(1u<<dy)){
      unsigned line=y+dy;block->next[dy]=plan->text_head[line];plan->text_head[line]=index;
     }
    }
   }
   x+=width;
  }
  y+=height;
 }
 pgm_stream_text_plan_ready=1;return 1;
}

static void pgm_stream_layers_close(void){
 free(pgm_stream_layers);pgm_stream_layers=0;pgm_stream_layers_ready=0;
 free(pgm_stream_text_blocks);pgm_stream_text_blocks=0;pgm_stream_text_capacity=pgm_stream_text_block_used=pgm_stream_text_plan_ready=0;
}
static int pgm_stream_layers_prepare(void){
 pgm_stream_layers_ready=0;pgm_stream_text_plan_ready=0;
 if(!pgm_stream_layers)pgm_stream_layers=malloc(sizeof *pgm_stream_layers);
 if(!pgm_stream_layers)return 0;
 pgm_stream_layers_plan *plan=pgm_stream_layers;
 memset(plan->count,0,sizeof plan->count);memset(plan->active,255,sizeof plan->active);
 plan->text_sx=word(vreg+0x6000);plan->text_sy=word(vreg+0x5000);
 unsigned sy=word(vreg+0x2000),sx=word(vreg+0x3000),size=parts[PGM_TILES].size;
 for(unsigned y=0;y<HEIGHT;){
  unsigned yy=(y+sy)&2047,row=yy&31,offset=(sx+word(rows+y*2))&2047;
  unsigned height=32-row;if(height>HEIGHT-y)height=HEIGHT-y;
  for(unsigned k=1;k<height;k++)if(((sx+word(rows+(y+k)*2))&2047)!=offset){height=k;break;}
  for(unsigned x=0;x<WIDTH;){
   unsigned xx=(x+offset)&2047,at=((yy>>5)*64+(xx>>5))*4;
   unsigned tile=word(bg+at),attr=word(bg+at+2);if(tile>0x7ff)tile+=0x1000;
   unsigned rx=xx&31,width=32-rx;if(width>WIDTH-x)width=WIDTH-x;
   unsigned base=tile*640;
   if(base+20<=size){
    unsigned slot=tile&(TILE_SLOTS-1),valid=(size-base)/20,nonempty=~0u,opaque=0;
    const uint8_t *data=tile_data(tile);
    if(tile_row_masks){nonempty=tile_row_masks[slot*2];opaque=tile_row_masks[slot*2+1];}
    const uint32_t *pal=colors+0x400+((attr&0x3e)<<4);
    for(unsigned dy=0;dy<height;dy++){
     unsigned ry=(attr&0x80)?31-row-dy:row+dy;
     if(ry>=valid||!(nonempty&(1u<<ry)))continue;
     unsigned reverse=(attr>>6)&1,solid=(opaque>>ry)&1;
     const uint8_t *src=data+ry*32+(reverse?31-rx:rx);
     /* A cropped part of a mixed row can itself be wholly transparent.
      * It must not reserve a slot and spuriously conflict with a visible
      * tile that maps to the same decoded-cache slot. */
     if(!solid){
      unsigned k=0;
      if(reverse){for(;k<width;k++)if(src[-(int)k]!=31)break;}
      else for(;k<width;k++)if(src[k]!=31)break;
      if(k==width)continue;
     }
     if(plan->active[slot]!=~0u&&plan->active[slot]!=tile)return 0;
     unsigned line=y+dy,n=plan->count[line];if(n>=PGM_STREAM_BG_SPANS)return 0;
     plan->active[slot]=tile;
     plan->spans[line][n]=(pgm_stream_bg_span){src,pal,(uint16_t)x,(uint8_t)width,(uint8_t)reverse,solid};
     plan->count[line]=(uint8_t)(n+1);
    }
   }
   x+=width;
  }
  y+=height;
 }
 /* Even a later fully transparent tile may have evicted an ACTIVE tile
  * after its source pointers were recorded. Restore each occupied slot;
  * every recorded pointer addresses that slot's stable decoded allocation.
  * Distinct ACTIVE slots cannot evict one another during this final pass. */
 for(unsigned slot=0;slot<TILE_SLOTS;slot++)if(plan->active[slot]!=~0u)tile_data(plan->active[slot]);
 pgm_stream_text_plan_prepare();
 pgm_stream_layers_ready=1;return 1;
}
static void pgm_stream_background_row(unsigned y,uint32_t *row){
 if(!pgm_stream_layers_ready||y>=HEIGHT)return;
 pgm_stream_layers_plan *plan=pgm_stream_layers;
 for(unsigned i=0;i<plan->count[y];i++){
  const pgm_stream_bg_span *span=plan->spans[y]+i;
  uint32_t *dst=row+span->left;
  if(span->opaque)background_opaque_span(dst,span->src,span->pal,span->width,1,span->reverse);
  else if(span->reverse){for(unsigned k=0;k<span->width;k++){unsigned v=span->src[-(int)k];if(v!=31)dst[k]=span->pal[v];}}
  else for(unsigned k=0;k<span->width;k++){unsigned v=span->src[k];if(v!=31)dst[k]=span->pal[v];}
 }
}
static void pgm_stream_text_row_old(unsigned y,uint32_t *row){
 if(!pgm_stream_layers_ready||y>=HEIGHT)return;
 unsigned sx=pgm_stream_layers->text_sx,sy=pgm_stream_layers->text_sy;
 unsigned yy=(y+sy)&255,ty=yy&7,size=parts[PGM_TILES].size;
 for(unsigned x=0;x<WIDTH;){
  unsigned xx=(x+sx)&511,at=((yy>>3)*64+(xx>>3))*4;
  unsigned tile=word(tx+at),attr=word(tx+at+2),rx=xx&7,width=8-rx;if(width>WIDTH-x)width=WIDTH-x;
  if(tile>0xbfff)tile=tile-0xc000+0x20000;
  unsigned ry=(attr&0x80)?7-ty:ty;
  if(tile*32+ry*4+4<=size){
   const uint8_t *data=text_data_fast(tile);
   if(data[64+ry]){
    const uint8_t *src=data+ry*8;const uint32_t *pal=colors+0x800+((attr&0x3e)<<3);
    uint32_t *dst=row+x;
    if(attr&0x40){for(unsigned k=0;k<width;k++){unsigned v=src[7-rx-k];if(v!=15)dst[k]=pal[v];}}
    else for(unsigned k=0;k<width;k++){unsigned v=src[rx+k];if(v!=15)dst[k]=pal[v];}
   }
  }
  x+=width;
 }
}

static void pgm_stream_text_row(unsigned y,uint32_t *row){
 if(!pgm_stream_layers_ready||y>=HEIGHT)return;
 if(!pgm_stream_text_plan_ready){pgm_stream_text_row_old(y,row);return;}
 for(unsigned index=pgm_stream_layers->text_head[y];index!=65535;){
  const pgm_stream_text_block *block=pgm_stream_text_blocks+index;
  unsigned dy=y-block->top,ry=(block->flags&2)?block->ry-dy:block->ry+dy;
  const uint8_t *src=block->indices+ry*8;const uint32_t *pal=block->pal;
  uint32_t *dst=row+block->left;
  if(block->flags&1){for(unsigned k=0;k<block->width;k++){unsigned v=src[7-block->rx-k];if(v!=15)dst[k]=pal[v];}}
  else for(unsigned k=0;k<block->width;k++){unsigned v=src[block->rx+k];if(v!=15)dst[k]=pal[v];}
  index=block->next[dy];
 }
}
#endif
