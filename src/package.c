/* Checked native ROM container and bounded sprite cache. GPL-2.0-or-later. */
#include "h1_port.h"
#include "memory.h"
#include "emu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "profile.h"
#define PAGE 4096u
typedef struct {uint32_t id,offset,size,crc;} section_t;
static FILE *package;
static section_t sections[15];
static unsigned count,slots;
static uint8_t *regions[15],*cache;
static int32_t *tags;
/* Direct page lookup plus a fully associative LRU avoids ROM banks evicting
 * one another solely because their page numbers differ by the cache size.
 * Reserve the small index in BSS: large cartridges exhaust the native heap. */
#define NONE 0xffffu
static uint16_t page_slot[0x4000000/PAGE],older[1024],newer[1024];
static unsigned newest=NONE,oldest=NONE,used_slots;
#if H1_MXU_RENDER
static uint32_t opaque_tiles[1024];
static int tile_opaque(const uint32_t *p){
 for(unsigned i=0;i<32;i++)if((p[i]-0x11111111u)&~p[i]&0x88888888u)return 0;
 return 1;
}
unsigned h1_sprite_opaque_key(const uint32_t *p){
 uintptr_t address=(uintptr_t)p,base=(uintptr_t)cache;
 if(cache&&address>=base&&address-base<slots*PAGE&&!((address-base)&127)){
  unsigned offset=(unsigned)(address-base);
  if(!((opaque_tiles[offset/PAGE]>>(offset%PAGE/128))&1))return ~0u;
  return (unsigned)tags[offset/PAGE]*32+offset%PAGE/128;
 }
 return tile_opaque(p)?0x80000000u:~0u; /* startup fixtures have mutable graphics */
}
int h1_sprite_tile_opaque(const uint32_t *p){
 uintptr_t address=(uintptr_t)p,base=(uintptr_t)cache;
 if(cache&&address>=base&&address-base<slots*PAGE&&!((address-base)&127)){
  unsigned offset=(unsigned)(address-base);
  return (opaque_tiles[offset/PAGE]>>(offset%PAGE/128))&1;
 }
 return tile_opaque(p);
}
#endif
static void touch_slot(unsigned slot){
 if(newest==slot)return;
 unsigned a=newer[slot],b=older[slot];
 if(a!=NONE)older[a]=b;
 if(b!=NONE)newer[b]=a;
 if(oldest==slot)oldest=a;
 newer[slot]=NONE;older[slot]=newest;
 if(newest!=NONE)newer[newest]=slot;
 newest=slot;if(oldest==NONE)oldest=slot;
}
static char savepath[256];
static uint32_t crc_table[256];
static uint32_t crc(const void *data,unsigned size){
 const uint8_t *p=data;uint32_t value=~0u;
 if(!crc_table[1])for(unsigned i=0;i<256;i++){uint32_t c=i;for(int j=0;j<8;j++)c=(c>>1)^((0u-(c&1))&0xedb88320u);crc_table[i]=c;}
 while(size--)value=crc_table[(value^*p++)&255]^(value>>8);
 return ~value;
}
static int read_at(unsigned offset,void *data,unsigned size){return !fseek(package,offset,SEEK_SET)&&fread(data,1,size,package)==size;}
static section_t *find(unsigned id){for(unsigned i=0;i<count;i++)if(sections[i].id==id)return &sections[i];return 0;}
static int load_region(unsigned id){
 section_t *s=find(id);if(!s)return 0;
 unsigned alloc=s->size;if(id==1&&alloc<0x80000)alloc=0x80000;
 regions[id]=calloc(1,alloc);if(!regions[id])return 0;
 return read_at(s->offset,regions[id],s->size)&&crc(regions[id],s->size)==s->crc;
}
static void bind(ROM_REGION *r,unsigned id){section_t *s=find(id);r->p=regions[id];r->size=s?s->size:0;}
int h1_neo_load(const char *path){
 uint8_t h[64];unsigned total,previous;uint32_t words[4];
 package=fopen(path,"rb");if(!package){h1_arcade_stats.error=20;return 0;}
 if(fseek(package,0,SEEK_END)||ftell(package)<64){h1_arcade_stats.error=21;return 0;}
 total=(unsigned)ftell(package);
 if(!read_at(0,h,64)||memcmp(h,"H1NEO1\0",8)){h1_arcade_stats.error=21;return 0;}
 memcpy(words,h+8,16);count=words[1];
 if(!memchr(h+24,0,16)){h1_arcade_stats.error=21;return 0;}
 uint32_t expected;memcpy(&expected,h+60,4);
 if(words[0]!=1||!count||count>15||words[2]!=total||words[3]!=0||crc(h,60)!=expected||
    !read_at(64,sections,count*sizeof(section_t))){h1_arcade_stats.error=21;return 0;}
 previous=64+count*sizeof(section_t);
 for(unsigned i=0;i<count;i++){
  section_t *s=&sections[i];
  if(s->id>=15||!s->size||s->offset<previous||s->offset>total||s->size>total-s->offset){h1_arcade_stats.error=22;return 0;}
  for(unsigned j=0;j<i;j++)if(sections[j].id==s->id){h1_arcade_stats.error=22;return 0;}
  if(s->id!=9&&s->size>16u*1024*1024){h1_arcade_stats.error=22;return 0;}
  previous=s->offset+s->size;
 }
 // All required regions have exact or bounded sizes before allocating anything.
 const unsigned required[]={1,3,5,6,7,8,9,10,11,12,13,14};
 for(unsigned i=0;i<sizeof required/sizeof required[0];i++)if(!find(required[i])){h1_arcade_stats.error=23;return 0;}
 if(find(1)->size>0x80000||find(5)->size!=0x20000||find(6)->size!=0x20000||find(7)->size!=0x20000||
    find(8)->size<0x100000||find(8)->size>0x800000||find(8)->size%0x100000||
    find(9)->size>0x4000000||find(9)->size%PAGE||
    find(10)->size!=find(9)->size/512||find(11)->size!=4096||find(12)->size!=0x20000||
    find(13)->size!=4096||find(14)->size!=find(9)->size/PAGE*4){h1_arcade_stats.error=23;return 0;}
 for(unsigned i=0;i<count;i++)if(sections[i].id!=9&&!load_region(sections[i].id)){
    h1_arcade_log("REGION_FAIL id=%u bytes=%u",sections[i].id,sections[i].size);h1_arcade_stats.error=24;return 0;
 }
 bind(&memory.rom.cpu_m68k,8);bind(&memory.rom.cpu_z80,1);memory.rom.cpu_z80.size=0x80000;
 bind(&memory.rom.adpcma,3);bind(&memory.rom.adpcmb,4);
 if(!memory.rom.adpcmb.p)memory.rom.adpcmb=memory.rom.adpcma;
 bind(&memory.rom.bios_m68k,7);bind(&memory.rom.bios_sfix,5);bind(&memory.rom.game_sfix,6);
 bind(&memory.rom.spr_usage,10);bind(&memory.rom.gfix_usage,11);
 memory.rom.tiles.size=find(9)->size;memory.nb_of_tiles=memory.rom.tiles.size/128;
 memory.ng_lo=regions[12];memory.fix_game_usage=regions[11];memcpy(memory.fix_board_usage,regions[13],4096);
 memcpy(memory.game_vector,memory.rom.cpu_m68k.p,128);memcpy(memory.rom.cpu_m68k.p,memory.rom.bios_m68k.p,128);
 for(unsigned bytes=4u*1024*1024;bytes>=256*1024;bytes/=2){cache=malloc(bytes);if(cache){slots=bytes/PAGE;break;}}
 tags=malloc(slots*sizeof(*tags));if(!cache||!tags){h1_arcade_stats.error=25;return 0;}
 memset(page_slot,0xff,sizeof page_slot);memset(older,0xff,sizeof older);memset(newer,0xff,sizeof newer);
 newest=oldest=NONE;used_slots=0;
 for(unsigned i=0;i<slots;i++)tags[i]=-1;
 memory.vid.spr_cache.data=cache;memory.vid.spr_cache.slot_size=PAGE;
 h1_arcade_stats.cache_bytes=slots*PAGE;
 unsigned len=strlen(path);if(len+5>=sizeof savepath){h1_arcade_stats.error=26;return 0;}
 memcpy(savepath,path,len+1);strcat(savepath,".nv");
 // Per-game NVRAM; checked header and payload, loaded by save_io below.
 h1_arcade_log("LOAD_OK game=%s sprites=%u cache=%u",h+24,memory.rom.tiles.size,slots*PAGE);
 return 1;
}
uint8_t *h1_sprite_page(unsigned tile){
 unsigned bank=tile/(PAGE/128);if(bank>=memory.rom.tiles.size/PAGE)h1_arcade_escape(27);
 unsigned slot=page_slot[bank];
 if(slot!=NONE){++h1_arcade_stats.cache_hits;touch_slot(slot);return cache+slot*PAGE;}
 ++h1_arcade_stats.cache_misses;
 unsigned prof=h1_profile_set(HP_IO);
 slot=used_slots<slots?used_slots++:oldest;
 if(tags[slot]>=0)page_slot[tags[slot]]=NONE;
 uint8_t *p=cache+slot*PAGE;uint32_t expected;memcpy(&expected,regions[14]+bank*4,4);
 if(!read_at(find(9)->offset+bank*PAGE,p,PAGE)||crc(p,PAGE)!=expected)h1_arcade_escape(28);
#if H1_MXU_RENDER
 uint32_t opaque=0;
 for(unsigned tile=0;tile<32;tile++)if(tile_opaque((const uint32_t*)(p+tile*128)))opaque|=1u<<tile;
 opaque_tiles[slot]=opaque;
#endif
 tags[slot]=(int)bank;page_slot[bank]=slot;touch_slot(slot);h1_profile_set(prof);return p;
}
typedef struct {uint32_t magic,seq,crc,bytes;} nv_header;
static unsigned nvseq,nvloaded,nvblocked;
/* The real H1 can load a large cartridge but have no 68 KiB heap block left.
 * Keep the single save/readback buffer in the reserved BDA loading window. */
