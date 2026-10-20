/* KOV ASIC28 HLE adapted from the pinned FBA-A320 pgm_prot.cpp
 * (PGM work by ElSemi, David Haywood, OopsWare and FBA contributors).
 * Upstream terms and provenance are retained in NOTICE/third_party. */
#include "pgm.h"
#include <string.h>
static unsigned key,reg[2],params[256],slots[16],reads;
static const unsigned char ba[64]={
 0x00,0x29,0x2c,0x35,0x3a,0x41,0x4a,0x4e,0x57,0x5e,0x77,0x79,0x7a,0x7b,0x7c,0x7d,
 0x7e,0x7f,0x80,0x81,0x82,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x90,
 0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9e,0xa3,0xd4,0xa9,0xaf,0xb5,0xbb,0xc1};
static const unsigned char b0[16]={2,0,1,4,3};
void h1_pgm_prot_reset(void){key=reg[0]=reg[1]=reads=0;memset(params,0,sizeof params);memset(slots,0,sizeof slots);}
void h1_pgm_prot_write(unsigned offset,unsigned v){
 if(!offset){reg[0]=(v^(key|(key>>8)))&65535;return;}
 key=v&0xff00;reg[1]=(v^(key|(key>>8)))&65535;unsigned cmd=reg[1]&255;params[cmd]=reg[0];
 if(reg[1]==0xe7){unsigned s=(params[0xe7]>>12)&15;slots[s]=(slots[s]&65535)|(reg[0]<<16);}
 if(reg[1]==0xe5){unsigned s=(params[0xe7]>>12)&15;slots[s]=(slots[s]&0xff0000)|reg[0];}
 reads=0;++h1_pgm_stats.protection;h1_pgm_stats.last_command=cmd;
}
unsigned h1_pgm_prot_read(unsigned offset){
 unsigned v=0x880000;
 switch(reg[1]&255){
 case 0x9d:case 0xe0:v=0xa00000+((reg[0]&31)<<6);break;
 case 0xb0:v=b0[reg[0]&15];break;
 case 0xba:v=ba[reg[0]&63];break;
 case 0xb4:{unsigned a=reg[0]&15,b=(reg[0]>>8)&15;if(reg[0]==0x102)slots[1]=slots[0];else slots[b]=slots[a];break;}
 case 0xc3:v=0x904000+(params[0xc0]+params[0xc3]*64)*4;break;
 case 0xcc:{int y=params[0xcc];if(y&0x400)y=-(0x400-(y&0x3ff));v=0x900000+(params[0xcb]+y*64)*4;break;}
 case 0xd0:v=0xa01000+(reg[0]<<5);break;
 case 0xd6:slots[0]=slots[reg[0]&15];break;
 case 0xdc:v=0xa00800+(reg[0]<<6);break;
 case 0xf0:v=0xc000;break;
 case 0xf8:v=slots[reg[0]&15]&0xffffff;break;
 case 0xfc:v=(params[0xfc]*params[0xfe])>>6;break;
 }
 unsigned k=key|(key>>8),result=((offset?v>>16:v)^k)&65535;
 if(offset&&!(++reads&15))key=(key+0x100)&0xff00;return result;
}
