/* PGM scanline sprite plans. Include after sprite_data()/zoom_extent().
 * GPL-2.0-or-later. Geometry follows sprite_layer(), including its prefix
 * priority rule. The plans own no ROM-cache pointers: every decoded sprite
 * is pinned in sprite_pixels for the duration of a prepared frame. */
#ifndef H1_PGM_STREAM_SPRITES_H
#define H1_PGM_STREAM_SPRITES_H

#define PGM_STREAM_SPRITE_LIMIT 256u
enum {
 PGM_STREAM_SPRITE_FLIPX=1,PGM_STREAM_SPRITE_FLIPY=2,
 PGM_STREAM_SPRITE_IDENTITY=4,PGM_STREAM_SPRITE_GROWX=8,
 PGM_STREAM_SPRITE_GROWY=16
};
typedef struct {
 const uint8_t *decoded;
 const uint32_t *pal;
 const uint16_t *columns,*ymap;
 unsigned boff,xzoom,yzoom;
 int16_t xpos,ypos;
 uint16_t fullw,high,outw,outh,left,width,top,row_count,source_x;
 uint16_t flags;
} pgm_stream_sprite_plan;
static pgm_stream_sprite_plan *pgm_stream_sprite_plans;
static uint16_t *pgm_stream_sprite_columns,*pgm_stream_sprite_ymaps;
static decoded_sprite *pgm_stream_sprite_virtual_tags;
static unsigned pgm_stream_sprite_count,pgm_stream_sprite_behind,pgm_stream_sprite_mode;

static void pgm_stream_sprites_close(void){
 free(pgm_stream_sprite_plans);free(pgm_stream_sprite_columns);free(pgm_stream_sprite_ymaps);free(pgm_stream_sprite_virtual_tags);
 pgm_stream_sprite_plans=0;pgm_stream_sprite_columns=pgm_stream_sprite_ymaps=0;
 pgm_stream_sprite_virtual_tags=0;
 pgm_stream_sprite_count=pgm_stream_sprite_behind=0;
}

