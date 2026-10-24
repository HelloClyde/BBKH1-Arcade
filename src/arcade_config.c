/* Two CRC-checked preference slots; a failed write retains the older slot.
 * Input mapping compares native scancodes so physical aliases cannot bind
 * the same simultaneous-key query to conflicting game buttons.
 * GPL-2.0-or-later. */
#include "arcade_config.h"
#include "game_input.h"
#include <stdio.h>
#include <string.h>
arcade_settings h1_arcade_settings;
unsigned h1_config_sequence,h1_config_writable;
static char config_path[260];
typedef struct {uint32_t magic,version,sequence,mode,skip,volume,muted;uint8_t mapping[45],padding[3];uint32_t crc;} config_disk;
static uint32_t checksum(const void *data,unsigned n){
 const uint8_t *p=data;uint32_t c=~0u;
 while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
 return ~c;
}
int arcade_key_reserved(unsigned k){
 return !h1_game_key_code(k)||k==H1_KEY_F||k==H1_KEY_G||k==H1_KEY_H||k==H1_KEY_T;
}
void arcade_mapping_defaults(arcade_settings *s){
 static const uint8_t keys[ARCADE_BUTTONS]={H1_KEY_UP,H1_KEY_DOWN,H1_KEY_LEFT,H1_KEY_RIGHT,H1_KEY_Z,H1_KEY_X,H1_KEY_C,H1_KEY_V,H1_KEY_SPACE,H1_KEY_ENTER};
 memset(s->mapping,0,sizeof s->mapping);
 for(unsigned b=0;b<ARCADE_BUTTONS;b++)s->mapping[keys[b]]=(uint8_t)(b+1);
}
int arcade_map_button(arcade_settings *s,unsigned button,unsigned key){
 if(button>=ARCADE_BUTTONS||arcade_key_reserved(key))return 0;
 unsigned code=h1_game_key_code(key);
 for(unsigned k=1;k<45;k++)if(s->mapping[k]==button+1||h1_game_key_code(k)==code)s->mapping[k]=0;
 s->mapping[key]=(uint8_t)(button+1);return 1;
}
static int valid(const config_disk *d){
 if(d->magic!=0x32434741||(d->version!=1&&d->version!=2)||!d->sequence||d->mode>2||d->skip>2||d->volume>100||d->muted>1||d->mapping[0]||
    (d->version==1?d->padding[0]!=0:d->padding[0]>1)||d->padding[1]||d->padding[2]||checksum(d,sizeof(*d)-4)!=d->crc)return 0;
 uint32_t codes[8]={0};unsigned buttons=0;
 for(unsigned k=1;k<45;k++)if(d->mapping[k]){
  unsigned b=d->mapping[k]-1,code=h1_game_key_code(k);
  if(b>=ARCADE_BUTTONS||arcade_key_reserved(k)||(buttons&(1u<<b))||(codes[code>>5]&(1u<<(code&31))))return 0;
  buttons|=1u<<b;codes[code>>5]|=1u<<(code&31);
 }
 return 1;
}
void arcade_config_load(arcade_settings *s,const char *rom,unsigned default_mode){
 memset(s,0,sizeof *s);s->mode=default_mode;s->skip=1;s->volume=10;arcade_mapping_defaults(s);
 h1_config_sequence=0;h1_config_writable=0;config_path[0]=0;
 if(strlen(rom)+6>=sizeof config_path)return;
 strcpy(config_path,rom);strcat(config_path,".cfg");
 unsigned existed=0;
 for(unsigned slot=0;slot<2;slot++){
  char name[264];snprintf(name,sizeof name,"%s%u",config_path,slot);
  FILE *f=fopen(name,"rb");if(!f)continue;existed=1;
  config_disk d;int ok=fread(&d,1,sizeof d,f)==sizeof d&&fgetc(f)==EOF;fclose(f);
  if(ok&&valid(&d)&&(!h1_config_sequence||(int32_t)(d.sequence-h1_config_sequence)>0)){
   h1_config_sequence=d.sequence;s->mode=d.mode;s->skip=d.skip;s->volume=d.volume;s->muted=d.muted;s->show_fps=d.version>=2?d.padding[0]:0;
   memcpy(s->mapping,d.mapping,sizeof s->mapping);
  }
 }
 h1_config_writable=!existed||h1_config_sequence!=0;
}
int arcade_config_save(const arcade_settings *s){
 if(!h1_config_writable||!config_path[0]||s->show_fps>1)return 0;
 config_disk d;memset(&d,0,sizeof d);d.magic=0x32434741;d.version=2;d.sequence=h1_config_sequence+1;
 if(!d.sequence)d.sequence=1;
 d.mode=s->mode;d.skip=s->skip;d.volume=s->volume;d.muted=s->muted;d.padding[0]=(uint8_t)s->show_fps;memcpy(d.mapping,s->mapping,sizeof d.mapping);
 d.crc=checksum(&d,sizeof d-4);if(!valid(&d))return 0;
 char name[264];snprintf(name,sizeof name,"%s%u",config_path,d.sequence&1);
 FILE *f=fopen(name,"wb");int ok=f&&fwrite(&d,1,sizeof d,f)==sizeof d;
 if(f&&fclose(f))ok=0;
 if(ok){config_disk check;f=fopen(name,"rb");ok=f&&fread(&check,1,sizeof check,f)==sizeof check&&fgetc(f)==EOF&&!memcmp(&d,&check,sizeof d);if(f)fclose(f);}
 if(ok)h1_config_sequence=d.sequence;
 return ok;
}
