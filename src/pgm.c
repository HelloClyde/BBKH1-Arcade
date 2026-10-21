/* PGM H1 adapter. GPL-2.0-or-later.
 * Bus/graphics formats follow the pinned FBA-A320 PGM driver (see NOTICE).
 * Graphics/sample ROMs remain in a bounded file cache rather than a 64 MiB
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
static uint32_t *pixels;
static unsigned pgm_stream_ready;
extern volatile unsigned h1_pgm_stream_mode;
static int pgm_stream_prepare(void);
static void pgm_stream_reset(void);
static void pgm_stream_close(void);
static uint32_t *bg_surface;
static unsigned bg_epoch=1,bg_cached_epoch,bg_opaque;
/* Whole RGB backgrounds increase SDRAM traffic on this target; keep that
 * measured experiment available for A/B, default to the tile-span path. */
volatile unsigned h1_pgm_layers_mode=1,h1_pgm_present_mode=1;
/* 1: complete unrolled fill, 2: initialize only background holes. */
volatile unsigned h1_pgm_clear_mode=2;
static uint16_t *reference_pixels,*colors565;
/* File format still accepts existing H1PGM1 containers. Per-page and loaded
 * region CRCs are intentionally not checked during playback. */
uint8_t **h1_pgm_bus_pages;
static uint8_t *tile_pixels,*sprite_pixels,*text_pixels;
static uint32_t *tile_tags,*text_tags;
/* Two 32-bit row masks live after the decoded tile allocation: nonempty
 * and wholly opaque. They share the decoded cache's direct-mapped tag. */
static uint32_t *tile_row_masks;
typedef struct {unsigned mask,width,height,offset;} decoded_sprite;
static decoded_sprite *sprite_tags;
static unsigned sprite_capacity,sprite_used;
#define TILE_SLOTS 1024u
#define SPRITE_SLOTS 512u
/* Version 2 separates descriptor/zoom setup, cache-miss decode, row
 * composition and text. The inner transparency test and palette lookup stay
 * together: timing each pixel would measure the instrumentation itself. */
volatile struct {unsigned magic,version,enabled,frames,ticks[15],tile_hits,tile_misses,sprite_hits,sprite_misses,mxu_pixels,decoded_bytes;
 unsigned identity_rows,mapped_rows,identity_pixels,mapped_pixels;} h1_pgm_perf={0x32504750,3};
/* Debug A/B switch: original mapped C, identity-aware C, MIPS rows, MXU. */
volatile unsigned h1_pgm_sprite_mode=H1_MXU?3:2;
#if defined(__mips__)
extern unsigned h1_pgm_sprite_row(uint32_t *,const uint8_t *,const uint32_t *,unsigned);
extern unsigned h1_pgm_sprite_rev(uint32_t *,const uint8_t *,const uint32_t *,unsigned);
extern unsigned h1_pgm_sprite_map(uint32_t *,const uint8_t *,const uint32_t *,const uint16_t *,unsigned);
#if H1_MXU
extern unsigned h1_pgm_sprite_mxu_row(uint32_t *,const uint8_t *,const uint32_t *,unsigned);
extern unsigned h1_pgm_sprite_mxu_rev(uint32_t *,const uint8_t *,const uint32_t *,unsigned);
extern unsigned h1_pgm_sprite_mxu_map(uint32_t *,const uint8_t *,const uint32_t *,const uint16_t *,unsigned);
#endif
#endif
static unsigned perf_phase,perf_last;
static void phase(unsigned next){if(h1_pgm_perf.enabled){unsigned now=h1_raw_tick_80hz();if(perf_last)h1_pgm_perf.ticks[perf_phase]+=now-perf_last;perf_last=now;perf_phase=next;}else perf_last=0;}
void h1_pgm_profile_phase(unsigned next){phase(next);}
static uint32_t tags[SLOTS],ages[SLOTS],age;
/* Reserve the complete bank selector range, including unconnected text
 * palette banks. They read as zero instead of indexing past the table. */
static uint32_t *colors;
static uint16_t sprites[0x500];
static unsigned expand(unsigned);
static unsigned input[3],z80_work,sound_ready,audio_fraction;
/* Match pinned FBA PGM_INTER_LEAVE=2. Keep a runtime A/B control while
 * preserving the same 20 MHz / 8.468 MHz cycle budget at every setting. */
