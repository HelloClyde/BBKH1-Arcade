/* Streaming PGM cache writer. GPL-2.0-or-later.
 * ROM layout/CRCs from pinned FBA-A320 d_pgm.cpp; no ROMs included. */
#include "pgm.h"
#include "pgm_format.h"
#include "import_zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BLOCK 32768u
typedef struct {const char *name;unsigned size,crc,region,dest,bios;} rom;
static const rom layout[]={
 {"pgm_p01s.rom",0x20000,0xe42b166e,PGM_BIOS,0,1},
 {"p0600.117",0x400000,0xc4d19fe6,PGM_PROGRAM,0,0},
 {"pgm_t01s.rom",0x200000,0x1a7123a0,PGM_TILES,0,1},
 {"t0600.rom",0x800000,0x4acc1ad6,PGM_TILES,0x400000,0},
 {"a0600.rom",0x800000,0xd8167834,PGM_COLOR,0,0},
 {"a0601.rom",0x800000,0xff7a4373,PGM_COLOR,0x800000,0},
 {"a0602.rom",0x800000,0xe7a32959,PGM_COLOR,0x1000000,0},
 {"a0603.rom",0x400000,0xec31abda,PGM_COLOR,0x1800000,0},
 {"b0600.rom",0x800000,0x7d3cd059,PGM_MASK,0,0},
 {"b0601.rom",0x400000,0xa0bb1c2f,PGM_MASK,0x800000,0},
 {"pgm_m01s.rom",0x200000,0x45ae7159,PGM_SOUND,0,1},
 {"m0600.rom",0x400000,0x3ada4fd6,PGM_SOUND,0x400000,0}
};
static const unsigned sizes[]={0x20000,0x400000,0xc00000,0x1c00000,0xc00000,0x800000};
static uint32_t checksum(const void *p,unsigned n){return ~h1_import_crc(~0u,p,n);}
int h1_pgm_paths(const char *p,char cache[240],char bios[240],char set[16]){
 unsigned n=strlen(p),base=0;if(n<8||n+7>240||p[1]!=':'||(p[2]!='\\'&&p[2]!='/'))return 0;
 for(unsigned i=0;i<n;i++){unsigned c=(uint8_t)p[i];if(c>=0x81&&c<=0xfe){unsigned t=(uint8_t)p[++i];if(i>=n||t<0x40||t>0xfe||t==0x7f)return 0;}else if(c=='\\'||c=='/')base=i+1;}
 if(n-base!=7)return 0;
 char name[8];for(unsigned i=0;i<7;i++){unsigned c=(uint8_t)p[base+i];name[i]=c>='A'&&c<='Z'?c+32:c;}name[7]=0;
 if(strcmp(name,"kov.zip"))return 0;
 memcpy(cache,p,n-4);strcpy(cache+n-4,".h1pgm");memcpy(bios,p,base);strcpy(bios+base,"pgm.zip");strcpy(set,"kov");return 1;
}
int h1_pgm_header(const uint8_t *h,unsigned size,const char *name){
 if(memcmp(h,"H1PGM1\0",8)||pgm_u32(h+8)!=1||pgm_u32(h+12)!=size||pgm_u32(h+16)!=PGM_REGIONS||
    !memchr(h+24,0,16)||strcmp((const char*)h+24,name)||pgm_u32(h+252)!=checksum(h,252))return 0;
 unsigned expected=PGM_HEADER;
 for(unsigned i=0;i<PGM_REGIONS;i++){const uint8_t *p=h+64+i*16;
  if(pgm_u32(p)!=expected||pgm_u32(p+4)!=sizes[i])return 0;expected+=sizes[i];}
 for(unsigned i=0;i<PGM_REGIONS;i++){if(pgm_u32(h+76+i*16)!=expected)return 0;expected+=sizes[i]/PGM_PAGE*4;}
 return expected==size;
}
int h1_pgm_cached(const char *path,const char *name){uint8_t h[256];FILE *f=fopen(path,"rb");if(!f)return 0;
 int ok=!fseek(f,0,SEEK_END);long n=ftell(f);ok=ok&&n>256&&!fseek(f,0,SEEK_SET)&&fread(h,1,256,f)==256&&h1_pgm_header(h,(unsigned)n,name);fclose(f);return ok;}