static int pgm_stream_sprites_prepare(void){
 unsigned count=0,behind=0,prefix=1,bytes=0,need_columns=0,need_ymaps=0;
 unsigned mode=h1_pgm_sprite_mode;
 /* Clear readiness first: an allocation/capacity failure must never expose
  * plans from a previous frame to a caller taking the fallback path. */
 pgm_stream_sprite_count=pgm_stream_sprite_behind=0;
 if(!pgm_stream_sprite_plans){
  pgm_stream_sprite_plans=malloc(PGM_STREAM_SPRITE_LIMIT*sizeof *pgm_stream_sprite_plans);
  if(!pgm_stream_sprite_plans)return 0;
 }
 for(unsigned at=0;at<0x500;at+=5){
  const uint16_t *s=sprites+at;
  unsigned high=s[4]&511,wide=(s[4]>>9)&63,attr=s[2];
  if(!high)break;
  /* The original low layer is a descriptor prefix, not a per-sprite flag.
   * Even an offscreen/invalid descriptor ends the prefix at this point. */
  if(prefix&&!(attr&0x80)){behind=count;prefix=0;}
  unsigned boff=(((attr&127)<<16)|s[3])*2;
  if(!wide||boff+4>parts[PGM_MASK].size)continue;
  int xpos=s[0]&2047,ypos=s[1]&1023;
  if(xpos>1023)xpos-=2048;if(ypos>511)ypos-=1024;
  unsigned zx=(s[0]>>11)&15,zy=(s[1]>>11)&15,gx=s[0]&0x8000,gy=s[1]&0x8000;
  if(gx)zx=16-zx;if(gy)zy=16-zy;
  unsigned xzoom=word(vreg+0x1000+zx*4)<<16|word(vreg+0x1002+zx*4);
  unsigned yzoom=word(vreg+0x1000+zy*4)<<16|word(vreg+0x1002+zy*4);
  unsigned fullw=wide*16,outw=xzoom?zoom_extent(fullw,xzoom,gx):fullw;
  unsigned outh=yzoom?zoom_extent(high,yzoom,gy):high;
  if(!outw||!outh||xpos>=(int)WIDTH||xpos+(int)outw<=0||ypos>=(int)HEIGHT||ypos+(int)outh<=0)continue;
  /* Count every visible descriptor, including duplicate cache keys. Thus
   * even direct-map tag collisions cannot consume more than this reserve. */
  unsigned decoded_bytes=fullw*high;
  if(decoded_bytes>sprite_capacity-bytes)return 0;
  bytes+=decoded_bytes;
  pgm_stream_sprite_plan *p=pgm_stream_sprite_plans+count++;
  int left=xpos<0?0:xpos,right=xpos+(int)outw;
  int top=ypos<0?0:ypos,bottom=ypos+(int)outh;
  if(right>(int)WIDTH)right=WIDTH;if(bottom>(int)HEIGHT)bottom=HEIGHT;
  unsigned flags=(attr&0x2000?PGM_STREAM_SPRITE_FLIPX:0)|(attr&0x4000?PGM_STREAM_SPRITE_FLIPY:0);
  if(mode&&!xzoom)flags|=PGM_STREAM_SPRITE_IDENTITY;
  if(gx)flags|=PGM_STREAM_SPRITE_GROWX;if(gy)flags|=PGM_STREAM_SPRITE_GROWY;
  p->decoded=0;p->pal=colors+((attr>>8&31)*32);p->columns=p->ymap=0;
  p->boff=boff;p->xzoom=xzoom;p->yzoom=yzoom;p->xpos=xpos;p->ypos=ypos;
  p->fullw=fullw;p->high=high;p->outw=outw;p->outh=outh;
  p->left=left;p->width=right-left;p->top=top;p->row_count=bottom-top;p->flags=flags;
  p->source_x=(flags&PGM_STREAM_SPRITE_FLIPX)?fullw-1-(left-xpos):left-xpos;
  if(!(flags&PGM_STREAM_SPRITE_IDENTITY))need_columns=1;
  if(yzoom)need_ymaps=1;
 }
 if(prefix)behind=count;
 /* Keep mapping storage off the BDA load window and allocate it only when
  * a frame actually needs that kind of zoom. Common identity sprites need
  * neither a 448-entry column map nor a 224-entry vertical map. */
 if(need_columns&&!pgm_stream_sprite_columns){
  pgm_stream_sprite_columns=malloc(PGM_STREAM_SPRITE_LIMIT*WIDTH*sizeof *pgm_stream_sprite_columns);
  if(!pgm_stream_sprite_columns)return 0;
 }
 if(need_ymaps&&!pgm_stream_sprite_ymaps){
  pgm_stream_sprite_ymaps=malloc(PGM_STREAM_SPRITE_LIMIT*HEIGHT*sizeof *pgm_stream_sprite_ymaps);
  if(!pgm_stream_sprite_ymaps)return 0;
 }
 /* Usually the all-visible bound already fits. Near the pool's end, avoid
  * discarding warm entries merely because that bound includes cache hits.
  * Simulate the exact descriptor order, including direct-map collisions:
  * a hit A, miss B, A sequence can require decoding A again after B evicts
  * its tag. No real tag or pinned allocation is changed by this preflight. */
 unsigned reset=sprite_used>sprite_capacity;
 if(!reset&&bytes>sprite_capacity-sprite_used){
  if(!pgm_stream_sprite_virtual_tags){
   pgm_stream_sprite_virtual_tags=malloc(SPRITE_SLOTS*sizeof *pgm_stream_sprite_virtual_tags);
   if(!pgm_stream_sprite_virtual_tags)return 0;
  }
  memcpy(pgm_stream_sprite_virtual_tags,sprite_tags,SPRITE_SLOTS*sizeof *sprite_tags);
  unsigned additional=0;
  for(unsigned i=0;i<count;i++){
   const pgm_stream_sprite_plan *p=pgm_stream_sprite_plans+i;unsigned wide=p->fullw/16;
   unsigned slot=((p->boff>>2)^(wide*17)^(p->high*31))&(SPRITE_SLOTS-1);
   decoded_sprite *tag=pgm_stream_sprite_virtual_tags+slot;
   if(tag->mask!=p->boff||tag->width!=wide||tag->height!=p->high){
    additional+=p->fullw*p->high;
    *tag=(decoded_sprite){p->boff,wide,p->high,0};
   }
  }
  reset=additional>sprite_capacity-sprite_used;
 }
 /* Reset before decoding, never during this frame. The exact preflight
  * proves the no-reset case; the all-visible bound proves the reset case.
  * Decode sizes are multiples of 16, so alignment adds no hidden bytes. */
 if(reset){
  sprite_used=0;memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);
 }
 for(unsigned i=0;i<count;i++){
  pgm_stream_sprite_plan *p=pgm_stream_sprite_plans+i;
  if(!(p->flags&PGM_STREAM_SPRITE_IDENTITY)){
   uint16_t *columns=pgm_stream_sprite_columns+i*WIDTH;unsigned destx=0;
   p->columns=columns;
   for(unsigned x=0;x<p->fullw;x++){
    unsigned bit=(p->xzoom>>(x&31))&1,n=(p->flags&PGM_STREAM_SPRITE_GROWX)?1+bit:1-bit;
    for(unsigned k=0;k<n;k++,destx++){
     int dx=p->xpos+((p->flags&PGM_STREAM_SPRITE_FLIPX)?(int)p->outw-1-(int)destx:(int)destx);
     if(dx>=p->left&&dx<(int)(p->left+p->width))columns[dx-p->left]=x;
    }
   }
  }
  if(p->yzoom){
   uint16_t *ymap=pgm_stream_sprite_ymaps+i*HEIGHT;unsigned desty=0;
   p->ymap=ymap;
   for(unsigned y=0;y<p->high;y++){
    unsigned bit=(p->yzoom>>(y&31))&1,n=(p->flags&PGM_STREAM_SPRITE_GROWY)?1+bit:1-bit;
    for(unsigned k=0;k<n;k++,desty++){
     int dy=p->ypos+((p->flags&PGM_STREAM_SPRITE_FLIPY)?(int)p->outh-1-(int)desty:(int)desty);
     unsigned line=dy-p->top;if(line<p->row_count)ymap[line]=y;
    }
   }
  }
  p->decoded=sprite_data(p->boff,p->fullw/16,p->high);
  /* Clip/flip's starting X is constant across rows. Keep it out of the
   * hot identity path rather than loading and adding it on every line. */
  if(p->flags&PGM_STREAM_SPRITE_IDENTITY)p->decoded+=p->source_x;
 }
 pgm_stream_sprite_mode=mode;pgm_stream_sprite_behind=behind;pgm_stream_sprite_count=count;
 return 1;
}

