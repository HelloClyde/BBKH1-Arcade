/* H1 Neo Geo frontend, GPL-2.0-or-later. */
#include "h1_sdk.h"
#include "h1_port.h"
#include "game_input.h"
#include "memory.h"
#include "emu.h"
#include "pd4990a.h"
#include "timer.h"
#include "audio.h"
#include "m68k.h"
#include "profile.h"
#include "present.h"
#include "arcade_menu.h"
#include "import_game.h"
#include "touch.h"
#if H1_CPU_MHZ > 336
#include "clock_control.h"
#endif
#include "h1_render.h"
#if H1_SPEED_CORE
#include "speed_core.h"
#endif
#if H1_AUDIO_FAST
#include "audio_fast.h"
#define H1_SYNTH_RATE h1_audio_source_rate
#else
#define H1_SYNTH_RATE 22050u
#endif
#if H1_PALETTE32
#include "palette32.h"
#endif
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

volatile h1_arcade_stats_t h1_arcade_stats={0x31435241,1};
const uint32_t h1_arcade_abi[] __attribute__((used,section(".h1meta")))={
 0x31494241,offsetof(neo_mem,ram),offsetof(neo_mem,sram),offsetof(neo_mem,memcard),sizeof(neo_mem)
};
int h1_arcade_jump[12];
struct config conf;
int current_line,nb_interlace=264,neogeo_fix_bank_type;
char fps_str[32];
SDL_Rect visible_area={16,16,320,224};
static SDL_Surface surface;
/* Fixed-size framebuffer belongs in the checked BDA window, not the scarce
 * native heap remaining after a large cartridge and its sprite cache load. */
static uint16_t framebuffer[352*256];
#if H1_PALETTE32
uint32_t framebuffer32[352*256+16];
#endif
SDL_Surface *buffer=&surface;
static unsigned gui_open,held[45],paused,skip=1,display_mode=H1_SCALE_OUTPUT?0:H1_SPEED_CORE?1:0,muted,audio_ready;
volatile unsigned h1_pause_menu_enabled=1;
static unsigned menu_requested,input_release_gate;
#if H1_SPEED_CORE
/* Fixed skip1 is the requested release target: about 30 displayed FPS. */
volatile unsigned h1_auto_frameskip=0;
volatile struct {unsigned magic,version,delayed;} h1_frameskip_stats={0x31534648,1};
#endif
static unsigned pace_start,pace_frames;
static char logs[16384],path[240];
static unsigned logsize;
static unsigned clear_lcd=1;
static uint32_t *lcd(void);
volatile struct {unsigned magic,version,active,done,total,start,end,result;} h1_import_stats={0x31504d49,1};
static unsigned import_last_tick;
static const char *import_last_stage;
static int import_notify(void *user,const char *stage,unsigned done,unsigned total){
 (void)user;int code=-1,key=-1,x,y;h1_event_fetch(&code,&key);
 if(code==H1_EVENT_KEY_DOWN&&(key==24||key==41))return 0;
 if(h1_game_code_down(h1_game_key_code(24))||h1_game_code_down(h1_game_key_code(41)))return 0;
 if(code==11&&h1_touch_position(&x,&y)&&x>=338&&x<442&&y>=236&&y<264)return 0;
 unsigned tick=h1_raw_tick_80hz();h1_import_stats.done=done;h1_import_stats.total=total;
 if(!import_last_stage||strcmp(stage,import_last_stage)||tick-import_last_tick>=4||done==total){
  arcade_import_draw(lcd(),stage,done,total);import_last_stage=stage;import_last_tick=tick;
 }
 return 1;
}
#if H1_PALETTE32
void h1_palette32_surface(unsigned enabled){
 void *pixels=enabled?(void*)framebuffer32:(void*)framebuffer;
 if(surface.pixels!=pixels){surface.pixels=pixels;surface.pitch=enabled?1408:704;
  h1_present_reset();h1_present32_reset();clear_lcd=1;}
}
#endif
extern void FMTimerInit(void);
extern void YM2610_sh_reset(void);
extern int z80_execute(int);
extern int z80_ICount;