int h1_pgm_import(const char *game,const char *bios,const char *output,const char *set,h1_import_progress notify,void *user,char error[128]){
 h1_zip *z[2]={0,0};h1_zip_stream *stream=0;FILE *f=0;uint8_t *buf=0;uint32_t *pages=0;uint8_t h[256]={0};
 char temp[256];int result=-1,owned=0;unsigned total=256,written=0,page_count=0,index=0;
 error[0]=0;
 #define FAIL(msg) do{snprintf(error,128,"%s",msg);goto done;}while(0)
 #define NOTIFY(stage,d,t) do{if(notify&&!notify(user,stage,d,t)){result=0;goto done;}}while(0)
 if(strcmp(set,"kov")||strlen(output)>234)FAIL("Unsupported PGM set or path");
 snprintf(temp,sizeof temp,"%s.tmp",output);f=fopen(output,"rb");if(f){fclose(f);f=0;FAIL("PGM cache exists; keep or rename it");}
 f=fopen(temp,"rb");if(f){fclose(f);f=0;FAIL("Incomplete .h1pgm.tmp exists; rename it to retry");}
 NOTIFY("inspect",0,1);z[0]=h1_zip_open(game);z[1]=h1_zip_open(bios);if(!z[0]||!z[1])FAIL("Cannot read kov.zip / pgm.zip");
 /* Validate every required member before creating any output. CRC lookup also
  * accepts contemporary MAME filenames without weakening identity checks. */
 for(unsigned i=0;i<sizeof layout/sizeof *layout;i++){
  int m=h1_zip_find(z[layout[i].bios],layout[i].name,layout[i].crc,1);
  if(m<0||h1_zip_size(z[layout[i].bios],m)!=layout[i].size){snprintf(error,128,"Missing or incorrect PGM ROM: %s",layout[i].name);goto done;}}
 for(unsigned r=0;r<PGM_REGIONS;r++){pgm_put32(h+64+r*16,total);pgm_put32(h+68+r*16,sizes[r]);total+=sizes[r];page_count+=sizes[r]/4096;}
 unsigned pt=total;for(unsigned r=0;r<PGM_REGIONS;r++){pgm_put32(h+76+r*16,pt);pt+=sizes[r]/4096*4;}total=pt;
 buf=malloc(BLOCK);pages=malloc(page_count*4);if(!buf||!pages)FAIL("Not enough PGM conversion memory");
 f=fopen(temp,"wb");if(!f)FAIL("Cannot create PGM cache");owned=1;
 if(fwrite(h,1,256,f)!=256)FAIL("Cannot write PGM header");written=256;
 for(unsigned r=0;r<PGM_REGIONS;r++){
  uint32_t crc=~0u;unsigned pos=0;
  while(pos<sizes[r]){
   const rom *entry=0;unsigned next=sizes[r];for(unsigned i=0;i<sizeof layout/sizeof *layout;i++)if(layout[i].region==r&&layout[i].dest>=pos&&layout[i].dest<next){entry=layout+i;next=entry->dest;}
   unsigned count=entry&&next==pos?entry->size:next-pos;
   if(entry&&next==pos){int m=h1_zip_find(z[entry->bios],entry->name,entry->crc,1);stream=h1_zip_start(z[entry->bios],m);if(!stream)FAIL("Cannot inflate PGM ROM");}
   else entry=0;
   for(unsigned at=0;at<count;){unsigned n=count-at<BLOCK?count-at:BLOCK;
    if(entry){if(!h1_zip_read(stream,buf,n))FAIL("Damaged PGM ZIP member");}else memset(buf,0,n);
    crc=h1_import_crc(crc,buf,n);for(unsigned p=0;p<n;p+=4096)pages[index++]=checksum(buf+p,4096);
    if(fwrite(buf,1,n,f)!=n)FAIL("Cannot write PGM cache");written+=n;at+=n;NOTIFY(r<2?"program":r==5?"sound":"sprites",written,total);
   }
   if(entry){if(!h1_zip_finish(stream)){snprintf(error,128,"PGM ROM CRC failed: %s",entry->name);goto done;}h1_zip_stop(stream);stream=0;}pos+=count;
  }pgm_put32(h+72+r*16,~crc);
 }
 if(index!=page_count||fwrite(pages,4,page_count,f)!=page_count)FAIL("Cannot write PGM page checksums");
 memcpy(h,"H1PGM1\0",8);pgm_put32(h+8,1);pgm_put32(h+12,total);pgm_put32(h+16,PGM_REGIONS);memcpy(h+24,set,strlen(set));pgm_put32(h+252,checksum(h,252));
 if(fseek(f,0,SEEK_SET)||fwrite(h,1,256,f)!=256)FAIL("Cannot finish PGM header");
 {int e=fclose(f);f=0;if(e)FAIL("Cannot close PGM cache");}
 f=fopen(temp,"rb");
 {uint8_t readback[256];if(!f||fread(readback,1,256,f)!=256||memcmp(readback,h,256)||!h1_pgm_header(readback,total,set))FAIL("PGM header readback failed");}
 index=0;for(unsigned r=0;r<PGM_REGIONS;r++)for(unsigned p=0;p<sizes[r];p+=4096){
  if(fread(buf,1,4096,f)!=4096||checksum(buf,4096)!=pages[index++])FAIL("PGM cache readback CRC failed");
  NOTIFY("verify",index,page_count);}
 /* Read back the page table as well as ROM data, before committing. */
 for(unsigned p=0;p<page_count;){unsigned n=page_count-p<BLOCK/4?page_count-p:BLOCK/4;
  if(fread(buf,4,n,f)!=n||memcmp(buf,pages+p,n*4))FAIL("PGM page table readback failed");p+=n;}
 {int e=fclose(f);f=0;if(e)FAIL("Cannot close PGM readback");}
 if(rename(temp,output))FAIL("Cannot commit PGM cache");owned=0;result=1;
 if(notify)notify(user,"done",total,total);
done:
 if(f)fclose(f);h1_zip_stop(stream);h1_zip_close(z[0]);h1_zip_close(z[1]);free(buf);free(pages);
 if(owned&&remove(temp)){snprintf(error,128,"Cannot remove incomplete PGM cache");result=-1;}return result;
 #undef FAIL
 #undef NOTIFY
}
