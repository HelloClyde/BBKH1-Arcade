/* PGM H1 adapter. GPL-2.0-or-later.
 * Bus/graphics formats follow the pinned FBA-A320 PGM driver (see NOTICE).
 * Graphics/sample ROMs remain in a checked file cache rather than a 64 MiB
 * resident/expanded allocation. No host emulator is used for validation. */
#include "pgm.h"
#include "h1_sdk.h"
#include "pgm_format.h"
#include "import_zip.h"
#include "pgm_sound.h"
#include "h1_port.h"
#include "audio.h"
#include "arcade_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SLOTS 512u
#define WIDTH 448u
#define HEIGHT 224u
unsigned h1_pgm_active;
volatile struct h1_pgm_diagnostics h1_pgm_stats={0x314d4750,1};
static FILE *romfile;
static pgm_part parts[PGM_REGIONS];
static uint8_t *bios,*program,*ram,*bg,*tx,*rows,*vreg,*palette,*cache;
static uint16_t *pixels;
static uint32_t *page_crc[PGM_REGIONS],*crc_all;
static uint32_t tags[SLOTS],ages[SLOTS],age;
/* Reserve the complete bank selector range, including unconnected text
 * palette banks. They read as zero instead of indexing past the table. */
static uint16_t colors[4096],sprites[0x500];
static unsigned input[3],z80_work,sound_ready,audio_fraction;
static unsigned calval,calmask,calcmd,calcount;
extern unsigned char mame_z80mem[65536];
extern void z80_init(void),z80_reset(void *),z80_set_irq_callback(int(*)(int));
extern int z80_execute(int);
extern void cpu_z80_nmi(void);
extern void cpu_68k_interrupt(int),cpu_68k_reset(void);
extern int cpu_68k_run(uint32_t);
extern unsigned cpu_68k_getpc(void);
int nBurnSoundLen=134;
static int16_t sound_out[268];
int16_t *pBurnSoundOut=sound_out;
static unsigned word(const void *p){const uint8_t *b=p;return b[0]|b[1]<<8;}
static void store(void *p,unsigned v){uint8_t *b=p;b[0]=v;b[1]=v>>8;}
static int read_at(unsigned off,void *p,unsigned n){return !fseek(romfile,off,SEEK_SET)&&fread(p,1,n,romfile)==n;}
static int sound_irq_ack(int line){(void)line;return 255;}
static uint8_t *page(unsigned region,unsigned offset){
 if(region>=PGM_REGIONS||offset>=parts[region].size)h1_arcade_escape(143);
 unsigned index=offset/4096,key=(region<<24)|index,base=((index*13+region*61)&127)*4,slot=base;
 for(unsigned i=base;i<base+4;i++){
  if(tags[i]==key){ages[i]=++age;++h1_pgm_stats.hits;return cache+i*4096;}
  if(tags[i]==~0u||ages[i]<ages[slot])slot=i;
 }
 uint8_t *p=cache+slot*4096;
 if(!read_at(parts[region].offset+index*4096,p,4096)||~h1_import_crc(~0u,p,4096)!=page_crc[region][index]){
  h1_arcade_log("PGM_CACHE_ERROR region=%u page=%u",region,index);h1_arcade_escape(142);}
 tags[slot]=key;ages[slot]=++age;++h1_pgm_stats.misses;return p;
}
static unsigned rom8(unsigned region,unsigned at){if(at>=parts[region].size)return 255;return page(region,at)[at&4095];}
static unsigned rom16(unsigned region,unsigned at){if(at+1>=parts[region].size)return 65535;if((at&4095)!=4095)return word(page(region,at)+(at&4095));return rom8(region,at)|(rom8(region,at+1)<<8);}
static void rom_copy(unsigned region,unsigned at,uint8_t *dst,unsigned n){
 while(n){unsigned take=4096-(at&4095);if(take>n)take=n;
  memcpy(dst,page(region,at)+(at&4095),take);at+=take;dst+=take;n-=take;}
}
typedef struct {unsigned region,pos,key,slot;uint8_t *data;} rom_cursor;
static inline unsigned next_word(rom_cursor *s){
 unsigned at=s->pos;s->pos+=2;
 if(at+1>=parts[s->region].size)return 65535;
 if((at&4095)==4095)return rom16(s->region,at);
 unsigned key=(s->region<<24)|(at>>12);
 /* The other stream may replace this slot. Validate its tag before every
  * use; a saved pointer alone would silently read a different ROM page. */
 if(!s->data||s->key!=key||tags[s->slot]!=key){
  s->data=page(s->region,at);s->slot=(unsigned)(s->data-cache)/4096;s->key=key;
 }
 return word(s->data+(at&4095));
}
unsigned char h1_pgm_sample(unsigned at){return rom8(PGM_SOUND,at&0x7fffff);}
static int load_region(unsigned region,uint8_t **p){
 unsigned size=parts[region].size;*p=malloc(size);if(!*p||!read_at(parts[region].offset,*p,size))return 0;
 return ~h1_import_crc(~0u,*p,size)==parts[region].crc;
}
int h1_pgm_load(const char *path){
 uint8_t h[256];h1_pgm_active=1;h1_pgm_stats.state=1;
 romfile=fopen(path,"rb");if(!romfile||fseek(romfile,0,SEEK_END))return 0;long size=ftell(romfile);
 if(size<256||!read_at(0,h,256)||!h1_pgm_header(h,size,"kov"))return 0;
 unsigned count=0;for(unsigned i=0;i<PGM_REGIONS;i++){parts[i]=(pgm_part){pgm_u32(h+64+i*16),pgm_u32(h+68+i*16),pgm_u32(h+72+i*16),pgm_u32(h+76+i*16)};count+=parts[i].size/4096;}
 crc_all=malloc(count*4);if(!crc_all||!read_at(parts[0].pages,crc_all,count*4))return 0;
 unsigned at=0;for(unsigned i=0;i<PGM_REGIONS;i++){page_crc[i]=crc_all+at;at+=parts[i].size/4096;}
 if(!load_region(PGM_BIOS,&bios)||!load_region(PGM_PROGRAM,&program))return 0;
 ram=calloc(1,0x20000);bg=calloc(1,0x4000);tx=calloc(1,0x2000);rows=calloc(1,0x800);vreg=calloc(1,0x10000);palette=calloc(1,0x1200);
 pixels=calloc(WIDTH*HEIGHT,2);cache=malloc(SLOTS*4096);
 if(!ram||!bg||!tx||!rows||!vreg||!palette||!pixels||!cache)return 0;
 memset(tags,255,sizeof tags);memset(ages,0,sizeof ages);memset(colors,0,sizeof colors);memset(sprites,0,sizeof sprites);age=0;
 /* FBA stores 68000 words in host order. Require an unambiguous PGM BIOS
  * vector pair; never guess and send a malformed PC to native assembly. */
 unsigned sp=word(bios)<<16|word(bios+2),pc=word(bios+4)<<16|word(bios+6);
 if(sp<0x800000||sp>0x900000||pc<8||pc>=0x20000||(pc&1)){
  for(unsigned i=0;i<parts[PGM_BIOS].size;i+=2){unsigned b=bios[i];bios[i]=bios[i+1];bios[i+1]=b;}
  sp=word(bios)<<16|word(bios+2);pc=word(bios+4)<<16|word(bios+6);
  if(sp<0x800000||sp>0x900000||pc<8||pc>=0x20000||(pc&1))return 0;
  for(unsigned i=0;i<parts[PGM_PROGRAM].size;i+=2){unsigned b=program[i];program[i]=program[i+1];program[i+1]=b;}
 }
 h1_pgm_decrypt(program,parts[PGM_PROGRAM].size);h1_pgm_prot_reset();
 memset(mame_z80mem,0,65536);z80_init();z80_reset(0);z80_set_irq_callback(sound_irq_ack);z80_work=0;
 if(ics2115_init()){ics2115_exit();return 0;}sound_ready=1;ics2115_reset();
 input[0]=input[1]=input[2]=65535;calval=calcmd=calcount=0;calmask=1;audio_fraction=0;
 h1_arcade_stats.cache_bytes=SLOTS*4096;h1_arcade_log("PGM_LOAD bios_sp=%08x bios_pc=%08x program=%u cache=%u sound=8000",sp,pc,parts[PGM_PROGRAM].size,SLOTS*4096);
 h1_pgm_stats.state=2;return 1;
}
void h1_pgm_close(void){
 if(sound_ready){ics2115_exit();sound_ready=0;}
 if(romfile){fclose(romfile);romfile=0;}
 free(bios);free(program);free(ram);free(bg);free(tx);free(rows);free(vreg);free(palette);free(pixels);free(cache);free(crc_all);
 bios=program=ram=bg=tx=rows=vreg=palette=cache=0;pixels=0;crc_all=0;h1_pgm_active=0;h1_pgm_stats.state=4;
}
static uint8_t *mapped(unsigned a){
 if(a<0x20000)return bios+a;
 if(a>=0x100000&&a<0x4f0000)return program+a-0x100000;
 if(a>=0x800000&&a<0x900000)return ram+(a&0x1ffff);
 if(a>=0x900000&&a<0x904000)return bg+(a-0x900000);
 if(a>=0x904000&&a<0x906000)return tx+(a-0x904000);
 if(a>=0x907000&&a<0x907800)return rows+(a-0x907000);
 if(a>=0xa00000&&a<0xa01200)return palette+(a-0xa00000);
 if(a>=0xb00000&&a<0xb10000)return vreg+(a-0xb00000);return 0;
}
uint8_t *h1_pgm_code(unsigned a){a&=0xffffff;if(a&1)return 0;
 if(a<0x20000|| (a>=0x100000&&a<0x4f0000)||(a>=0x800000&&a<0x900000))return mapped(a);return 0;}
