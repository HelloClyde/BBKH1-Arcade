/* GBA-style pause menu, direct LCD drawing without a second full framebuffer.
 * Native coordinates and menu interaction follow the existing H1 GBA port.
 * GPL-2.0-or-later; font subset is OFL-1.1, see assets/licenses/.
 */
#include "arcade_menu.h"
#include "h1_port.h"
#include "game_input.h"
#include "touch.h"
#include "audio.h"
#include "menu_font.h"
#include <stdio.h>
#include <string.h>
volatile arcade_menu_stats h1_menu_stats={0x31554e4d,1};
static uint32_t *canvas;
static void rect(int x,int y,int w,int h,uint32_t color){
 for(int r=y;r<y+h&&r<272;r++)for(int c=x;c<x+w&&c<480;c++)if(r>=0&&c>=0)canvas[r*480+c]=color;
}
static void text(const char *s,int x,int y,uint32_t color){
 while(*s&&x<470){
  unsigned cp=(uint8_t)*s++;
  if(cp>=0xe0){cp=(cp&15)<<12;cp|=((uint8_t)*s++&63)<<6;cp|=(uint8_t)*s++&63;}
  else if(cp>=0xc0){cp=(cp&31)<<6;cp|=(uint8_t)*s++&63;}
  unsigned advance=cp<128?10:20;
  for(unsigned i=0;i<MENU_GLYPH_COUNT;i++)if(menu_glyphs[i].code==cp){
   advance=menu_glyphs[i].advance;
   for(unsigned r=0;r<21;r++)for(unsigned c=0;c<18;c++)if(menu_glyphs[i].rows[r]&(1u<<c))rect(x+c,y+r,1,1,color);
   break;
  }
  x+=advance;
 }
}
void arcade_import_draw(uint32_t *target,const char *stage,unsigned done,unsigned total){
 const char *label=!strcmp(stage,"sprites")?"解码图形":!strcmp(stage,"sound")?"解压声音":
  !strcmp(stage,"fixed")?"解码文字":!strcmp(stage,"verify")?"校验文件":
  !strcmp(stage,"program")?"解压程序":!strcmp(stage,"padding")?"准备文件":!strcmp(stage,"done")?"准备完成":"检查游戏文件";
 unsigned percent=total?(unsigned)((uint64_t)done*100/total):0;if(percent>100)percent=100;
 char line[80];canvas=target;rect(0,0,480,272,0x00101818);
 text("首次准备游戏",18,12,0x00ffffff);
 text("首次需要等待，之后直接进入游戏",18,52,0x00b8bcbc);
 snprintf(line,sizeof line,"%s  %u%%",label,percent);text(line,38,108,0x00ffffff);
 rect(38,150,404,18,0x00202440);rect(38,150,404*percent/100,18,0x0000b4a0);
 text("返回取消，原始文件会保留",18,202,0x00b8bcbc);
 rect(338,236,104,28,0x00202440);text("取消",366,239,0x00ffffff);
}
static const char *key_names[45]={"未设置","Q","W","E","R","T","Y","U","A","S","D","F","G","H","J","上页",
 "Z","X","C","V","B","N","下页","空格","退出","回车","左ALT","下","右ALT","I","O","P","K","L","符号","右","M","上","删除","确认","左","返回","SHIFT","FN","电源"};