volatile unsigned h1_pgm_interleave=2;
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
 unsigned previous=perf_phase;phase(7);
 if(!read_at(parts[region].offset+index*4096,p,4096)){
  h1_arcade_log("PGM_CACHE_ERROR region=%u page=%u",region,index);h1_arcade_escape(142);}
 phase(previous);
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
static uint8_t *sample_data;static unsigned sample_key,sample_slot;
unsigned char h1_pgm_sample(unsigned at){
 at&=0x7fffff;unsigned key=(PGM_SOUND<<24)|(at>>12);
 if(!sample_data||sample_key!=key||tags[sample_slot]!=key){sample_data=page(PGM_SOUND,at);sample_slot=(unsigned)(sample_data-cache)/4096;sample_key=key;}
 return sample_data[at&4095];
}
static int load_region(unsigned region,uint8_t **p){
 unsigned size=parts[region].size;*p=malloc(size);if(!*p||!read_at(parts[region].offset,*p,size))return 0;
 return 1;
}
int h1_pgm_load(const char *path){
 h1_pgm_present_reset();
 uint8_t h[256];h1_pgm_active=1;h1_pgm_stats.state=1;
 romfile=fopen(path,"rb");if(!romfile||fseek(romfile,0,SEEK_END))return 0;long size=ftell(romfile);
 if(size<256||!read_at(0,h,256)||!h1_pgm_header(h,size,"kov"))return 0;
 for(unsigned i=0;i<PGM_REGIONS;i++)parts[i]=(pgm_part){pgm_u32(h+64+i*16),pgm_u32(h+68+i*16),pgm_u32(h+72+i*16),pgm_u32(h+76+i*16)};
 if(!load_region(PGM_BIOS,&bios)||!load_region(PGM_PROGRAM,&program))return 0;
 ram=calloc(1,0x20000);bg=calloc(1,0x4000);tx=calloc(1,0x2000);rows=calloc(1,0x800);vreg=calloc(1,0x10000);palette=calloc(1,0x1200);
 pixels=calloc(WIDTH*HEIGHT,4);cache=malloc(SLOTS*4096);
 colors=calloc(4096,4);colors565=calloc(4096,2);
 if(!ram||!bg||!tx||!rows||!vreg||!palette||!pixels||!cache||!colors||!colors565)return 0;
 h1_pgm_bus_pages=calloc(256,sizeof *h1_pgm_bus_pages);
 tile_pixels=malloc(TILE_SLOTS*(1024+8));tile_tags=malloc(TILE_SLOTS*4);
 tile_row_masks=tile_pixels?(uint32_t*)(tile_pixels+TILE_SLOTS*1024):0;
 text_pixels=malloc(512*72);text_tags=malloc(512*4);
 sprite_tags=malloc(SPRITE_SLOTS*sizeof *sprite_tags);
 sprite_capacity=8*1024*1024;
 while(sprite_capacity>=1024*1024&&!(sprite_pixels=malloc(sprite_capacity)))sprite_capacity/=2;
 if(!h1_pgm_bus_pages||!tile_pixels||!tile_tags||!text_pixels||!text_tags||!sprite_tags||!sprite_pixels)return 0;
 for(unsigned i=0;i<2;i++)h1_pgm_bus_pages[i]=bios+i*65536;
 for(unsigned i=0x10;i<0x4f;i++)h1_pgm_bus_pages[i]=program+(i-0x10)*65536;
 for(unsigned i=0x80;i<0x90;i++)h1_pgm_bus_pages[i]=ram+(i&1)*65536;
 memset(tile_tags,255,TILE_SLOTS*4);memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);sprite_used=0;
 memset(text_tags,255,512*4);h1_pgm_perf.decoded_bytes=TILE_SLOTS*1024+sprite_capacity+512*72;
 memset(tags,255,sizeof tags);memset(ages,0,sizeof ages);memset(sprites,0,sizeof sprites);age=0;
 sample_data=0;
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
 h1_pgm_present_reset();
 pgm_stream_close();
 free(bg_surface);bg_surface=0;bg_cached_epoch=0;++bg_epoch;
 if(sound_ready){ics2115_exit();sound_ready=0;}
 if(romfile){fclose(romfile);romfile=0;}
 free(bios);free(program);free(ram);free(bg);free(tx);free(rows);free(vreg);free(palette);free(pixels);free(cache);
 free(h1_pgm_bus_pages);h1_pgm_bus_pages=0;free(tile_pixels);free(tile_tags);free(sprite_pixels);free(sprite_tags);
 free(colors);colors=0;free(colors565);colors565=0;
 free(text_pixels);text_pixels=0;free(text_tags);text_tags=0;
 tile_pixels=sprite_pixels=0;tile_row_masks=0;tile_tags=0;sprite_tags=0;
 sample_data=0;
 bios=program=ram=bg=tx=rows=vreg=palette=cache=0;pixels=0;h1_pgm_active=0;h1_pgm_stats.state=4;
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
 unsigned r=(v>>10)&31,g=(v>>5)&31,b=v&31,index=(a-0xa00000)/2,c=(r<<11)|(g<<6)|((g>>4)<<5)|b;
 colors565[index]=c;colors[index]=expand(c);
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
 if(p){if(a<0x800000)return;
  if(word(p)!=v&&((a>=0x900000&&a<0x904000)||(a>=0x907000&&a<0x907800)||(a>=0xa00800&&a<0xa01000)||a==0xb02000||a==0xb03000))++bg_epoch;
  store(p,v);if(a>=0xa00000&&a<0xa01200)palette_write(a,v);return;}
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
 if(p&&a>=0x800000){unsigned aligned=a&~1u;
  if(p[(a&1)^1]!=(uint8_t)v&&((a>=0x900000&&a<0x904000)||(a>=0x907000&&a<0x907800)||(a>=0xa00800&&a<0xa01000)||aligned==0xb02000||aligned==0xb03000))++bg_epoch;
  p[(a&1)^1]=v;if(a>=0xa00000&&a<0xa01200)palette_write(a&~1u,word(p));return;}
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
/* Phase ticks are an optional exclusive wall-time partition on physical H1:
 * other, 68K, Z80, sound, background, sprites/text, LCD, file I/O, PCM. */