void h1_arcade_log(const char *fmt,...){
 if(logsize+300>=sizeof logs)return;
 va_list ap;va_start(ap,fmt);int n=vsnprintf(logs+logsize,sizeof logs-logsize-2,fmt,ap);va_end(ap);
 if(n<0)return;unsigned used=(unsigned)n;if(used>sizeof logs-logsize-2)used=sizeof logs-logsize-2;
 logsize+=used;logs[logsize++]='\n';
}
void h1_arcade_log_flush(void){FILE *f=fopen("A:\\ARCADE\\h1arcade.log","ab");if(f){fwrite(logs,1,logsize,f);fclose(f);logsize=0;}}
void h1_arcade_escape(int error){if(error)h1_arcade_stats.error=error;h1_arcade_longjmp(h1_arcade_jump);}
unsigned h1_arcade_rtc(void){return *(volatile uint32_t*)0xb0003004;}
static void video_open(void){
 typedef int(*fn)(void);fn f=(fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x84c);
 gui_open=f&&f();if(!gui_open)h1_arcade_escape(10);
}
static void video_close(void){
 if(gui_open){typedef int(*fn)(void);fn f=(fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x850);if(f)f();gui_open=0;}
}
static uint32_t *lcd(void){
 typedef volatile uint32_t*(*fn)(uint16_t*,void*);
 fn f=(fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x8f4);
 uint16_t info[6]={0};volatile uint32_t *p=f?f(info,0):0;
 if(!p||info[1]!=480||info[2]!=272||info[3]!=1920||info[5]!=32)h1_arcade_escape(11);
 return (uint32_t*)p;
}
int SDL_SetClipRect(SDL_Surface *s,const SDL_Rect *r){(void)s;(void)r;return 1;}
int SDL_LockSurface(SDL_Surface *s){(void)s;return 0;}
void SDL_UnlockSurface(SDL_Surface *s){(void)s;}
void SDL_textout(SDL_Surface *s,int x,int y,const char *t){(void)s;(void)x;(void)y;(void)t;}
int SDL_FillRect(SDL_Surface *s,const SDL_Rect *r,Uint32 color){
 int x=r?r->x:0,y=r?r->y:0,w=r?r->w:s->w,h=r?r->h:s->h;
 if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}if(x+w>s->w)w=s->w-x;if(y+h>s->h)h=s->h-y;
 for(int j=0;j<h;j++){
#if H1_PALETTE32
  if(s->pitch==1408){uint32_t *p=(uint32_t*)((uint8_t*)s->pixels+(y+j)*s->pitch)+x;for(int i=0;i<w;i++)p[i]=color;continue;}
#endif
  uint16_t *p=(uint16_t*)((uint8_t*)s->pixels+(y+j)*s->pitch)+x;for(int i=0;i<w;i++)p[i]=(uint16_t)color;
 }
 return 0;
}
void screen_flip(void){
 if(!surface.pixels)return;
#if H1_FAST_RENDER || H1_MXU_RENDER
 if(h1_render_bench_only)return;
#endif
 unsigned prof=h1_profile_set(HP_LCD);
#if H1_PALETTE32
 if(surface.pitch==1408)h1_present_rgb32(lcd(),framebuffer32+16*352+16,surface.pitch,display_mode,clear_lcd);
 else
#endif
 h1_present_rgb565(lcd(),(const uint16_t*)((const uint8_t*)surface.pixels+16*surface.pitch)+16,
                   surface.pitch,display_mode,clear_lcd);
 clear_lcd=0;
 ++h1_arcade_stats.displayed;
 h1_profile_set(prof);
}
#if H1_MXU && H1_CPU_SELFTEST
static unsigned pixel_hash(const void *data,unsigned bytes){
 const uint8_t *p=data;unsigned h=2166136261u;
 while(bytes--)h=(h^*p++)*16777619u;return h;
}
#if H1_PALETTE32
unsigned h1_palette32_lcd_hash(void){return pixel_hash(lcd(),480*272*4);}
unsigned h1_palette32_mode(unsigned mode){unsigned old=display_mode;display_mode=mode;clear_lcd=1;return old;}
unsigned h1_palette32_compare_source(void){unsigned n=0;for(unsigned i=0;i<352*256;i++)n+=h1_expand565(framebuffer[i])!=framebuffer32[i];return n;}
#endif
/* Runs in the owning app's pause loop, never in the USB worker/EXEC thread.
 * Full redraw measures conversion/scaling/copy on exactly one frozen frame.
 * Game, VRAM and audio state do not advance. Per-game FPS is measured separately. */
