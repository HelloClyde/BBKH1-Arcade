#ifndef H1_IMPORT_PATH_H
#define H1_IMPORT_PATH_H
#include <string.h>

static inline unsigned h1_zip_path_stem(const char *path,unsigned *base){
 unsigned length=(unsigned)strlen(path),start=0;
 if(length<8||length>233||path[1]!=':'||(path[2]!='\\'&&path[2]!='/'))return 0;
 for(unsigned i=0;i<length;i++){
  unsigned ch=(unsigned char)path[i];
  if(ch>=0x81&&ch<=0xfe){
   unsigned trail=(unsigned char)path[++i];
   if(i>=length||trail<0x40||trail>0xfe||trail==0x7f)return 0;
  }else if(ch=='\\'||ch=='/')start=i+1;
 }
 unsigned end=length-4;
 if(start>=end||path[end]!='.'||(path[end+1]|32)!='z'||(path[end+2]|32)!='i'||(path[end+3]|32)!='p')return 0;
 *base=start;return end;
}

static inline unsigned h1_zip_path_name(const char *path,unsigned *base,char set[16]){
 unsigned start,end=h1_zip_path_stem(path,&start);
 if(!end)return 0;
 unsigned short_end=start;
 while(short_end<end&&(unsigned char)path[short_end]<0x80)short_end++;
 unsigned size=short_end-start;
 if(!size||size>=16)return 0;
 if(short_end<end&&((unsigned char)path[short_end]<0x81||(unsigned char)path[short_end]>0xfe||short_end+1>=end))return 0;
 for(unsigned i=0;i<size;i++){
  unsigned ch=(unsigned char)path[start+i];
  set[i]=(char)(ch>='A'&&ch<='Z'?ch+32:ch);
 }
 set[size]=0;*base=start;return end;
}
#endif