static void draw(void);
void h1_pgm_frame(unsigned render){
 static int debt68,debt80;
 unsigned count=h1_pgm_interleave;if(count<2||count>100)count=2;
 unsigned done68=0,done80=0;
 for(unsigned i=0;i<count;i++){
  unsigned next68=(i+1)*(20000000u/60)/count,next80=(i+1)*(8468000u/60)/count;
  int budget68=(int)(next68-done68)-debt68,budget80=(int)(next80-done80)-debt80;
  phase(1);
  debt68=cpu_68k_run(budget68);
  phase(2);
  if(z80_work)debt80=z80_execute(budget80)-budget80;
  done68=next68;done80=next80;
 }
 phase(3);
 cpu_68k_interrupt(6);ics2115_frame();
 audio_fraction+=8000;nBurnSoundLen=audio_fraction/60;audio_fraction%=60;ics2115_update(nBurnSoundLen);
 if(h1_arcade_settings.muted)memset(sound_out,0,nBurnSoundLen*4);phase(8);gba_audio_write(sound_out,nBurnSoundLen);
 phase(0);
 h1_arcade_stats.audio_samples+=nBurnSoundLen;
 /* Prepare before the hardware's one-frame sprite latch is advanced below.
  * A stream plan snapshots geometry and pins decoded pointers until present. */
 if(render){if(!pgm_stream_prepare())draw();++h1_pgm_stats.draws;}
 memcpy(sprites,ram,sizeof sprites);
 ++h1_pgm_stats.frames;h1_pgm_stats.pc=cpu_68k_getpc();
 if(h1_pgm_perf.enabled)++h1_pgm_perf.frames;
 h1_arcade_stats.cache_hits=h1_pgm_stats.hits;h1_arcade_stats.cache_misses=h1_pgm_stats.misses;
 if(!h1_pgm_stats.game_started&&h1_pgm_stats.pc>=0x100000&&h1_pgm_stats.pc<0x500000){h1_pgm_stats.game_started=h1_pgm_stats.frames;h1_arcade_log("PGM_CARTRIDGE frame=%u pc=%08x",h1_pgm_stats.frames,h1_pgm_stats.pc);h1_arcade_log_flush();}
}
/* Cache palette indices, independent of palette writes, scroll, zoom and
 * flipping. ROM contents do not change during a loaded game. */