static uint8_t nv_buffer[0x10800];
void h1_neo_save(void){
 if(!savepath[0]||nvblocked)return;
 char name[260];uint8_t *backup=nv_buffer;
 if(!nvloaded){
  nvloaded=1;unsigned best=0,had_file=0;
  for(unsigned slot=0;slot<2;slot++){
   snprintf(name,sizeof name,"%s%u",savepath,slot);FILE *f=fopen(name,"rb");if(!f)continue;had_file=1;
   nv_header h;int ok=fread(&h,1,sizeof h,f)==sizeof h&&h.magic==0x31564e48&&h.bytes==0x10800&&
      fread(backup,1,0x10800,f)==0x10800&&crc(backup,0x10800)==h.crc;fclose(f);
   if(ok&&(!best||(int32_t)(h.seq-best)>0)){
    best=h.seq;memcpy(memory.sram,backup,0x10000);memcpy(memory.memcard,backup+0x10000,0x800);
   }
  }
  if(best)nvseq=best;
  else if(had_file){nvblocked=1;h1_arcade_log("NVRAM_CORRUPT saving_disabled=1");}
  return;
 }
 memcpy(backup,memory.sram,0x10000);memcpy(backup+0x10000,memory.memcard,0x800);
 nv_header h={0x31564e48,nvseq+1,crc(backup,0x10800),0x10800};
 snprintf(name,sizeof name,"%s%u",savepath,h.seq&1);FILE *f=fopen(name,"wb");
 int ok=f&&fwrite(&h,1,sizeof h,f)==sizeof h&&fwrite(backup,1,0x10800,f)==0x10800;
 if(f&&fclose(f))ok=0;
 // Reopen to verify the new slot before advancing sequence.
 if(ok){f=fopen(name,"rb");nv_header check;
  ok=f&&fread(&check,1,sizeof check,f)==sizeof check&&!memcmp(&check,&h,sizeof h)&&
     fread(backup,1,0x10800,f)==0x10800&&crc(backup,0x10800)==h.crc;
  if(f)fclose(f);
 }
 if(ok)nvseq=h.seq;
 h1_arcade_log("NVRAM_SAVE ok=%u seq=%u",ok,nvseq);
}
void h1_neo_close(void){
 if(package)fclose(package);package=0;
 for(unsigned i=0;i<15;i++){free(regions[i]);regions[i]=0;}
 free(cache);cache=0;free(tags);tags=0;
 memory.vid.spr_cache.data=0;memory.rom.tiles.p=0;
}