static void present_benchmark(void){
 unsigned loops=h1_present_bench.request,old_mxu=h1_present_mxu,old_dirty=h1_present_dirty;
 unsigned old_direct=h1_present_direct;
 unsigned old_cached=h1_present_cached;
 unsigned old_scale=h1_present_scale;
 h1_present_bench.request=0;h1_present_bench.state=1;
 if(loops<100||loops>800){h1_present_bench.state=3;return;}
 unsigned input=pixel_hash(framebuffer,sizeof framebuffer);
 h1_present_bench.loops=loops*2;h1_present_bench.original_ticks=h1_present_bench.fast_ticks=0;
 h1_present_dirty=0;
 for(unsigned pass=0;pass<4;pass++){
  unsigned fast=pass==1||pass==2;
#if H1_SCALE_OUTPUT
  h1_present_mxu=1;h1_present_direct=1;h1_present_cached=1;h1_present_scale=fast;
#elif H1_SPEED_CORE
  h1_present_mxu=1;h1_present_direct=1;h1_present_cached=fast;
#else
  h1_present_mxu=fast;
#endif
  screen_flip();
  unsigned start=h1_raw_tick_80hz();
  for(unsigned i=0;i<loops;i++){screen_flip();if(!(i&15))h1_arcade_check_stop();}
  unsigned elapsed=h1_raw_tick_80hz()-start,hash=pixel_hash(lcd(),480*272*4);
  if(fast){h1_present_bench.fast_ticks+=elapsed;h1_present_bench.fast_hash=hash;}
  else{h1_present_bench.original_ticks+=elapsed;h1_present_bench.original_hash=hash;}
 }
 h1_present_bench.input_unchanged=input==pixel_hash(framebuffer,sizeof framebuffer);
 h1_present_mxu=old_mxu;h1_present_direct=old_direct;h1_present_cached=old_cached;
 h1_present_scale=old_scale;
 h1_present_dirty=old_dirty;h1_present_reset();screen_flip();
 h1_present_bench.state=2;
}
#endif
#if H1_SPEED_CORE
static uint32_t queried_keys[8],pressed_keys[8];
static int (*query_key)(unsigned);
static unsigned query_calls,query_unique;
volatile struct {unsigned magic,version,scans,queries,reused;} h1_input_stats={0x31504e49,1};
static int down(unsigned key){
 unsigned code=h1_game_key_code(key);if(!code||!query_key)return 0;
 unsigned slot=code>>5,mask=1u<<(code&31);++query_calls;
 if(!(queried_keys[slot]&mask)){
  queried_keys[slot]|=mask;++query_unique;
  if(query_key(code))pressed_keys[slot]|=mask;else pressed_keys[slot]&=~mask;
 }
 return (pressed_keys[slot]&mask)!=0;
}
#else
static int down(unsigned key){return h1_game_code_down(h1_game_key_code(key));}
#endif
void pad_update(void){
#if H1_SPEED_CORE
 /* Cache aliases only within this sample. Keep the authoritative firmware
  * query (including its H1Debug hook) for every distinct native scancode. */
 memset(queried_keys,0,sizeof queried_keys);query_calls=query_unique=0;
 query_key=(int(*)(unsigned))h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x9d8u);