static void palette_write(unsigned a,unsigned v){
 unsigned r=(v>>10)&31,g=(v>>5)&31,b=v&31;colors[(a-0xa00000)/2]=(r<<11)|(g<<6)|((g>>4)<<5)|b;
}
unsigned h1_pgm_read16(unsigned a){
 a&=0xfffffe;uint8_t *p=mapped(a);if(p)return word(p);
 if((a&0xff0000)==0xc10000)return (mame_z80mem[a&65535]<<8)|mame_z80mem[(a+1)&65535];
 if((a&0xff0000)==0x4f0000)return (a&65535)==8?1:0; /* China region */
 if(a==0x500000||a==0x500002)return h1_pgm_prot_read(a&2);
 switch(a){
 case 0xc00004:return ics2115_soundlatch_r(1);
 case 0xc00006:{unsigned v=(calval&calmask)!=0;calmask<<=1;return v;}
 case 0xc08000:return input[0];case 0xc08002:return 65535;case 0xc08004:return input[1];case 0xc08006:return 65535;
 }
 ++h1_pgm_stats.unknown_reads;return 0;
}
unsigned h1_pgm_read8(unsigned a){
 a&=0xffffff;uint8_t *p=mapped(a&~1u);if(p)return p[(a&1)^1];
 if((a&0xff0000)==0xc10000)return mame_z80mem[a&65535];
 return (h1_pgm_read16(a&~1u)>>((a&1)?0:8))&255;
}
void h1_pgm_write16(unsigned a,unsigned v){
 a&=0xfffffe;v&=65535;uint8_t *p=mapped(a);
 if(p){if(a<0x800000)return;store(p,v);if(a>=0xa00000&&a<0xa01200)palette_write(a,v);return;}
 if((a&0xff0000)==0xc10000){mame_z80mem[a&65535]=v>>8;mame_z80mem[(a+1)&65535]=v;return;}
 switch(a){
 case 0x500000:case 0x500002:h1_pgm_prot_write(a&2,v);return;
 case 0x500004:case 0x700006:case 0xc0000a:case 0xc08006:return;
 case 0xc00002:ics2115_soundlatch_w(0,v);if(z80_work)cpu_z80_nmi();return;
 case 0xc00004:ics2115_soundlatch_w(1,v);return;
 case 0xc0000c:ics2115_soundlatch_w(2,v);return;
 case 0xc00008:z80_work=v==0x5050;if(z80_work){ics2115_reset();z80_reset(0);z80_set_irq_callback(sound_irq_ack);}return;
 case 0xc00006:
  calcmd=(calcmd<<1)|(v&1);if(++calcount==4){static const uint8_t date[16]={3,2,0x12,2,0,2,0x10,2,0,2,0x21,2,0,2,0x26,1};calval=date[calcmd&15];calmask=1;calcount=0;}return;
 }
 ++h1_pgm_stats.unknown_writes;
}
void h1_pgm_write8(unsigned a,unsigned v){
 a&=0xffffff;uint8_t *p=mapped(a&~1u);
 if(p&&a>=0x800000){p[(a&1)^1]=v;if(a>=0xa00000&&a<0xa01200)palette_write(a&~1u,word(p));return;}
 if((a&0xff0000)==0xc10000){mame_z80mem[a&65535]=v;return;}
 /* Byte lane writes to I/O are deliberately not converted into reads of
  * side-effect registers. The PGM interface consumes the low data lane. */
 if(a&1)h1_pgm_write16(a&~1u,v);
}
unsigned char h1_pgm_z80_read(unsigned short p){switch(p>>8){case 0x80:return ics2115read(p&255);case 0x81:return ics2115_soundlatch_r(2);case 0x82:return ics2115_soundlatch_r(0);case 0x84:return ics2115_soundlatch_r(1);}return 0;}
void h1_pgm_z80_write(unsigned short p,unsigned char v){switch(p>>8){case 0x80:ics2115write(p&255,v);break;case 0x81:ics2115_soundlatch_w(2,v);break;case 0x82:ics2115_soundlatch_w(0,v);break;case 0x84:ics2115_soundlatch_w(1,v);break;}}
void h1_pgm_inputs(unsigned buttons){
 unsigned j=0,b=0;if(buttons&(1u<<ARCADE_START))j|=1;
 for(unsigned i=0;i<4;i++)if(buttons&(1u<<i))j|=2u<<i;
 for(unsigned i=0;i<3;i++)if(buttons&(1u<<(ARCADE_A+i)))j|=32u<<i;
 if(buttons&(1u<<ARCADE_COIN))b|=1;if(buttons&(1u<<ARCADE_D))b|=256;
 input[0]=65535^j;input[1]=65535^b;h1_pgm_stats.keys=buttons;
}
/* Drawing lives below; every cache access validates a page before use. */
static void draw(void);
void h1_pgm_frame(unsigned render){
 static int debt68,debt80;
 for(unsigned i=0;i<100;i++){
  debt68=cpu_68k_run(3333-debt68);
  if(z80_work)debt80=z80_execute(1411-debt80)-(1411-debt80);
 }
 cpu_68k_interrupt(6);ics2115_frame();
 audio_fraction+=8000;nBurnSoundLen=audio_fraction/60;audio_fraction%=60;ics2115_update(nBurnSoundLen);
 if(h1_arcade_settings.muted)memset(sound_out,0,nBurnSoundLen*4);gba_audio_write(sound_out,nBurnSoundLen);
 h1_arcade_stats.audio_samples+=nBurnSoundLen;
 if(render){draw();++h1_pgm_stats.draws;}
 memcpy(sprites,ram,sizeof sprites);
 ++h1_pgm_stats.frames;h1_pgm_stats.pc=cpu_68k_getpc();
 h1_arcade_stats.cache_hits=h1_pgm_stats.hits;h1_arcade_stats.cache_misses=h1_pgm_stats.misses;
 if(!h1_pgm_stats.game_started&&h1_pgm_stats.pc>=0x100000&&h1_pgm_stats.pc<0x500000){h1_pgm_stats.game_started=h1_pgm_stats.frames;h1_arcade_log("PGM_CARTRIDGE frame=%u pc=%08x",h1_pgm_stats.frames,h1_pgm_stats.pc);h1_arcade_log_flush();}
}
/* Decode individual 5-bit background rows, and 4-bit text nibbles, on demand.
 * This avoids allocating the upstream 19.2 MiB expanded tile region. */
