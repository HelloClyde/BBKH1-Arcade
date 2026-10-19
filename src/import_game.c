/* Streaming ZIP -> H1NEO conversion. GPL-2.0-or-later.
 * ZIP entries and complete graphics never reside in memory together. */
#include "import_game.h"
#include "import_zip.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "import_drivers.h"
#include "import_gfx.h"
#define BLOCK 32768u
typedef struct {uint32_t id,offset,size,crc;} part;
typedef struct {
 h1_zip *game,*bios;h1_zip_stream *a,*b;FILE *out;
 part parts[15];unsigned sizes[15],count,total,written;
 uint8_t *buffer,*lane,*usage,*pages;uint8_t fix[2][4096];
 const import_driver *driver;h1_import_progress notify;void *user;char *error;
 int canceled;
} context;
static uint32_t read32(const uint8_t *p){return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static uint32_t checksum(const void *p,unsigned n){return ~h1_import_crc(~0u,p,n);}
static int progress(context *c,const char *stage,unsigned done,unsigned total){
 if(c->notify&&!c->notify(c->user,stage,done,total)){c->canceled=1;return 0;}return 1;
}
static int fail(context *c,const char *s){snprintf(c->error,128,"%s",s);return 0;}
static int write_bytes(context *c,const void *p,unsigned n,uint32_t *crc){
 if(fwrite(p,1,n,c->out)!=n)return fail(c,"Cannot write cache: check free space");
 if(crc)*crc=h1_import_crc(*crc,p,n);c->written+=n;return 1;
}
static int zeros(context *c,unsigned n,uint32_t *crc){
 memset(c->buffer,0,BLOCK);
 while(n){unsigned k=n<BLOCK?n:BLOCK;if(!write_bytes(c,c->buffer,k,crc)||!progress(c,"padding",c->written,c->total))return 0;n-=k;}
 return 1;
}
static int skip(h1_zip_stream *s,unsigned n,uint8_t *buffer){
 while(n){unsigned k=n<BLOCK?n:BLOCK;if(!h1_zip_read(s,buffer,k))return 0;n-=k;}return 1;
}
/* 68000 native word order: same unambiguous reset-vector check as PC tool. */
static int word_order(const uint8_t *p,unsigned size){
 int result=-1;unsigned count=0;
 for(unsigned swap=0;swap<2;swap++){
  unsigned w[4];for(unsigned i=0;i<4;i++)w[i]=p[i*2+swap]|p[i*2+1-swap]<<8;
  unsigned sp=w[0]<<16|w[1],pc=w[2]<<16|w[3];
  if(sp>=0x100000&&sp<=0x110000&&!(pc&1)&&((pc>=0x80&&pc<size)||(pc>=0xc00000&&pc<0xc20000))){result=(int)swap;++count;}
 }return count==1?result:-1;
}
/* GnGeo's fixed-layer swizzle, one 32-byte tile at a time; no allocation. */
static void characters(uint8_t *data,unsigned size,uint8_t *usage){
 for(unsigned offset=0;offset<size;offset+=32){
  uint8_t source[32],used=0;memcpy(source,data+offset,32);
  for(unsigned x=0;x<8;x++){
   uint8_t *p=data+offset+x*4;p[0]=source[x+16];p[1]=source[x+24];p[2]=source[x];p[3]=source[x+8];used|=p[0]|p[1]|p[2]|p[3];
  }usage[offset/32]=used;
 }
}
static int regular(context *c,unsigned id,uint32_t *crc){
 unsigned dest=0;int swap=-1;
 const char *stage=id==3||id==4?"sound":id==5||id==6?"fixed":"program";
 while(dest<c->sizes[id]){
  const import_rom *r=0;unsigned best=c->sizes[id];int bios_index=-1;
  if(id==5||id==7||id==12){
   static const char *main_bios[]={"vs-bios.rom","sp-s2.sp1","usa_2slt.bin","asia-s3.rom"};
   static const char *fixed[]={"sfix.sfix","sfix.sfx"};
   const char *single="000-lo.lo";
   const char **names=id==7?main_bios:id==5?fixed:&single;unsigned count=id==7?4:id==5?2:1;
   if(dest)return fail(c,"Invalid BIOS layout");
   for(unsigned i=0;i<count;i++)if((bios_index=h1_zip_find(c->bios,names[i],0,0))>=0)break;
   if(bios_index<0||h1_zip_size(c->bios,bios_index)!=0x20000)return fail(c,"Missing or incorrect 128 KiB Neo Geo BIOS");
  }else{
   for(unsigned i=0;i<c->driver->count;i++)if(c->driver->roms[i].id==id&&c->driver->roms[i].dest>=dest&&c->driver->roms[i].dest<best){r=c->driver->roms+i;best=r->dest;}
   if(!r){if(!zeros(c,c->sizes[id]-dest,crc))return 0;break;}
   if(r->dest>dest){if(!zeros(c,r->dest-dest,crc))return 0;dest=r->dest;}
  }
  unsigned n=r?r->size:0x20000,source=r?r->src:0;
  int index=r?h1_zip_find(c->game,r->name,r->crc,1):bios_index;h1_zip *z=r?c->game:c->bios;
  if(index<0){snprintf(c->error,128,"Missing or incorrect ROM: %s",r?r->name:"BIOS");return 0;}
  if(source>h1_zip_size(z,index)||n>h1_zip_size(z,index)-source||n>c->sizes[id]-dest)return fail(c,"ROM exceeds region");
  c->a=h1_zip_start(z,index);if(!c->a||!skip(c->a,source,c->buffer))return fail(c,"Invalid compressed ROM");
  for(unsigned offset=0;offset<n;){
   unsigned bytes=n-offset<BLOCK?n-offset:BLOCK;
   if(!h1_zip_read(c->a,c->buffer,bytes))return fail(c,"Damaged compressed ROM");
   if(id==7||id==8){
    if(swap<0){if(bytes<8||(swap=word_order(c->buffer,id==7?0x20000:c->driver->sizes[8]))<0)return fail(c,"Unrecognized 68000 reset vector");}
    if(bytes&1)return fail(c,"Odd program ROM size");
    if(swap)for(unsigned i=0;i<bytes;i+=2){uint8_t b=c->buffer[i];c->buffer[i]=c->buffer[i+1];c->buffer[i+1]=b;}
   }
   if(id==5||id==6){if(bytes%32)return fail(c,"Invalid fixed graphics size");characters(c->buffer,bytes,c->fix[id==6]+(dest+offset)/32);}
   if(!write_bytes(c,c->buffer,bytes,crc)||!progress(c,stage,dest+offset+bytes,c->sizes[id]))return 0;offset+=bytes;
  }
  if(!h1_zip_finish(c->a))return fail(c,"ROM CRC check failed");h1_zip_stop(c->a);c->a=0;dest+=n;
 }
 return 1;
}
static int sprite_block(context *c,unsigned dest,unsigned bytes,uint32_t *crc){
 unsigned base=dest/128;
 for(unsigned i=0;i<bytes/128;i++)((uint32_t*)c->usage)[(base+i)/16]|=(unsigned)convert_roms_tile(c->buffer,(int)i);
 for(unsigned i=0;i<bytes;i+=4096)put32(c->pages+(dest+i)/4096*4,checksum(c->buffer+i,4096));
 return write_bytes(c,c->buffer,bytes,crc)&&progress(c,"sprites",dest+bytes,c->sizes[9]);
}
static int sprites(context *c,uint32_t *crc){
 unsigned dest=0;
 while(dest<c->sizes[9]){
  const import_rom *r[2]={0,0};
  for(unsigned i=0;i<c->driver->count;i++)for(unsigned lane=0;lane<2;lane++)if(c->driver->roms[i].id==9&&c->driver->roms[i].dest==dest+lane)r[lane]=c->driver->roms+i;
  if(!r[0]&&!r[1]){
   unsigned next=c->sizes[9];
   for(unsigned i=0;i<c->driver->count;i++)if(c->driver->roms[i].id==9&&c->driver->roms[i].dest>dest&&(c->driver->roms[i].dest&~1u)<next)next=c->driver->roms[i].dest&~1u;
   if(next%4096)return fail(c,"Unaligned sprite gap");
   while(dest<next){unsigned n=next-dest<BLOCK?next-dest:BLOCK;memset(c->buffer,0,n);if(!sprite_block(c,dest,n,crc))return 0;dest+=n;}
   continue;
  }
  if(!r[0]||!r[1]||r[0]->size!=r[1]->size||r[0]->size>(c->sizes[9]-dest)/2||r[0]->size%2048)return fail(c,"Unsupported sprite layout");
  for(unsigned lane=0;lane<2;lane++){
   int index=h1_zip_find(c->game,r[lane]->name,r[lane]->crc,1);unsigned start=r[lane]->src/2;
   if(index<0||start>h1_zip_size(c->game,index)||r[lane]->size>h1_zip_size(c->game,index)-start){snprintf(c->error,128,"Missing or incorrect ROM: %s",r[lane]->name);return 0;}
   h1_zip_stream *s=h1_zip_start(c->game,index);if(lane)c->b=s;else c->a=s;
   if(!s||!skip(s,start,c->buffer))return fail(c,"Invalid compressed sprite ROM");
  }
  for(unsigned offset=0;offset<r[0]->size;){
   unsigned n=r[0]->size-offset<BLOCK/2?r[0]->size-offset:BLOCK/2;
   if(!h1_zip_read(c->a,c->lane,n)||!h1_zip_read(c->b,c->lane+BLOCK/2,n))return fail(c,"Damaged compressed sprite ROM");
   for(unsigned i=0;i<n;i++){c->buffer[2*i]=c->lane[i];c->buffer[2*i+1]=c->lane[BLOCK/2+i];}
   if(!sprite_block(c,dest+offset*2,n*2,crc))return 0;offset+=n;
  }
  if(!h1_zip_finish(c->a)||!h1_zip_finish(c->b))return fail(c,"Sprite ROM CRC check failed");
  h1_zip_stop(c->a);h1_zip_stop(c->b);c->a=c->b=0;dest+=r[0]->size*2;
 }return 1;
}
static int header_ok(FILE *f,const char *name){
 uint8_t h[64];if(fseek(f,0,SEEK_END))return 0;long n=ftell(f);
 if(n<64||fseek(f,0,SEEK_SET)||fread(h,1,64,f)!=64)return 0;
 return !memcmp(h,"H1NEO1\0",8)&&read32(h+8)==1&&read32(h+12)>0&&read32(h+12)<=15&&read32(h+16)==(unsigned)n&&read32(h+20)==0&&
   memchr(h+24,0,16)&&!strcmp((char*)h+24,name)&&read32(h+60)==checksum(h,60);
}
int h1_import_cached(const char *path,const char *name){FILE *f=fopen(path,"rb");if(!f)return 0;int ok=header_ok(f,name);fclose(f);return ok;}
int h1_import_game(const char *game,const char *bios,const char *output,const char *set,h1_import_progress notify,void *user,char error[128]){
 context *c=calloc(1,sizeof(*c));char temporary[256]={0};int result=-1,owns_temp=0;FILE *existing=0;uint8_t h[64]={0};
 error[0]=0;if(!c){snprintf(error,128,"Not enough memory");return -1;}c->notify=notify;c->user=user;c->error=error;
 if(strlen(output)+5>=sizeof temporary||!set||strlen(set)>15){fail(c,"Path too long");goto done;}
 for(unsigned i=0;i<IMPORT_DRIVER_COUNT;i++)if(!strcmp(set,import_drivers[i].name))c->driver=import_drivers+i;
 if(!c->driver){fail(c,"Unsupported game ZIP name");goto done;}
 if(!progress(c,"inspect",0,1))goto done;
 existing=fopen(output,"rb");if(existing){fclose(existing);fail(c,"Cache already exists; keep or rename it");goto done;}
 snprintf(temporary,sizeof temporary,"%s.tmp",output);existing=fopen(temporary,"rb");if(existing){fclose(existing);fail(c,"Incomplete .h1neo.tmp exists; remove it to retry");goto done;}
 c->game=h1_zip_open(game);c->bios=h1_zip_open(bios);if(!c->game||!c->bios){fail(c,"Cannot read game ZIP / neogeo.zip");goto done;}
 for(unsigned i=0;i<10;i++)c->sizes[i]=c->driver->sizes[i];
 c->sizes[5]=c->sizes[7]=c->sizes[12]=0x20000;c->sizes[8]=(c->sizes[8]+0xfffff)&~0xfffffu;
 if(c->sizes[6]!=0x20000||!c->sizes[9]||c->sizes[9]%4096||c->sizes[9]>0x4000000){fail(c,"Unsupported graphics layout");goto done;}
 c->sizes[10]=c->sizes[9]/512;c->sizes[11]=c->sizes[13]=4096;c->sizes[14]=c->sizes[9]/4096*4;
 c->buffer=malloc(BLOCK);c->lane=malloc(BLOCK);c->usage=calloc(1,c->sizes[10]);c->pages=malloc(c->sizes[14]);
 if(!c->buffer||!c->lane||!c->usage||!c->pages){fail(c,"Not enough conversion memory");goto done;}
 for(unsigned id=0;id<15;id++)if(c->sizes[id])++c->count;
 c->total=64+c->count*16;
 unsigned index=0;for(unsigned id=0;id<15;id++)if(c->sizes[id]){c->parts[index++]=(part){id,c->total,c->sizes[id],0};c->total+=c->sizes[id];}
 c->out=fopen(temporary,"wb");if(!c->out){fail(c,"Cannot create cache beside ZIP");goto done;}owns_temp=1;
 if(!zeros(c,64+c->count*16,0))goto done;
 for(unsigned i=0;i<c->count;i++){
  unsigned id=c->parts[i].id;uint32_t crc=~0u;
  if(id==9){if(!sprites(c,&crc))goto done;}
  else if(id==10||id==11||id==13||id==14){
   const void *data=id==10?c->usage:id==11?c->fix[1]:id==13?c->fix[0]:c->pages;
   if(!write_bytes(c,data,c->sizes[id],&crc))goto done;
  }else if(!regular(c,id,&crc))goto done;
  c->parts[i].crc=~crc;
 }
 if(c->written!=c->total){fail(c,"Conversion size mismatch");goto done;}
 memcpy(h,"H1NEO1\0",8);put32(h+8,1);put32(h+12,c->count);put32(h+16,c->total);memcpy(h+24,set,strlen(set));put32(h+60,checksum(h,60));
 if(fseek(c->out,0,SEEK_SET)||fwrite(h,1,64,c->out)!=64||fwrite(c->parts,16,c->count,c->out)!=c->count){fail(c,"Cannot finish cache header");goto done;}
 {int close_error=fclose(c->out);c->out=0;if(close_error){fail(c,"Cannot close converted cache");goto done;}}
 c->out=fopen(temporary,"rb");if(!c->out||!header_ok(c->out,set)||fseek(c->out,64+c->count*16,SEEK_SET)){fail(c,"Cache readback failed");goto done;}
 unsigned checked=0;
 for(unsigned i=0;i<c->count;i++){
  uint32_t crc=~0u;unsigned left=c->parts[i].size;
  while(left){unsigned n=left<BLOCK?left:BLOCK;if(fread(c->buffer,1,n,c->out)!=n){fail(c,"Cache readback failed");goto done;}
   crc=h1_import_crc(crc,c->buffer,n);left-=n;checked+=n;if(!progress(c,"verify",checked,c->total-64-c->count*16))goto done;}
  if(~crc!=c->parts[i].crc){fail(c,"Written cache CRC check failed");goto done;}
 }
 {int close_error=fclose(c->out);c->out=0;if(close_error){fail(c,"Cache readback close failed");goto done;}}
 if(rename(temporary,output)){fail(c,"Cannot commit converted cache");goto done;}owns_temp=0;
 if(notify)notify(user,"done",c->total,c->total);result=1;
done:
 if(c->out)fclose(c->out);h1_zip_stop(c->a);h1_zip_stop(c->b);h1_zip_close(c->game);h1_zip_close(c->bios);
 if(owns_temp&&remove(temporary)){snprintf(error,128,"Cannot remove incomplete .h1neo.tmp cache");c->canceled=0;result=-1;}
 if(c->canceled)result=0;else if(result<0&&!error[0])snprintf(error,128,"Conversion failed");
 free(c->buffer);free(c->lane);free(c->usage);free(c->pages);free(c);return result;
}