#endif
 for(unsigned i=0;i<64;i++){
  int e=-1,k=-1;h1_event_fetch(&e,&k);if(e==-1&&k==-1)break;
  if(e==11&&h1_pause_menu_enabled){menu_requested=2;paused=1;if(audio_ready)gba_audio_pause();break;}
 }
 // Native game mode does not reliably enqueue matrix-key transitions.
 // Use the same authoritative scan query as the game controls for hotkeys.
 static const unsigned hotkeys[]={H1_KEY_ESCAPE,H1_KEY_BACK,H1_KEY_P,H1_KEY_F,H1_KEY_G,H1_KEY_H
#if H1_AUDIO_FAST
 ,H1_KEY_T
#endif
 };
 for(unsigned i=0;i<sizeof hotkeys/sizeof hotkeys[0];i++){
   unsigned k=hotkeys[i],pressed=down(k),edge=pressed&&!held[k];held[k]=pressed;
   if(edge&&(k==H1_KEY_ESCAPE||k==H1_KEY_BACK))h1_arcade_stats.stop=1;
   if(edge&&k==H1_KEY_P){paused=!paused;if(paused&&h1_pause_menu_enabled)menu_requested=1;if(audio_ready){if(paused)gba_audio_pause();else gba_audio_resume();}}
   if(edge&&k==H1_KEY_F){
#if H1_SPEED_CORE
    if(h1_auto_frameskip){h1_auto_frameskip=0;skip=0;}
    else if(skip<2)++skip;
    else{h1_auto_frameskip=1;skip=1;}
#else
    skip=(skip+1)%3;
#endif
   }
   if(edge&&k==H1_KEY_G){display_mode=(display_mode+1)%3;clear_lcd=1;screen_flip();}
   if(edge&&k==H1_KEY_H)muted=!muted;
#if H1_AUDIO_FAST
   if(edge&&k==H1_KEY_T)h1_audio_quality=(h1_audio_quality+1)%3;
#endif
 }
 unsigned buttons=0;
 for(unsigned k=1;k<=42;k++)if(h1_arcade_settings.mapping[k]&&down(k))buttons|=1u<<(h1_arcade_settings.mapping[k]-1);
 if(input_release_gate){if(!buttons)input_release_gate=0;buttons=0;}
 if(paused||menu_requested)buttons=0;
 memory.intern_p1=(uint8_t)~buttons;memory.intern_p2=255;
 memory.intern_coin=7u&~((buttons>>ARCADE_COIN)&1u);
 memory.intern_start=255u&~((buttons>>ARCADE_START)&1u);
 h1_arcade_stats.keys=buttons&255;
#if H1_SPEED_CORE
 ++h1_input_stats.scans;h1_input_stats.queries+=query_unique;
 h1_input_stats.reused+=query_calls-query_unique;