static const char *button_names[10]={"上","下","左","右","A","B","C","D","投币","开始"};
static unsigned mapped_key(const arcade_settings *s,unsigned b){for(unsigned k=1;k<45;k++)if(s->mapping[k]==b+1)return k;return 0;}
static int count(unsigned page){return page==0?6:page==1?12:page==4?1:4;}
static void box(unsigned page,int i,int *x,int *y,int *w,int *h){
 if(page==4){*x=338;*y=240;*w=104;*h=28;}
 else if(page==0){*x=18+(i%2)*230;*y=56+(i/2)*56;*w=214;*h=44;}
 else if(page==1){*x=18+(i%2)*230;*y=i<10?54+(i/2)*32:226;*w=214;*h=i<10?28:30;}
 else{*x=38;*y=56+i*40;*w=404;*h=34;}
}
static int hit(unsigned page,int x,int y){
 for(int i=0;i<count(page);i++){int bx,by,w,h;box(page,i,&bx,&by,&w,&h);if(x>=bx&&x<bx+w&&y>=by&&y<by+h)return i;}
 return -1;
}
static int volume_hit(int x,int y){return x>=26&&x<454&&y>=214&&y<250;}
static unsigned volume_at(int x){int p=x-38;if(p<0)p=0;if(p>403)p=403;return ((p*20+201)/403)*5;}
static void draw(unsigned page,int cursor,int capture,const char *status,const arcade_settings *s,uint32_t *(*target)(void)){
 static const char *titles[]={"游戏已暂停","按键映射","显示设置","声音设置","关于街机模拟器"};
 static const char *root[]={"继续游戏","按键映射","显示设置","声音设置","退出游戏","关于"};
 static const char *modes[]={"保持4:3比例","原始320x224","全屏拉伸"};
 char label[128];canvas=target();rect(0,0,480,272,0x00101818);text(titles[page],18,8,0x00ffffff);
 if(page==3&&capture<0&&!(status&&*status)){
  snprintf(label,sizeof label,"音量：%u%%  左右键／触摸调节",s->volume);text(label,18,32,0x00b8bcbc);
  rect(38,226,404,12,0x00202440);rect(38,226,404*s->volume/100,12,0x0000b4a0);
  rect(38+403*s->volume/100-4,222,9,20,0x00ffffff);
 }else text(capture>=0?"请按实体键，返回取消":status&&*status?status:page==4?"确认／返回回到暂停菜单":"触摸选择，方向键／确认，返回继续",18,32,0x00b8bcbc);
 if(page==0)text("触屏／P 打开菜单，返回继续",18,232,0x00b8bcbc);
 if(page==4){
  text("作者：HelloClyde",38,64,0x00ffffff);
  text("核心：GnGeo / A68K",38,96,0x00ffffff);
  text("声音：Z80 / YM2610",38,128,0x00ffffff);
  text("H1 / Y100 V1.41",38,160,0x00ffffff);
  text("Thanks: GnGeo, A68K, H1 SDK",38,194,0x00b8bcbc);
 }
 for(int i=0;i<count(page);i++){
  const char *title=label;
  if(!page)title=root[i];
  else if(page==1){if(i<10)snprintf(label,sizeof label,"%s：%s",button_names[i],key_names[mapped_key(s,i)]);else title=i==10?"恢复默认按键":"返回";}
  else if(page==2){
   if(i==0)snprintf(label,sizeof label,"画面：%s",modes[s->mode]);
   if(i==1)snprintf(label,sizeof label,"跳帧：%u（0 为关闭）",s->skip);
   if(i==2)title="恢复默认显示";if(i==3)title="返回";
  }else if(page==3){
   if(i==0)snprintf(label,sizeof label,"声音：%s",s->muted?"关":"开");
   if(i==1)title="减小音量（-5%）";if(i==2)title="增大音量（+5%）";if(i==3)title="返回";
  }else title="返回";
  int x,y,w,h;box(page,i,&x,&y,&w,&h);rect(x,y,w,h,i==cursor?0x0000b4a0:0x00202440);text(title,x+12,y+(h-20)/2,0x00ffffff);
 }
#ifdef __mips__
 __asm__ volatile("sync":::"memory");
#endif
 h1_menu_stats.page=page;h1_menu_stats.cursor=cursor;h1_menu_stats.capture=(unsigned)capture;
}
int arcade_pause_menu(arcade_settings *s,uint32_t *(*target)(void),unsigned opening_touch){
 unsigned page=0,discard_pen=opening_touch,last_scan=~0u;int cursor=0,capture=-1,dirty=1,pen=-1,drag=0,last_x=0,last_y=0,result=0;
 uint8_t held[256]={0};
 for(unsigned k=1;k<=42;k++){unsigned code=h1_game_key_code(k);if(code)held[code]=(uint8_t)h1_game_code_down(code);}
 const char *status=h1_config_writable?"":"设置文件损坏，本次不写入";
 h1_menu_stats.active=1;++h1_menu_stats.opened;h1_arcade_log("PAUSE_MENU_BEGIN");
 for(;;){
  if(h1_arcade_stats.stop){result=2;break;}
  ++h1_arcade_stats.paused_frames;
  if(dirty){draw(page,cursor,capture,status,s,target);dirty=0;}
  int code=-1,key=-1,activate=0,back=0,x=0,y=0;
  h1_event_fetch(&code,&key);
  if(discard_pen){if(code==8)discard_pen=0;if(code==11||code==8)continue;}
  if(drag){
   if(h1_touch_position(&x,&y)){unsigned v=volume_at(x);if(v!=s->volume){s->volume=v;gba_audio_set_volume(v);dirty=1;}}
   if(code==8){drag=0;pen=-1;}continue;
  }
  if(code==11){
   ++h1_menu_stats.touches;
   if(h1_touch_position(&last_x,&last_y)){
    if(page==3&&volume_hit(last_x,last_y)){drag=1;pen=-1;s->volume=volume_at(last_x);gba_audio_set_volume(s->volume);status="";dirty=1;continue;}
    pen=hit(page,last_x,last_y);if(pen>=0&&capture<0){cursor=pen;dirty=1;}
   }else pen=-1;
  }else if(code==8){
   if(h1_touch_position(&x,&y)){last_x=x;last_y=y;}
   if(pen>=0&&capture<0&&hit(page,last_x,last_y)==pen){cursor=pen;activate=1;}pen=-1;
  }else if(pen>=0&&h1_touch_position(&x,&y)){last_x=x;last_y=y;}
  int pressed=0;
  if((code==H1_EVENT_KEY_DOWN||code==H1_EVENT_KEY_UP)&&key>=1&&key<=42){
   unsigned native=h1_game_key_code((unsigned)key);pressed=code==H1_EVENT_KEY_DOWN&&!held[native];held[native]=code==H1_EVENT_KEY_DOWN;
  }else if(code!=11&&code!=8){
   unsigned now=h1_raw_tick_80hz();
   if(now!=last_scan){
    last_scan=now;uint8_t seen[256]={0};
    for(unsigned k=1;k<=42;k++){
     unsigned native=h1_game_key_code(k);if(!native||seen[native])continue;seen[native]=1;
     unsigned down=h1_game_code_down(native);
     if(down&&!held[native]&&!pressed){key=(int)(native==105?H1_KEY_LEFT:native==106?H1_KEY_RIGHT:native==28?H1_KEY_ENTER:k);pressed=1;}
     held[native]=(uint8_t)down;
    }
   }
  }
  if(pressed){
   back=key==H1_KEY_BACK||key==H1_KEY_ESCAPE||key==H1_KEY_P;
   if(capture>=0){
    if(back){capture=-1;status="已取消";}
    else if(arcade_map_button(s,(unsigned)capture,(unsigned)key)){capture=-1;status="按键已修改";++h1_menu_stats.edits;}
    else status="此键用于菜单，请选择其他键";
    dirty=1;continue;
   }
   activate=key==H1_KEY_CONFIRM||key==H1_KEY_ENTER||key==H1_KEY_Z;
   if(page==3&&(key==H1_KEY_LEFT||key==H1_KEY_RIGHT)){
    s->volume=key==H1_KEY_LEFT?(s->volume>5?s->volume-5:0):(s->volume<95?s->volume+5:100);
    gba_audio_set_volume(s->volume);status="";dirty=1;continue;
   }
   int delta=key==H1_KEY_LEFT?-1:key==H1_KEY_RIGHT?1:key==H1_KEY_UP?(page<=1?-2:-1):key==H1_KEY_DOWN?(page<=1?2:1):0;
   if(delta){cursor=(cursor+delta+count(page))%count(page);dirty=1;}
  }
  if(back){if(!page)break;page=0;cursor=0;capture=-1;pen=-1;status="";dirty=1;}
  if(!activate||capture>=0)continue;
  pen=-1;status="";dirty=1;
  if(!page){
   if(cursor==0)break;if(cursor==4){result=2;break;}
   page=cursor==1?1:cursor==2?2:cursor==3?3:4;cursor=0;
  }else if(page==1){
   if(cursor<10)capture=cursor;
   else if(cursor==10){arcade_mapping_defaults(s);status="已恢复默认按键";++h1_menu_stats.edits;}
   else{page=0;cursor=1;}
  }else if(page==2){
   if(cursor==0)s->mode=(s->mode+1)%3;if(cursor==1)s->skip=(s->skip+1)%3;
   if(cursor==2){s->mode=0;s->skip=1;}if(cursor==3){page=0;cursor=2;}
  }else if(page==3){
   if(cursor==0)s->muted=!s->muted;
   if(cursor==1)s->volume=s->volume>5?s->volume-5:0;if(cursor==2)s->volume=s->volume<95?s->volume+5:100;
   if(cursor==3){page=0;cursor=3;}gba_audio_set_volume(s->volume);
  }else{page=0;cursor=5;}
 }
 h1_menu_stats.save_ok=result==2?0:(unsigned)arcade_config_save(s);
 h1_menu_stats.action=(unsigned)result;h1_menu_stats.active=0;++h1_menu_stats.closed;
 h1_arcade_log("PAUSE_MENU_END action=%u mode=%u skip=%u volume=%u muted=%u",result,s->mode,s->skip,s->volume,s->muted);
 return result;
}