static void background(void){
 unsigned sy=word(vreg+0x2000),sx=word(vreg+0x3000);
 for(unsigned y=0;y<224;y++){
  unsigned yy=(y+sy)&2047,row=yy&31,offset=(sx+word(rows+y*2))&2047;
  for(unsigned x=0;x<448;){
   unsigned xx=(x+offset)&2047,at=((yy>>5)*64+(xx>>5))*4;
   unsigned tile=word(bg+at),attr=word(bg+at+2);if(tile>0x7ff)tile+=0x1000;
   unsigned rx=xx&31,n=32-rx;if(n>448-x)n=448-x;
   unsigned ry=(attr&0x80)?31-row:row,base=tile*640+ry*20,pal=0x400+((attr&0x3e)<<4);
   if(base+20<=parts[PGM_TILES].size){uint8_t raw[20];rom_copy(PGM_TILES,base,raw,20);
    for(unsigned k=0;k<n;k++){unsigned px=(attr&0x40)?31-(rx+k):rx+k,bit=px*5,b=bit>>3,s=bit&7;
     unsigned v=(raw[b]>>s)|((b+1<20?raw[b+1]:0)<<(8-s));v&=31;if(v!=31)pixels[y*448+x+k]=colors[pal+v];}}
   x+=n;
  }
 }
}
static void text_layer(void){
 unsigned sy=word(vreg+0x5000),sx=word(vreg+0x6000);
 for(unsigned y=0;y<224;y++)for(unsigned x=0;x<448;){
  unsigned xx=(x+sx)&511,yy=(y+sy)&255,at=((yy>>3)*64+(xx>>3))*4;
  unsigned tile=word(tx+at),attr=word(tx+at+2),rx=xx&7,n=8-rx;if(n>448-x)n=448-x;
  if(tile>0xbfff)tile=tile-0xc000+0x20000;
  unsigned ry=(attr&0x80)?7-(yy&7):yy&7,base=tile*32+ry*4,pal=0x800+((attr&0x3e)<<3);
  if(base+4<=parts[PGM_TILES].size){uint32_t raw=pgm_u32(page(PGM_TILES,base)+(base&4095));
   for(unsigned k=0;k<n;k++){unsigned px=(attr&0x40)?7-(rx+k):rx+k,v=(raw>>(px*4))&15;if(v!=15)pixels[y*448+x+k]=colors[pal+v];}}
  x+=n;
 }
}
static unsigned sprite_index;
static unsigned zoom_extent(unsigned n,unsigned mask,unsigned grow){
 unsigned count=0;for(unsigned i=0;i<n;i++)count+=(mask>>(i&31))&1;
 return grow?n+count:n-count;
}
static void sprite_layer(unsigned priority){
 for(;sprite_index<0x500;sprite_index+=5){
  const uint16_t *s=sprites+sprite_index;unsigned high=s[4]&511,wide=(s[4]>>9)&63,attr=s[2];
  if(!high||(priority&&!(attr&0x80)))break;
  int xpos=s[0]&2047,ypos=s[1]&1023;if(xpos>1023)xpos-=2048;if(ypos>511)ypos-=1024;
  unsigned boff=(((attr&127)<<16)|s[3])*2;
  if(!wide||boff+4>parts[PGM_MASK].size)continue;
  rom_cursor masks={.region=PGM_MASK,.pos=boff};
  unsigned low=next_word(&masks),color=(low|(next_word(&masks)<<16))>>1;
  rom_cursor data={.region=PGM_COLOR,.pos=color};
  unsigned bits=0,remain=0,pal=(attr>>8&31)*32,flipx=attr&0x2000,flipy=attr&0x4000;
  unsigned zx=(s[0]>>11)&15,zy=(s[1]>>11)&15,gx=s[0]&0x8000,gy=s[1]&0x8000;
  if(gx)zx=16-zx;if(gy)zy=16-zy;
  unsigned xzoom=word(vreg+0x1000+zx*4)<<16|word(vreg+0x1002+zx*4),yzoom=word(vreg+0x1000+zy*4)<<16|word(vreg+0x1002+zy*4);
  uint8_t line[1008];unsigned desty=0,fullw=wide*16;
  unsigned outw=zoom_extent(fullw,xzoom,gx),outh=zoom_extent(high,yzoom,gy);
  if(!outw||!outh||xpos>=448||xpos+(int)outw<=0||ypos>=224||ypos+(int)outh<=0)continue;
  for(unsigned y=0;y<high;y++){
   if((!flipy&&ypos+(int)desty>=224)||(flipy&&ypos+(int)outh-1-(int)desty<0))break;
   for(unsigned block=0;block<wide;block++){
    unsigned mask=next_word(&masks);
    for(unsigned k=0;k<16;k++){unsigned v=255;if(!(mask&(1u<<k))){if(!remain){bits=next_word(&data);remain=3;}v=bits&31;bits>>=5;--remain;}line[block*16+k]=v;}
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
static void draw(void){for(unsigned i=0;i<448*224;i++)pixels[i]=colors[0x3ff];sprite_index=0;sprite_layer(1);background();sprite_layer(0);text_layer();}
#include "pgm_draw_reference.h"
volatile struct {unsigned magic,version,request,state,tests,differences,reference_crc,fast_crc,reference_ticks,fast_ticks;} h1_pgm_draw_probe={0x31444750,1};
void h1_pgm_pause_probe(void){
 if(!h1_pgm_draw_probe.request)return;
 h1_pgm_draw_probe.request=0;h1_pgm_draw_probe.state=1;
 uint16_t *temporary=malloc(448*224*2),*saved=pixels;
 if(!temporary){h1_pgm_draw_probe.state=4;return;}
 unsigned start=h1_raw_tick_80hz();pixels=temporary;draw_reference();h1_pgm_draw_probe.reference_ticks=h1_raw_tick_80hz()-start;
 pixels=saved;start=h1_raw_tick_80hz();draw();h1_pgm_draw_probe.fast_ticks=h1_raw_tick_80hz()-start;
 unsigned n=0;for(unsigned i=0;i<448*224;i++)n+=temporary[i]!=saved[i];
 h1_pgm_draw_probe.reference_crc=~h1_import_crc(~0u,temporary,448*224*2);
 h1_pgm_draw_probe.fast_crc=~h1_import_crc(~0u,saved,448*224*2);
 h1_pgm_draw_probe.differences=n;++h1_pgm_draw_probe.tests;free(temporary);
 h1_pgm_draw_probe.state=n?3:2;
}
static unsigned expand(unsigned c){unsigned r=(c>>11)&31,g=(c>>5)&63,b=c&31;return ((r<<3|r>>2)<<16)|((g<<2|g>>4)<<8)|(b<<3|b>>2);}
void h1_pgm_present(uint32_t *lcd,unsigned mode){
 unsigned w=mode==0?362:mode==1?448:480,h=mode==1?224:272,left=(480-w)/2,top=(272-h)/2;
 /* The full LCD is owned by the app. Clear margins on every draw so a mode
  * change or pause menu cannot leave old pixels at either edge. */
 for(unsigned y=0;y<272;y++)for(unsigned x=0;x<480;x++)lcd[y*480+x]=0;
 for(unsigned y=0;y<h;y++){const uint16_t *src=pixels+(y*224/h)*448;uint32_t *dst=lcd+(top+y)*480+left;for(unsigned x=0;x<w;x++)dst[x]=expand(src[x*448/w]);}
#if defined(__mips__)
 if(((uintptr_t)lcd&0xe0000000u)==0x80000000u){
  uintptr_t a=(uintptr_t)lcd&~15u,end=(uintptr_t)(lcd+480*272);
  __asm__ volatile("sync":::"memory");
  for(;a<end;a+=16)__asm__ volatile("cache 0x15,0(%0)"::"r"(a):"memory");
  __asm__ volatile("sync":::"memory");
 }
#endif
}