#endif
}
void h1_arcade_check_stop(void){pad_update();if(h1_arcade_stats.stop)h1_arcade_escape(0);}
unsigned get_ticks(void){return (unsigned)((uint64_t)h1_raw_tick_80hz()*1000000/(h1_arcade_stats.tick_hz?h1_arcade_stats.tick_hz:40));}
static void calibrate(void){
 unsigned r=h1_arcade_rtc(),spins=0;
 while(h1_arcade_rtc()==r&&++spins<60000000){}
 r=h1_arcade_rtc();unsigned a=h1_raw_tick_80hz();spins=0;
 while(h1_arcade_rtc()==r&&++spins<60000000){}
 unsigned hz=h1_raw_tick_80hz()-a;
 if(h1_arcade_rtc()==r+1&&hz>=10&&hz<=2000)h1_arcade_stats.tick_hz=hz;
}
static unsigned audio_fraction,audio_goal,audio_done,audio_target;
Uint16 play_buffer[16384];
void h1_audio_render(unsigned n){
 if(!n)return;unsigned prof=h1_profile_set(HP_YM);YM2610Update_stream(n);
 if(muted)memset(play_buffer,0,n*4);
 if(audio_ready){h1_profile_set(HP_PCM);gba_audio_write((int16_t*)play_buffer,n);}
 audio_done+=n;h1_arcade_stats.audio_samples+=n;
 h1_profile_set(prof);
}
void h1_audio_sync(void){if(audio_target>audio_done)h1_audio_render(audio_target-audio_done);}
void h1_audio_reset(void){audio_fraction=audio_goal=audio_done=audio_target=0;}
static int select_game(void){
#if H1_TEST_FRAMES
 FILE *auto_file=fopen("A:\\ARCADE\\autorun.txt","rb");
 if(auto_file){char *got=fgets(path,sizeof path,auto_file);fclose(auto_file);if(got){path[strcspn(path,"\r\n")]=0;return path[0]!=0;}}
#endif
 char output[512]={0};typedef void(*fn)(const char*,const char*,char*);
 fn f=(fn)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x9ec);
 if(!f)return -1;f("A:\\ARCADE\\","h1neo;zip",output);
 video_open();video_close();
 unsigned n=0;while(n<sizeof output&&output[n])++n;if(!n)return 0;
 if(n>=sizeof path||n<4||output[1]!=':'||(output[2]!='\\'&&output[2]!='/'))return -1;
 memcpy(path,output,n+1);
 if(n>=4&&output[n-4]=='.'&&(output[n-3]=='z'||output[n-3]=='Z')&&(output[n-2]=='i'||output[n-2]=='I')&&(output[n-1]=='p'||output[n-1]=='P')){
  char cache_path[240],bios_path[240],name[16],error[128];
  if(!h1_import_paths(output,cache_path,bios_path,name)){h1_message_box(0,"Unsupported game ZIP name or path too long. See README.","Game ZIP",0);return 0;}
  if(h1_import_cached(cache_path,name)){memcpy(path,cache_path,strlen(cache_path)+1);h1_arcade_log("IMPORT_CACHE path=%s",path);return 1;}
  FILE *bios=fopen(bios_path,"rb");if(bios)fclose(bios);else strcpy(bios_path,"A:\\ARCADE\\neogeo.zip");
  h1_import_stats.active=1;h1_import_stats.start=h1_raw_tick_80hz();import_last_stage=0;
  h1_arcade_log("IMPORT_BEGIN game=%s bios=%s output=%s",output,bios_path,cache_path);h1_arcade_log_flush();
  video_open();int result=h1_import_game(output,bios_path,cache_path,name,import_notify,0,error);video_close();
  h1_import_stats.end=h1_raw_tick_80hz();h1_import_stats.result=(unsigned)result;h1_import_stats.active=0;
  h1_arcade_log("IMPORT_RESULT result=%d ticks=%u error=%s",result,h1_import_stats.end-h1_import_stats.start,error);h1_arcade_log_flush();
  if(result<0){h1_message_box(0,error,"Game ZIP",0);return 0;}
  if(!result)return 0;
  memcpy(path,cache_path,strlen(cache_path)+1);
 }
 return 1;
}
static void run_frames(void){
 unsigned fc=0,cartridge_started=0;int debt=0;
#if H1_SPEED_CORE
 unsigned draw_age=0;
#endif
 pace_start=h1_raw_tick_80hz();pace_frames=0;
 h1_arcade_stats.raw_start=pace_start;h1_arcade_stats.rtc_start=h1_arcade_rtc();
 h1_profile_start();
 for(;;){
  h1_profile_set(HP_INPUT);
  h1_arcade_check_stop();
  if(paused){unsigned start=h1_raw_tick_80hz();h1_neo_save();h1_arcade_log_flush();
   h1_profile_set(HP_WAIT);
   while(paused){
    if(menu_requested){
     h1_arcade_settings.mode=display_mode;h1_arcade_settings.skip=skip;h1_arcade_settings.muted=muted;
     int action=arcade_pause_menu(&h1_arcade_settings,lcd,menu_requested==2);
     menu_requested=0;display_mode=h1_arcade_settings.mode;skip=h1_arcade_settings.skip;muted=h1_arcade_settings.muted;
#if H1_SPEED_CORE
     h1_auto_frameskip=0;
#endif
     if(action==2){h1_arcade_stats.stop=1;h1_arcade_escape(0);}
     /* Menu confirm/held directions cannot leak into the resumed game. */
     for(unsigned k=1;k<=42;k++)held[k]=h1_game_code_down(h1_game_key_code(k));
     input_release_gate=1;h1_present_reset();clear_lcd=1;screen_flip();
     paused=0;if(audio_ready)gba_audio_resume();break;
    }
    h1_arcade_check_stop();++h1_arcade_stats.paused_frames;
#if H1_MXU && H1_CPU_SELFTEST
    if(h1_present_bench.request)present_benchmark();
#endif
#if H1_CPU_SELFTEST && (H1_FAST_RENDER || H1_MXU_RENDER)
    if(h1_render_bench.request)h1_render_benchmark();
#endif
#if H1_PALETTE32
    if(h1_palette32_bench.request)h1_palette32_benchmark();
#endif
#if H1_AUDIO_FAST
    if(h1_audio_bench.request)h1_audio_benchmark();
#endif
   }
   pace_start+=h1_raw_tick_80hz()-start;
  }
  /* BIOS VRAM tests contain arbitrary sprite numbers. Draw them sparingly;
   * all CPU, sound and RTC work still runs. Restore the user's draw interval
   * as soon as BIOS selects the cartridge vectors, including BIOS calls later. */
  if(!cartridge_started&&memory.current_vector){cartridge_started=1;h1_arcade_log("CARTRIDGE_START frame=%u",h1_arcade_stats.frames);}
  unsigned interval=cartridge_started?skip+1:60;
  unsigned draw=(h1_arcade_stats.frames%interval)==0,last_line=16;
#if H1_SPEED_CORE
  draw=++draw_age>=interval;
  /* Prefer every second frame. When DMA shows a shrinking audio lead, defer
   * this display once; render by the third frame. Guest clocks stay intact. */
  if(draw&&cartridge_started&&h1_auto_frameskip&&interval==2&&draw_age<3&&!gba_audio_draw_budget()){
   draw=0;++h1_frameskip_stats.delayed;
  }
  if(draw)draw_age=0;
#endif
#if H1_AUDIO_FAST
  unsigned requested_rate=h1_audio_quality==2?8000:h1_audio_quality?16000:22050;
  if(h1_audio_source_rate!=requested_rate){
   h1_ym_set_rate(requested_rate);gba_audio_source_rate(requested_rate);
   h1_audio_source_rate=requested_rate;audio_fraction=0;conf.sample_rate=requested_rate;
   ++h1_audio_fast_stats.switches;h1_arcade_log("AUDIO_QUALITY source=%u output=32000",requested_rate);
  }
#endif
  audio_fraction+=H1_SYNTH_RATE*16896u;audio_goal=audio_fraction/1000000;audio_fraction%=1000000;
  audio_done=audio_target=0;
  for(unsigned line=0;line<264;line++){
   current_line=line;
   if((memory.vid.irq2control&0x10)&&line==memory.vid.irq2start){
    if(draw&&line>=16){h1_profile_set(HP_DRAW);draw_screen_scanline(last_line-16,line-15,0);last_line=line+1;}
    cpu_68k_interrupt(2);
    if(memory.vid.irq2control&0x80)memory.vid.irq2start+=(memory.vid.irq2pos+3)/384;
   }
   h1_profile_set(HP_68K);debt=cpu_68k_run(768-debt);
   audio_target=audio_goal*(line+1)/264;
   h1_profile_set(HP_Z80);
   cpu_z80_run(256);h1_profile_set(HP_TIMER);my_timer();
  }
  h1_audio_sync();
  h1_profile_set(HP_DRAW);
  if(draw){if(last_line==16)draw_screen();else draw_screen_scanline(last_line-16,240,1);}
  h1_profile_set(HP_OTHER);
  if(++fc>=neogeo_frame_counter_speed){fc=0;++neogeo_frame_counter;}
  pd4990a_addretrace();
  if(memory.vid.irq2control&0x40)memory.vid.irq2start=(memory.vid.irq2pos+3)/384;else memory.vid.irq2start=1000;
  cpu_68k_interrupt(1);
  if(++memory.watchdog>8){h1_arcade_log("WATCHDOG pc=%08x",cpu_68k_getpc());cpu_68k_reset();memory.watchdog=0;}
  ++h1_arcade_stats.frames;++pace_frames;
  h1_arcade_stats.pc=cpu_68k_getpc();h1_arcade_stats.checksum=*(uint16_t*)memory.ram;
  h1_arcade_stats.raw_end=h1_raw_tick_80hz();h1_arcade_stats.rtc_end=h1_arcade_rtc();
  if(H1_TEST_FRAMES&&h1_arcade_stats.frames>=H1_TEST_FRAMES)return;
#if !H1_BENCHMARK
  h1_profile_set(HP_WAIT);
  if(gba_audio_pacing()){while(gba_audio_wait())h1_arcade_check_stop();}
  else if(h1_arcade_stats.tick_hz){unsigned target=(uint64_t)pace_frames*16896*h1_arcade_stats.tick_hz/1000000;
   while(h1_raw_tick_80hz()-pace_start<target)h1_arcade_check_stop();}
#endif
 }
}
int h1_app_main(void){
 volatile unsigned loaded=0;FILE *f=fopen("A:\\ARCADE\\h1arcade.log","wb");if(f)fclose(f);
 h1_arcade_log("H1ARCADE_BEGIN version=0.1.0 NeoGeo core=%s",H1_A68K?"A68K MIPS evaluation":"Musashi C");h1_arcade_log_flush();
#if H1_CPU_SELFTEST
 extern int h1_cpu_selftest(void);
 if(!h1_cpu_selftest()){h1_arcade_log_flush();h1_arcade_stats.error=111;h1_arcade_stats.state=5;return 111;}
#if H1_SPEED_CORE
 if(!h1_z80_hle_selftest()){h1_arcade_log_flush();h1_arcade_stats.error=116;h1_arcade_stats.state=5;return 116;}
 if(!h1_timer_selftest()){h1_arcade_log_flush();h1_arcade_stats.error=117;h1_arcade_stats.state=5;return 117;}
#endif
 h1_arcade_log_flush();
#if H1_FAST_RENDER || H1_MXU_RENDER
 extern int h1_render_selftest(void);
 if(!h1_render_selftest()){h1_arcade_log_flush();h1_arcade_stats.error=112;h1_arcade_stats.state=5;return 112;}
 h1_arcade_log_flush();
#endif
#if H1_PALETTE32
 if(!h1_palette32_selftest()){h1_arcade_log_flush();h1_arcade_stats.error=114;h1_arcade_stats.state=5;return 114;}
 h1_arcade_log_flush();
#endif
#if H1_MXU && H1_CPU_SELFTEST
 if(!h1_mxu_selftest()){h1_arcade_log("MXU_TEST_FAIL cases=%u",h1_mxu_stats.tests);h1_arcade_log_flush();h1_arcade_stats.error=113;h1_arcade_stats.state=5;return 113;}
 h1_arcade_log("MXU_TEST_OK cases=%u all_65536_colors=1",h1_mxu_stats.tests);h1_arcade_log_flush();
#if H1_SCALE_OUTPUT
 if(!h1_scale_selftest()){h1_arcade_log("SCALE_TEST_FAIL");h1_arcade_log_flush();h1_arcade_stats.error=118;h1_arcade_stats.state=5;return 118;}
 h1_arcade_log("SCALE_TEST_OK all_65536_colors=1");h1_arcade_log_flush();
#endif
#endif
#if H1_AUDIO_FAST
 if(!h1_ym_rate_selftest()||!gba_audio_resampler_selftest()){h1_arcade_log("AUDIO_FAST_TEST_FAIL");h1_arcade_log_flush();h1_arcade_stats.error=115;h1_arcade_stats.state=5;return 115;}
 h1_arcade_log("AUDIO_FAST_TEST_OK cases=%u",h1_audio_fast_stats.tests);h1_arcade_log_flush();
#endif
#endif
 h1_arcade_stats.state=1;
 if(!h1_arcade_setjmp(h1_arcade_jump)){
  int selected=select_game();if(selected<=0){if(selected<0)h1_arcade_stats.error=1;goto done;}
  arcade_config_load(&h1_arcade_settings,path,H1_SCALE_OUTPUT?0:H1_SPEED_CORE?1:0);
  display_mode=h1_arcade_settings.mode;skip=h1_arcade_settings.skip;muted=h1_arcade_settings.muted;
  gba_audio_set_volume(h1_arcade_settings.volume);
  h1_arcade_log("SELECTED path=%s",path);h1_arcade_log_flush();
  h1_arcade_stats.state=2;
  if(!h1_neo_load(path))goto done;loaded=1;
  video_open();surface.w=352;surface.h=256;surface.pitch=704;surface.pixels=framebuffer;
  memset(framebuffer,0,sizeof framebuffer);
  conf.sound=1;conf.sample_rate=H1_SYNTH_RATE;conf.system=SYS_ARCADE;conf.country=CTY_JAPAN;
  current_pal=memory.vid.pal_neo[0];current_pc_pal=(Uint32*)memory.vid.pal_host[0];
  current_fix=memory.rom.bios_sfix.p;fix_usage=memory.fix_board_usage;
  init_video();cpu_68k_init();pd4990a_init();cpu_z80_init();YM2610_sh_start();
  bankaddress=memory.rom.cpu_m68k.size>0x100000?0x100000:0;
  memory.vid.irq2start=1000;cpu_68k_reset();h1_neo_save();
  calibrate();h1_audio_reset();audio_ready=gba_audio_open(H1_SYNTH_RATE);
#if H1_CPU_MHZ > 336
  /* Use the same accepted 408 MHz PLL backend as PS1. Stop owned native
   * PCM/DMA before any bus-clock transition; start timing only afterwards. */
  if(!audio_ready)h1_arcade_escape(119);
  gba_audio_pause();
  unsigned clock_active=h1_clock_open(H1_CPU_MHZ);
  h1_arcade_log("CPU_CLOCK requested=%u active=%u state=%u error=%u",H1_CPU_MHZ,clock_active,h1_clock_stats[2],h1_clock_stats[3]);
  if(!clock_active)h1_arcade_escape(119);
  if(!gba_audio_resume())h1_arcade_escape(119);
#endif
  h1_arcade_log("RUN hz=%u audio=%u cache=%u",h1_arcade_stats.tick_hz,audio_ready,h1_arcade_stats.cache_bytes);h1_arcade_log_flush();
  h1_arcade_stats.state=3;run_frames();
 }
done:
 h1_profile_stop();
 h1_arcade_stats.state=4;gba_audio_close();audio_ready=0;
#if H1_CPU_MHZ > 336
 if(!h1_clock_close()){
  /* Avoid persistent save/log work after an unconfirmed clock restoration. */
  h1_arcade_stats.error=120;surface.pixels=0;h1_neo_close();video_close();
  h1_arcade_stats.state=5;return 120;
 }
#endif
 if(loaded){
  h1_arcade_settings.mode=display_mode;h1_arcade_settings.skip=skip;h1_arcade_settings.muted=muted;
  h1_menu_stats.save_ok=arcade_config_save(&h1_arcade_settings);h1_neo_save();
 }
 surface.pixels=0;h1_neo_close();video_close();
 h1_arcade_log("RESULT error=%u frames=%u displayed=%u pc=%08x hits=%u misses=%u samples=%u hz=%u",h1_arcade_stats.error,h1_arcade_stats.frames,h1_arcade_stats.displayed,h1_arcade_stats.pc,h1_arcade_stats.cache_hits,h1_arcade_stats.cache_misses,h1_arcade_stats.audio_samples,h1_arcade_stats.tick_hz);
 h1_arcade_log_flush();
#if !H1_TEST_FRAMES
 if(h1_arcade_stats.error)h1_message_box(0,"Cannot load game. See A:\\ARCADE\\h1arcade.log","H1 Arcade",0);
#endif
 h1_arcade_stats.state=5;return h1_arcade_stats.error;
}