static void pgm_stream_sprites_row(unsigned y,uint32_t *row,unsigned behind){
 unsigned first=behind?0:pgm_stream_sprite_behind;
 unsigned end=behind?pgm_stream_sprite_behind:pgm_stream_sprite_count,mode=pgm_stream_sprite_mode;
 for(unsigned i=first;i<end;i++){
  const pgm_stream_sprite_plan *p=pgm_stream_sprite_plans+i;
  unsigned line=y-p->top;if(line>=p->row_count)continue;
  unsigned source_y=p->ymap?p->ymap[line]:
   ((p->flags&PGM_STREAM_SPRITE_FLIPY)?p->high-1-(y-p->ypos):y-p->ypos);
  const uint8_t *src=p->decoded+source_y*p->fullw;
  const uint32_t *pal=p->pal;uint32_t *dst=row+p->left;unsigned width=p->width;
  if(p->flags&PGM_STREAM_SPRITE_IDENTITY){
   ++h1_pgm_perf.identity_rows;h1_pgm_perf.identity_pixels+=width;
#if defined(__mips__) && H1_MXU
   if(mode==3){if(p->flags&PGM_STREAM_SPRITE_FLIPX)h1_pgm_sprite_mxu_rev(dst,src,pal,width);else h1_pgm_sprite_mxu_row(dst,src,pal,width);}else
#endif
#if defined(__mips__)
   if(mode==2){if(p->flags&PGM_STREAM_SPRITE_FLIPX)h1_pgm_sprite_rev(dst,src,pal,width);else h1_pgm_sprite_row(dst,src,pal,width);}else
#endif
   if(p->flags&PGM_STREAM_SPRITE_FLIPX){for(unsigned x=0;x<width;x++){unsigned v=src[-(int)x];if(v!=255)dst[x]=pal[v];}}
   else for(unsigned x=0;x<width;x++){unsigned v=src[x];if(v!=255)dst[x]=pal[v];}
  }else{
   const uint16_t *columns=p->columns;
   ++h1_pgm_perf.mapped_rows;h1_pgm_perf.mapped_pixels+=width;
#if defined(__mips__) && H1_MXU
   if(mode==3)h1_pgm_sprite_mxu_map(dst,src,pal,columns,width);else
#endif
#if defined(__mips__)
   if(mode==2)h1_pgm_sprite_map(dst,src,pal,columns,width);else
#endif
   for(unsigned x=0;x<width;x++){unsigned v=src[columns[x]];if(v!=255)dst[x]=pal[v];}
  }
 }
}
#endif
