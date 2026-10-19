/* ZIP32 stored/deflate reader, bounded to the supported ROM sets.
 * GPL-2.0-or-later. The included miniz inflater retains its MIT notice. */
#include "import_zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "import_vendor/miniz_tinfl.c"
#define DICT 32768u
#define INPUT 16384u
#define MAX_MEMBERS 512u
typedef struct {char name[96];uint32_t offset,packed,size,crc;unsigned method,flags;} member;
struct h1_zip {FILE *file;char path[256];unsigned bytes,count,central;member *members;};
struct h1_zip_stream {
 FILE *file;member m;tinfl_decompressor inflater;
 unsigned input_pos,input_count,left,dict_pos,read_pos,ready,total,delivered;
 uint32_t crc;int status,failed;
 unsigned char input[INPUT],dict[DICT];
};
static unsigned le16(const unsigned char *p){return p[0]|p[1]<<8;}
static uint32_t le32(const unsigned char *p){return le16(p)|(uint32_t)le16(p+2)<<16;}
static int at(FILE *f,unsigned offset,void *p,unsigned n){return !fseek(f,(long)offset,SEEK_SET)&&fread(p,1,n,f)==n;}
static uint32_t crc_table[256];
uint32_t h1_import_crc(uint32_t c,const void *data,unsigned n){
 const unsigned char *p=data;
 if(!crc_table[1])for(unsigned i=0;i<256;i++){uint32_t v=i;for(unsigned b=0;b<8;b++)v=(v>>1)^((0u-(v&1))&0xedb88320u);crc_table[i]=v;}
 while(n--)c=crc_table[(c^*p++)&255]^(c>>8);return c;
}
static int basename_lower(char *out,const unsigned char *name,unsigned n){
 unsigned start=0;for(unsigned i=0;i<n;i++){if(!name[i])return 0;if(name[i]=='/'||name[i]=='\\')start=i+1;}
 if(n-start>=96)return 0;
 for(unsigned i=start;i<n;i++){unsigned c=name[i];out[i-start]=(char)(c>='A'&&c<='Z'?c+32:c);}out[n-start]=0;return 1;
}
h1_zip *h1_zip_open(const char *path){
 h1_zip *z=calloc(1,sizeof(*z));unsigned char *tail=0,h[46],name[256];
 if(!z||strlen(path)>=sizeof z->path)goto fail;strcpy(z->path,path);
 z->file=fopen(path,"rb");if(!z->file||fseek(z->file,0,SEEK_END))goto fail;
 long length=ftell(z->file);if(length<22||length>128*1024*1024)goto fail;z->bytes=(unsigned)length;
 unsigned n=z->bytes<65557?z->bytes:65557;tail=malloc(n);if(!tail||!at(z->file,z->bytes-n,tail,n))goto fail;
 int e=(int)n-22;for(;e>=0;e--)if(le32(tail+e)==0x06054b50&&e+22+le16(tail+e+20)==n)break;
 if(e<0||le16(tail+e+4)||le16(tail+e+6)||le16(tail+e+8)!=le16(tail+e+10))goto fail;
 z->count=le16(tail+e+10);z->central=le32(tail+e+16);unsigned central_size=le32(tail+e+12);
 if(!z->count||z->count>MAX_MEMBERS||z->central>z->bytes-n+(unsigned)e||central_size!=z->bytes-n+(unsigned)e-z->central)goto fail;
 free(tail);tail=0;z->members=calloc(z->count,sizeof(member));if(!z->members)goto fail;
 unsigned p=z->central;
 for(unsigned i=0;i<z->count;i++){
  if(p>z->central+central_size||z->central+central_size-p<46||!at(z->file,p,h,46)||le32(h)!=0x02014b50)goto fail;
  unsigned fn=le16(h+28),extra=le16(h+30),comment=le16(h+32);member *m=z->members+i;
  if(!fn||fn>255||fn+extra+comment>z->central+central_size-p-46||le16(h+34)||!at(z->file,p+46,name,fn)||!basename_lower(m->name,name,fn))goto fail;
  m->flags=le16(h+8);m->method=le16(h+10);m->crc=le32(h+16);m->packed=le32(h+20);m->size=le32(h+24);m->offset=le32(h+42);
  if((m->flags&~0x080eu)||(m->method!=0&&m->method!=8)||m->size>64*1024*1024||m->offset>z->central||z->central-m->offset<30||m->packed>z->central-m->offset-30)goto fail;
  for(unsigned j=0;j<i;j++)if(m->name[0]&&!strcmp(m->name,z->members[j].name))goto fail;
  p+=46+fn+extra+comment;
 }
 if(p!=z->central+central_size)goto fail;return z;
fail:free(tail);h1_zip_close(z);return 0;
}
void h1_zip_close(h1_zip *z){if(z){if(z->file)fclose(z->file);free(z->members);free(z);}}
int h1_zip_find(h1_zip *z,const char *name,uint32_t crc,int by_crc){
 int candidate=-1;unsigned matches=0;if(!z)return -1;
 for(unsigned i=0;i<z->count;i++){
  member *m=z->members+i;if(!strcmp(m->name,name)&&(!by_crc||m->crc==crc))return (int)i;
  if(by_crc&&m->crc==crc){candidate=(int)i;++matches;}
 }return matches==1?candidate:-1;
}
unsigned h1_zip_size(h1_zip *z,int i){return z&&i>=0&&(unsigned)i<z->count?z->members[i].size:0;}
h1_zip_stream *h1_zip_start(h1_zip *z,int i){
 unsigned char h[30],name[256];char base[96];if(!z||i<0||(unsigned)i>=z->count)return 0;
 member *m=z->members+i;
 if(!at(z->file,m->offset,h,30)||le32(h)!=0x04034b50||le16(h+6)!=m->flags||le16(h+8)!=m->method)return 0;
 unsigned fn=le16(h+26),extra=le16(h+28),start=m->offset+30;
 if(!fn||fn>255||fn+extra>z->central-start||m->packed>z->central-start-fn-extra||!at(z->file,start,name,fn)||!basename_lower(base,name,fn)||strcmp(base,m->name))return 0;
 if(!(m->flags&8)&&(le32(h+14)!=m->crc||le32(h+18)!=m->packed||le32(h+22)!=m->size))return 0;
 if(!m->method&&m->size!=m->packed)return 0;
 h1_zip_stream *s=calloc(1,sizeof(*s));if(!s)return 0;
 s->m=*m;s->left=m->packed;s->crc=~0u;s->status=TINFL_STATUS_NEEDS_MORE_INPUT;s->file=fopen(z->path,"rb");
 if(!s->file||fseek(s->file,start+fn+extra,SEEK_SET)){h1_zip_stop(s);return 0;}
 tinfl_init(&s->inflater);return s;
}
static int produce(h1_zip_stream *s){
 if(s->failed||s->status==TINFL_STATUS_DONE)return 0;
 if(!s->m.method){
  unsigned n=s->left<DICT?s->left:DICT;
  if(fread(s->dict,1,n,s->file)!=n){s->failed=1;return 0;}s->left-=n;s->ready=n;s->read_pos=0;s->total+=n;
  s->crc=h1_import_crc(s->crc,s->dict,n);if(!s->left)s->status=TINFL_STATUS_DONE;return n!=0;
 }
 for(;;){
  if(s->input_pos==s->input_count){s->input_count=s->left<INPUT?s->left:INPUT;s->input_pos=0;
   if(fread(s->input,1,s->input_count,s->file)!=s->input_count){s->failed=1;return 0;}s->left-=s->input_count;}
  size_t in=s->input_count-s->input_pos,out=DICT-s->dict_pos;unsigned begin=s->dict_pos;
  s->status=tinfl_decompress(&s->inflater,s->input+s->input_pos,&in,s->dict,s->dict+s->dict_pos,&out,s->left?TINFL_FLAG_HAS_MORE_INPUT:0);
  s->input_pos+=(unsigned)in;s->dict_pos=(s->dict_pos+(unsigned)out)&(DICT-1);s->read_pos=begin;s->ready=(unsigned)out;
  if(s->status<0||out>s->m.size-s->total||(!in&&!out&&s->status!=TINFL_STATUS_DONE)){s->failed=1;return 0;}
  s->total+=(unsigned)out;s->crc=h1_import_crc(s->crc,s->dict+begin,(unsigned)out);
  if(out)return 1;if(s->status==TINFL_STATUS_DONE)return 0;
 }
}
int h1_zip_read(h1_zip_stream *s,void *out,unsigned n){
 unsigned char *p=out;if(!s||n>s->m.size-s->delivered)return 0;
 while(n){if(!s->ready&&!produce(s))return 0;unsigned k=n<s->ready?n:s->ready;
  memcpy(p,s->dict+s->read_pos,k);p+=k;s->read_pos+=k;s->ready-=k;s->delivered+=k;n-=k;}
 return !s->failed;
}
int h1_zip_finish(h1_zip_stream *s){
 if(!s)return 0;while(s->status!=TINFL_STATUS_DONE&&!s->failed){s->ready=0;if(!produce(s))break;}
 return !s->failed&&s->status==TINFL_STATUS_DONE&&s->total==s->m.size&&~s->crc==s->m.crc&&s->left==0&&s->input_pos==s->input_count;
}
void h1_zip_stop(h1_zip_stream *s){if(s){if(s->file)fclose(s->file);free(s);}}
