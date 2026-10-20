/* Original H1 PGM renderer, retained for frozen-frame physical differential checks.
 * GPL-2.0-or-later; PGM formats derive from pinned FBA (see NOTICE). */
static void background_reference(void){
 unsigned sy=word(vreg+0x2000),sx=word(vreg+0x3000);
 for(unsigned y=0;y<224;y++){
  unsigned yy=(y+sy)&2047,row=yy&31,offset=(sx+word(rows+y*2))&2047;
  for(unsigned x=0;x<448;){
   unsigned xx=(x+offset)&2047,at=((yy>>5)*64+(xx>>5))*4;
   unsigned tile=word(bg+at),attr=word(bg+at+2);if(tile>0x7ff)tile+=0x1000;
   unsigned rx=xx&31,n=32-rx;if(n>448-x)n=448-x;
   unsigned ry=(attr&0x80)?31-row:row,base=tile*640+ry*20,pal=0x400+((attr&0x3e)<<4);
   if(base+20<=parts[PGM_TILES].size){uint8_t raw[20];for(unsigned i=0;i<20;i++)raw[i]=rom8(PGM_TILES,base+i);
    for(unsigned k=0;k<n;k++){unsigned px=(attr&0x40)?31-(rx+k):rx+k,bit=px*5,b=bit>>3,s=bit&7;
     unsigned v=(raw[b]>>s)|((b+1<20?raw[b+1]:0)<<(8-s));v&=31;if(v!=31)pixels[y*448+x+k]=colors[pal+v];}}
   x+=n;
  }
 }
}
static void text_layer_reference(void){
 unsigned sy=word(vreg+0x5000),sx=word(vreg+0x6000);
 for(unsigned y=0;y<224;y++)for(unsigned x=0;x<448;){
  unsigned xx=(x+sx)&511,yy=(y+sy)&255,at=((yy>>3)*64+(xx>>3))*4;
  unsigned tile=word(tx+at),attr=word(tx+at+2),rx=xx&7,n=8-rx;if(n>448-x)n=448-x;
  if(tile>0xbfff)tile=tile-0xc000+0x20000;
  unsigned ry=(attr&0x80)?7-(yy&7):yy&7,base=tile*32+ry*4,pal=0x800+((attr&0x3e)<<3);
  if(base+4<=parts[PGM_TILES].size){uint32_t raw=rom16(PGM_TILES,base)|(rom16(PGM_TILES,base+2)<<16);
   for(unsigned k=0;k<n;k++){unsigned px=(attr&0x40)?7-(rx+k):rx+k,v=(raw>>(px*4))&15;if(v!=15)pixels[y*448+x+k]=colors[pal+v];}}
  x+=n;
 }
}
static unsigned sprite_index_reference;
static unsigned zoom_extent_reference(unsigned n,unsigned mask,unsigned grow){
 unsigned count=0;for(unsigned i=0;i<n;i++)count+=(mask>>(i&31))&1;
 return grow?n+count:n-count;
}
static void sprite_layer_reference(unsigned priority){
 for(;sprite_index_reference<0x500;sprite_index_reference+=5){
  const uint16_t *s=sprites+sprite_index_reference;unsigned high=s[4]&511,wide=(s[4]>>9)&63,attr=s[2];
  if(!high||(priority&&!(attr&0x80)))break;
  int xpos=s[0]&2047,ypos=s[1]&1023;if(xpos>1023)xpos-=2048;if(ypos>511)ypos-=1024;
  unsigned boff=(((attr&127)<<16)|s[3])*2;
  if(!wide||boff+4>parts[PGM_MASK].size)continue;
  unsigned color=(rom16(PGM_MASK,boff)|(rom16(PGM_MASK,boff+2)<<16))>>1;boff+=4;
  unsigned bits=0,remain=0,pal=(attr>>8&31)*32,flipx=attr&0x2000,flipy=attr&0x4000;
  unsigned zx=(s[0]>>11)&15,zy=(s[1]>>11)&15,gx=s[0]&0x8000,gy=s[1]&0x8000;
  if(gx)zx=16-zx;if(gy)zy=16-zy;
  unsigned xzoom=word(vreg+0x1000+zx*4)<<16|word(vreg+0x1002+zx*4),yzoom=word(vreg+0x1000+zy*4)<<16|word(vreg+0x1002+zy*4);
  uint8_t line[1008];unsigned desty=0,fullw=wide*16;
  unsigned outw=zoom_extent_reference(fullw,xzoom,gx),outh=zoom_extent_reference(high,yzoom,gy);
  for(unsigned y=0;y<high;y++){
   for(unsigned block=0;block<wide;block++){
    unsigned mask=rom16(PGM_MASK,boff);boff+=2;
    for(unsigned k=0;k<16;k++){unsigned v=255;if(!(mask&(1u<<k))){if(!remain){bits=rom16(PGM_COLOR,color);color+=2;remain=3;}v=bits&31;bits>>=5;--remain;}line[block*16+k]=v;}
   }
   unsigned yz=(yzoom>>(y&31))&1,repeats=gy?1+yz:1-yz;
   for(unsigned rep=0;rep<repeats;rep++,desty++){
    int dy=ypos+(flipy?(int)outh-1-(int)desty:(int)desty);if(dy<0||dy>=224)continue;
    unsigned destx=0;
    for(unsigned x=0;x<fullw;x++){
     unsigned xz=(xzoom>>(x&31))&1,n=gx?1+xz:1-xz;
     for(unsigned rr=0;rr<n;rr++,destx++){int dx=xpos+(flipx?(int)outw-1-(int)destx:(int)destx);if(dx>=0&&dx<448&&line[x]!=255)pixels[dy*448+dx]=colors[pal+line[x]];}
    }
   }
  }
 }
}
static void draw_reference(void){for(unsigned i=0;i<448*224;i++)pixels[i]=colors[0x3ff];sprite_index_reference=0;sprite_layer_reference(1);background_reference();sprite_layer_reference(0);text_layer_reference();}