static const uint8_t *tile_data(unsigned tile){
 unsigned slot=tile&(TILE_SLOTS-1);uint8_t *dst=tile_pixels+slot*1024;
 if(tile_tags[slot]==tile){++h1_pgm_perf.tile_hits;return dst;}
 uint8_t raw[640];unsigned base=tile*640,bytes=parts[PGM_TILES].size-base;if(bytes>640)bytes=640;
 memset(raw,255,sizeof raw);rom_copy(PGM_TILES,base,raw,bytes);
 unsigned nonempty=0,opaque=0;
 for(unsigned y=0;y<32;y++){
  unsigned mask=0;
  for(unsigned x=0;x<32;x++){
   unsigned bit=x*5,b=bit>>3,s=bit&7;
   unsigned v=((raw[y*20+b]>>s)|((b+1<20?raw[y*20+b+1]:0)<<(8-s)))&31;
   dst[y*32+x]=v;if(v!=31)mask|=1u<<x;
  }
  if(mask)nonempty|=1u<<y;if(mask==~0u)opaque|=1u<<y;
 }
 if(tile_row_masks){tile_row_masks[slot*2]=nonempty;tile_row_masks[slot*2+1]=opaque;}
 tile_tags[slot]=tile;++h1_pgm_perf.tile_misses;return dst;
}
static void background(void){
 unsigned sy=word(vreg+0x2000),sx=word(vreg+0x3000);
 for(unsigned y=0;y<224;y++){
  unsigned yy=(y+sy)&2047,row=yy&31,offset=(sx+word(rows+y*2))&2047;
  for(unsigned x=0;x<448;){
   unsigned xx=(x+offset)&2047,at=((yy>>5)*64+(xx>>5))*4;
   unsigned tile=word(bg+at),attr=word(bg+at+2);if(tile>0x7ff)tile+=0x1000;
   unsigned rx=xx&31,n=32-rx;if(n>448-x)n=448-x;
   unsigned ry=(attr&0x80)?31-row:row,pal=0x400+((attr&0x3e)<<4);
   if(tile*640+ry*20+20<=parts[PGM_TILES].size){const uint8_t *src=tile_data(tile)+ry*32;uint32_t *dst=pixels+y*448+x;
    if(attr&0x40){for(unsigned k=0;k<n;k++){unsigned v=src[31-rx-k];if(v!=31)dst[k]=colors[pal+v];}}
    else for(unsigned k=0;k<n;k++){unsigned v=src[rx+k];if(v!=31)dst[k]=colors[pal+v];}}
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
  if(base+4<=parts[PGM_TILES].size){
   unsigned slot=tile&511;uint8_t *data=text_pixels+slot*72;
   if(text_tags[slot]!=tile){
    uint8_t packed[32];unsigned bytes=parts[PGM_TILES].size-tile*32;if(bytes>32)bytes=32;
    memset(packed,255,sizeof packed);rom_copy(PGM_TILES,tile*32,packed,bytes);
    for(unsigned yy=0;yy<8;yy++){unsigned mask=0;for(unsigned xx=0;xx<8;xx++){
     unsigned v=(packed[yy*4+xx/2]>>((xx&1)*4))&15;data[yy*8+xx]=v;if(v!=15)mask|=1u<<xx;
    }data[64+yy]=mask;}
    text_tags[slot]=tile;
   }
   unsigned ry=(attr&0x80)?7-(yy&7):yy&7;
   if(data[64+ry]){const uint8_t *src=data+ry*8;for(unsigned k=0;k<n;k++){unsigned px=(attr&0x40)?7-rx-k:rx+k,v=src[px];if(v!=15)pixels[y*448+x+k]=colors[pal+v];}}
  }
  x+=n;
 }
}
#include "pgm_layers.h"
/* Cache the composed background independently of sprites. Writes affecting
 * tiles, row scroll, scroll registers or its palette invalidate it. 0xffffffff
 * is a transparency marker outside the RGB32 color range. */
static int prepare_background(void){
 if(!bg_surface)bg_surface=malloc(WIDTH*HEIGHT*4);
 if(!bg_surface)return 0;
 if(bg_cached_epoch!=bg_epoch){
  phase(4);uint32_t *saved=pixels;pixels=bg_surface;
  for(unsigned i=0;i<WIDTH*HEIGHT;i++)pixels[i]=~0u;
  background_fast();pixels=saved;bg_opaque=1;
  for(unsigned i=0;i<WIDTH*HEIGHT;i++)if(bg_surface[i]==~0u){bg_opaque=0;break;}
  bg_cached_epoch=bg_epoch;
 }
 return 1;
}
static unsigned sprite_index;
static unsigned zoom_extent(unsigned n,unsigned mask,unsigned grow){
 unsigned count=0;for(unsigned i=0;i<n;i++)count+=(mask>>(i&31))&1;
 return grow?n+count:n-count;
}
static const uint8_t *sprite_data(unsigned boff,unsigned wide,unsigned high){
 unsigned slot=((boff>>2)^(wide*17)^(high*31))&(SPRITE_SLOTS-1);
 decoded_sprite *tag=sprite_tags+slot;
 if(tag->mask==boff&&tag->width==wide&&tag->height==high){++h1_pgm_perf.sprite_hits;return sprite_pixels+tag->offset;}
 phase(9);
 unsigned bytes=wide*16*high;
 if(sprite_used+bytes>sprite_capacity){sprite_used=0;memset(sprite_tags,255,SPRITE_SLOTS*sizeof *sprite_tags);}
 uint8_t *dst=sprite_pixels+sprite_used;
 rom_cursor masks={.region=PGM_MASK,.pos=boff};
 unsigned low=next_word(&masks),color=(low|(next_word(&masks)<<16))>>1;
 rom_cursor data={.region=PGM_COLOR,.pos=color};unsigned bits=0,remain=0;
 for(unsigned block=0;block<wide*high;block++){
  unsigned mask=next_word(&masks);
  if(mask==65535){memset(dst+block*16,255,16);continue;}
  for(unsigned k=0;k<16;k++){
   unsigned v=255;if(!(mask&(1u<<k))){if(!remain){bits=next_word(&data);remain=3;}v=bits&31;bits>>=5;--remain;}
   dst[block*16+k]=v;
  }
 }
 *tag=(decoded_sprite){boff,wide,high,sprite_used};sprite_used=(sprite_used+bytes+3)&~3u;
 ++h1_pgm_perf.sprite_misses;phase(5);return dst;
}
static void sprite_layer(unsigned priority){
 for(;sprite_index<0x500;sprite_index+=5){
  phase(5);
  const uint16_t *s=sprites+sprite_index;unsigned high=s[4]&511,wide=(s[4]>>9)&63,attr=s[2];
  if(!high||(priority&&!(attr&0x80)))break;
  int xpos=s[0]&2047,ypos=s[1]&1023;if(xpos>1023)xpos-=2048;if(ypos>511)ypos-=1024;
  unsigned boff=(((attr&127)<<16)|s[3])*2;
  if(!wide||boff+4>parts[PGM_MASK].size)continue;
  unsigned pal=(attr>>8&31)*32,flipx=attr&0x2000,flipy=attr&0x4000;
  unsigned zx=(s[0]>>11)&15,zy=(s[1]>>11)&15,gx=s[0]&0x8000,gy=s[1]&0x8000;
  if(gx)zx=16-zx;if(gy)zy=16-zy;
  unsigned xzoom=word(vreg+0x1000+zx*4)<<16|word(vreg+0x1002+zx*4),yzoom=word(vreg+0x1000+zy*4)<<16|word(vreg+0x1002+zy*4);
  unsigned desty=0,fullw=wide*16;
  unsigned outw=xzoom?zoom_extent(fullw,xzoom,gx):fullw,outh=yzoom?zoom_extent(high,yzoom,gy):high;
  if(!outw||!outh||xpos>=448||xpos+(int)outw<=0||ypos>=224||ypos+(int)outh<=0)continue;
  const uint8_t *decoded=sprite_data(boff,wide,high);
  int left=xpos<0?0:xpos,right=xpos+(int)outw;if(right>448)right=448;
  /* Compute horizontal zoom/flip/clipping once per sprite, not once per
   * pixel of every row. The mapped span is contiguous even when shrinking. */
  unsigned mode=h1_pgm_sprite_mode,identity=mode&&!xzoom;
  uint16_t columns[448];unsigned destx=0;
  if(!identity)for(unsigned x=0;x<fullw;x++){
   unsigned xz=(xzoom>>(x&31))&1,n=gx?1+xz:1-xz;
   for(unsigned k=0;k<n;k++,destx++){int dx=xpos+(flipx?(int)outw-1-(int)destx:(int)destx);if(dx>=left&&dx<right)columns[dx-left]=x;}
  }
  const uint32_t *pal32=colors+pal;unsigned width=right-left;
  phase(10);
  for(unsigned y=0;y<high;y++){
   if((!flipy&&ypos+(int)desty>=224)||(flipy&&ypos+(int)outh-1-(int)desty<0))break;
   unsigned yz=(yzoom>>(y&31))&1,repeats=gy?1+yz:1-yz;
   for(unsigned rep=0;rep<repeats;rep++,desty++){
    int dy=ypos+(flipy?(int)outh-1-(int)desty:(int)desty);if(dy<0||dy>=224)continue;
    const uint8_t *src=decoded+y*fullw;uint32_t *dst=pixels+dy*448+left;
    if(identity){
     src+=flipx?fullw-1-(left-xpos):left-xpos;
     ++h1_pgm_perf.identity_rows;h1_pgm_perf.identity_pixels+=width;
#if defined(__mips__) && H1_MXU
     if(mode==3){if(flipx)h1_pgm_sprite_mxu_rev(dst,src,pal32,width);else h1_pgm_sprite_mxu_row(dst,src,pal32,width);}else
#endif
#if defined(__mips__)
     if(mode==2){if(flipx)h1_pgm_sprite_rev(dst,src,pal32,width);else h1_pgm_sprite_row(dst,src,pal32,width);}else
#endif
     if(flipx){for(unsigned x=0;x<width;x++){unsigned v=src[-(int)x];if(v!=255)dst[x]=pal32[v];}}
     else for(unsigned x=0;x<width;x++){unsigned v=src[x];if(v!=255)dst[x]=pal32[v];}
    }else{
     ++h1_pgm_perf.mapped_rows;h1_pgm_perf.mapped_pixels+=width;
#if defined(__mips__) && H1_MXU
     if(mode==3)h1_pgm_sprite_mxu_map(dst,src,pal32,columns,width);else
#endif
#if defined(__mips__)
     if(mode==2)h1_pgm_sprite_map(dst,src,pal32,columns,width);else
#endif
     for(unsigned x=0;x<width;x++){unsigned v=src[columns[x]];if(v!=255)dst[x]=pal32[v];}
    }
   }
  }
 }
}
/* Palette and framebuffer are separate allocations. Snapshot the backdrop
 * before writing, so alias analysis cannot reload it for every pixel. Keep
 * sixteen consecutive stores per branch for the JZ4740's small in-order core. */
static void clear_pixels(void){
 uint32_t c=colors[0x3ff],*p=pixels,*end=pixels+WIDTH*HEIGHT;
 do{
  p[0]=c;p[1]=c;p[2]=c;p[3]=c;p[4]=c;p[5]=c;p[6]=c;p[7]=c;
  p[8]=c;p[9]=c;p[10]=c;p[11]=c;p[12]=c;p[13]=c;p[14]=c;p[15]=c;p+=16;
 }while(p!=end);
}
#include "pgm_clear.h"
static void draw(void){
 pgm_stream_ready=0;
 unsigned mode=h1_pgm_layers_mode,cached=mode==2&&prepare_background();sprite_index=0;
 if(cached&&bg_opaque){
  /* All low-priority sprites are covered by an opaque background. Advance
   * the list exactly as sprite_layer(1) without decoding invisible pixels. */
  while(sprite_index<0x500&&(sprites[sprite_index+4]&511)&&(sprites[sprite_index+2]&0x80))sprite_index+=5;
  phase(4);memcpy(pixels,bg_surface,WIDTH*HEIGHT*4);
 }else{
  phase(13);if(h1_pgm_clear_mode==2&&mode==1)background_clear_fast(colors[0x3ff]);else clear_pixels();
  phase(5);sprite_layer(1);phase(4);
  if(cached){for(unsigned i=0;i<WIDTH*HEIGHT;i++){unsigned c=bg_surface[i];if(c!=~0u)pixels[i]=c;}}
  else if(mode)background_fast();else background();
 }
 phase(5);sprite_layer(0);phase(11);if(mode)text_layer_fast();else text_layer();phase(0);
}
#define pixels reference_pixels
#define colors colors565
#include "pgm_draw_reference.h"
#undef colors
#undef pixels
volatile struct {unsigned magic,version,request,state,tests,differences,reference_crc,fast_crc,reference_ticks,fast_ticks,present_differences[3];} h1_pgm_draw_probe={0x31444750,2};
volatile struct {unsigned magic,version,request,state,loops,ticks[4],differences[4];} h1_pgm_sprite_bench={0x31424750,2};
static void sprite_benchmark(void){
 unsigned loops=h1_pgm_sprite_bench.request;if(!loops)return;
 h1_pgm_sprite_bench.request=0;h1_pgm_sprite_bench.state=1;
 if(loops>128)loops=128;
 uint32_t *expected=malloc(WIDTH*HEIGHT*4);
 if(!expected){h1_pgm_sprite_bench.state=4;return;}
 unsigned mode=h1_pgm_sprite_mode,enabled=h1_pgm_perf.enabled;h1_pgm_perf.enabled=0;
 h1_pgm_sprite_mode=0;draw();memcpy(expected,pixels,WIDTH*HEIGHT*4);
 for(unsigned m=0;m<4;m++){
  h1_pgm_sprite_mode=m;draw();unsigned start=h1_raw_tick_80hz();
  for(unsigned i=0;i<loops;i++)draw();
  h1_pgm_sprite_bench.ticks[m]=h1_raw_tick_80hz()-start;
  unsigned differences=0;for(unsigned i=0;i<WIDTH*HEIGHT;i++)differences+=expected[i]!=pixels[i];
  h1_pgm_sprite_bench.differences[m]=differences;
 }
 h1_pgm_sprite_mode=mode;h1_pgm_perf.enabled=enabled;perf_last=0;
 h1_pgm_sprite_bench.loops=loops;free(expected);
 h1_pgm_sprite_bench.state=h1_pgm_sprite_bench.differences[0]||h1_pgm_sprite_bench.differences[1]||h1_pgm_sprite_bench.differences[2]||h1_pgm_sprite_bench.differences[3]?3:2;
}
static void present_check(void);
static void pipeline_benchmark(void);
static void stream_benchmark(void);
void h1_pgm_pause_probe(void){
 sprite_benchmark();
 pipeline_benchmark();
 stream_benchmark();
 if(!h1_pgm_draw_probe.request)return;
 h1_pgm_draw_probe.request=0;h1_pgm_draw_probe.state=1;
 uint16_t *temporary=malloc(448*224*2),*actual565=malloc(448*224*2);
 if(!temporary||!actual565){free(temporary);free(actual565);h1_pgm_draw_probe.state=4;return;}
 unsigned start=h1_raw_tick_80hz();reference_pixels=temporary;draw_reference();h1_pgm_draw_probe.reference_ticks=h1_raw_tick_80hz()-start;
 start=h1_raw_tick_80hz();draw();h1_pgm_draw_probe.fast_ticks=h1_raw_tick_80hz()-start;
 unsigned n=0;for(unsigned i=0;i<448*224;i++){unsigned c=pixels[i];n+=expand(temporary[i])!=c;actual565[i]=((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31);}
 h1_pgm_draw_probe.reference_crc=~h1_import_crc(~0u,temporary,448*224*2);
 h1_pgm_draw_probe.fast_crc=~h1_import_crc(~0u,actual565,448*224*2);
 h1_pgm_draw_probe.differences=n;++h1_pgm_draw_probe.tests;free(temporary);free(actual565);reference_pixels=0;
 present_check();for(unsigned i=0;i<3;i++)n+=h1_pgm_draw_probe.present_differences[i];
 h1_pgm_draw_probe.state=n?3:2;
}
static unsigned expand(unsigned c){unsigned r=(c>>11)&31,g=(c>>5)&63,b=c&31;return ((r<<3|r>>2)<<16)|((g<<2|g>>4)<<8)|(b<<3|b>>2);}
static void present_reference(uint32_t *lcd,unsigned mode){
 unsigned w=mode==0?362:mode==1?448:480,h=mode==1?224:272,left=(480-w)/2,top=(272-h)/2;
 /* The full LCD is owned by the app. Clear margins on every draw so a mode
  * change or pause menu cannot leave old pixels at either edge. */
 for(unsigned y=0;y<272;y++)for(unsigned x=0;x<480;x++)lcd[y*480+x]=0;
 for(unsigned y=0;y<h;y++){const uint32_t *src=pixels+(y*224/h)*448;uint32_t *dst=lcd+(top+y)*480+left;for(unsigned x=0;x<w;x++)dst[x]=src[x*448/w];}
}
#if H1_MXU
extern void h1_mxu_rgb565(uint32_t*,const uint16_t*,unsigned);
extern void h1_mxu_copy32(uint32_t*,const uint32_t*,unsigned);
#endif
#include "pgm_present_fast.h"
#include "pgm_stream.h"
void h1_pgm_present(uint32_t *native,unsigned mode){
 if(pgm_stream_ready){pgm_stream_present(native,mode);return;}
 if(h1_pgm_present_mode){present_fast(native,mode);return;}
 h1_pgm_present_reset();
 phase(6);uint32_t *lcd=native;uintptr_t address=(uintptr_t)native;
 unsigned cached=address>=0xa0000000u&&address<0xa4000000u&&!(address&255u)&&480u*272u*4u<=0xa4000000u-address;
 if(cached)lcd=(uint32_t*)((address&0x1fffffffu)|0x80000000u);
 unsigned w=mode==0?362:mode==1?448:480,h=mode==1?224:272,left=(480-w)/2,top=(272-h)/2;
 static uint16_t columns[480];static unsigned last_width;
 if(last_width!=w){for(unsigned x=0;x<w;x++)columns[x]=x*448/w;last_width=w;}
 if(top){memset(lcd,0,top*480*4);memset(lcd+(top+h)*480,0,(272-top-h)*480*4);}
 uint32_t row[480];unsigned last=~0u;
 for(unsigned y=0;y<h;y++){
  unsigned sy=y*224/h;
  if(sy!=last){const uint32_t *src=pixels+sy*448;
   for(unsigned x=0;x<w;x++)row[x]=src[columns[x]];
   last=sy;
  }
  uint32_t *dst=lcd+(top+y)*480;memset(dst,0,left*4);
#if H1_MXU
  unsigned n=w&~7u;h1_mxu_copy32(dst+left,row,n);h1_pgm_perf.mxu_pixels+=n;
  for(unsigned x=n;x<w;x++)dst[left+x]=row[x];
#else
  memcpy(dst+left,row,w*4);
#endif
  memset(dst+left+w,0,(480-left-w)*4);
 }
#if defined(__mips__)
 if(((uintptr_t)lcd&0xe0000000u)==0x80000000u){
  uintptr_t a=(uintptr_t)lcd&~15u,end=(uintptr_t)(lcd+480*272);
  __asm__ volatile("sync":::"memory");
  for(;a<end;a+=16)__asm__ volatile("cache 0x15,0(%0)"::"r"(a):"memory");
  __asm__ volatile("sync":::"memory");
 }
#endif
 phase(0);
}
void h1_pgm_refresh(uint32_t *native,unsigned mode){
 h1_pgm_present_reset();
 if(!pgm_stream_prepare())draw();
 h1_pgm_present(native,mode);
}
static void present_check(void){
 uint32_t *reference=malloc(480*272*4),*actual=malloc(480*272*4);
 if(!reference||!actual){free(reference);free(actual);for(unsigned i=0;i<3;i++)h1_pgm_draw_probe.present_differences[i]=~0u;return;}
 for(unsigned mode=0;mode<3;mode++){
  present_reference(reference,mode);memset(actual,0xa5,480*272*4);h1_pgm_present_reset();h1_pgm_present(actual,mode);
  unsigned n=0;for(unsigned i=0;i<480*272;i++)n+=reference[i]!=actual[i];h1_pgm_draw_probe.present_differences[mode]=n;
  /* Stable output may omit black-margin writes. A UI can overwrite the
   * same target without changing its address or display mode. */
  h1_pgm_present(actual,mode);for(unsigned i=0;i<480*272;i++)n+=reference[i]!=actual[i];
  memset(actual,0x5a,480*272*4);h1_pgm_present_reset();h1_pgm_present(actual,mode);
  for(unsigned i=0;i<480*272;i++)n+=reference[i]!=actual[i];h1_pgm_draw_probe.present_differences[mode]=n;
 }
 h1_pgm_present_reset();
 free(reference);free(actual);
}
volatile struct {unsigned magic,version,request,state,loops,draw_ticks[3],present_ticks[2],differences[5];} h1_pgm_pipeline_bench={0x314c5050,1};
static void pipeline_benchmark(void){
 unsigned loops=h1_pgm_pipeline_bench.request;if(!loops)return;
 h1_pgm_pipeline_bench.request=0;h1_pgm_pipeline_bench.state=1;if(loops>128)loops=128;
 uint32_t *expected=malloc(WIDTH*HEIGHT*4),*reference=malloc(480*272*4),*actual=malloc(480*272*4);
 if(!expected||!reference||!actual){free(expected);free(reference);free(actual);h1_pgm_pipeline_bench.state=4;return;}
 unsigned layers=h1_pgm_layers_mode,present=h1_pgm_present_mode,enabled=h1_pgm_perf.enabled;
 h1_pgm_perf.enabled=0;h1_pgm_layers_mode=0;draw();memcpy(expected,pixels,WIDTH*HEIGHT*4);
 unsigned fail=0;
 for(unsigned m=0;m<3;m++){
  h1_pgm_layers_mode=m;draw();unsigned start=h1_raw_tick_80hz();
  for(unsigned i=0;i<loops;i++)draw();h1_pgm_pipeline_bench.draw_ticks[m]=h1_raw_tick_80hz()-start;
  unsigned n=0;for(unsigned i=0;i<WIDTH*HEIGHT;i++)n+=expected[i]!=pixels[i];h1_pgm_pipeline_bench.differences[m]=n;fail+=n;
 }
 h1_pgm_present_mode=0;h1_pgm_present(reference,0);
 for(unsigned m=0;m<2;m++){
  h1_pgm_present_mode=m;h1_pgm_present(actual,0);unsigned start=h1_raw_tick_80hz();
  for(unsigned i=0;i<loops;i++)h1_pgm_present(actual,0);h1_pgm_pipeline_bench.present_ticks[m]=h1_raw_tick_80hz()-start;
  unsigned n=0;for(unsigned i=0;i<480*272;i++)n+=reference[i]!=actual[i];h1_pgm_pipeline_bench.differences[3+m]=n;fail+=n;
 }
 h1_pgm_layers_mode=layers;h1_pgm_present_mode=present;h1_pgm_perf.enabled=enabled;perf_last=0;
 free(expected);free(reference);free(actual);h1_pgm_pipeline_bench.loops=loops;h1_pgm_pipeline_bench.state=fail?3:2;
}
/* Paused physical-device comparison: original RGB565 rendering is the
 * independent oracle. Timings cover complete draw+LCD output in 4:3, in
 * forward/reverse order to expose noise in the coarse firmware clock. */
volatile struct {
 unsigned magic,version,request,state,loops,ticks[3][2],differences[3][3],prepare_failures;
} h1_pgm_stream_probe={0x31535050,1};
static int stream_probe_render(uint32_t *target,unsigned mode,unsigned variant){
 if(!variant){h1_pgm_scale_mode=0;draw();present_fast(target,mode);return 1;}
 h1_pgm_scale_mode=variant-1;
 if(!pgm_stream_prepare()){++h1_pgm_stream_probe.prepare_failures;return 0;}
 pgm_stream_present(target,mode);return 1;
}
static void stream_benchmark(void){
 unsigned loops=h1_pgm_stream_probe.request;if(!loops)return;
 h1_pgm_stream_probe.request=0;h1_pgm_stream_probe.state=1;if(loops>128)loops=128;
 uint16_t *gold=malloc(WIDTH*HEIGHT*2);
 uint32_t *reference=malloc(480*272*4),*actual=malloc(480*272*4);
 if(!gold||!reference||!actual){free(gold);free(reference);free(actual);h1_pgm_stream_probe.state=4;return;}
 unsigned enabled=h1_pgm_perf.enabled,layers=h1_pgm_layers_mode;
 unsigned stream=h1_pgm_stream_mode,scale=h1_pgm_scale_mode;
 h1_pgm_perf.enabled=0;h1_pgm_layers_mode=1;h1_pgm_stream_mode=1;
 h1_pgm_stream_probe.prepare_failures=0;
 reference_pixels=gold;draw_reference();reference_pixels=0;
 unsigned fail=0;
 for(unsigned mode=0;mode<3;mode++){
  for(unsigned i=0;i<WIDTH*HEIGHT;i++)pixels[i]=expand(gold[i]);
  present_reference(reference,mode);
  for(unsigned variant=0;variant<3;variant++){
   unsigned n=0;
   for(unsigned pass=0;pass<3;pass++){
    if(pass!=1){memset(actual,0xa5,480*272*4);h1_pgm_present_reset();}
    if(!stream_probe_render(actual,mode,variant)){n=~0u;break;}
    for(unsigned i=0;i<480*272;i++)n+=reference[i]!=actual[i];
   }
   h1_pgm_stream_probe.differences[variant][mode]=n;fail|=n;
  }
 }
 if(!h1_pgm_stream_probe.prepare_failures){
  for(unsigned round=0;round<2;round++)for(unsigned k=0;k<3;k++){
   unsigned variant=round?2-k:k;h1_pgm_present_reset();
   stream_probe_render(actual,0,variant);unsigned start=h1_raw_tick_80hz();
   for(unsigned i=0;i<loops;i++)if(!stream_probe_render(actual,0,variant))break;
   h1_pgm_stream_probe.ticks[variant][round]=h1_raw_tick_80hz()-start;
  }
 }
 h1_pgm_stream_mode=stream;h1_pgm_scale_mode=scale;h1_pgm_layers_mode=layers;
 h1_pgm_perf.enabled=enabled;perf_last=0;h1_pgm_present_reset();draw();
 free(gold);free(reference);free(actual);h1_pgm_stream_probe.loops=loops;
 h1_pgm_stream_probe.state=fail||h1_pgm_stream_probe.prepare_failures?3:2;
}
