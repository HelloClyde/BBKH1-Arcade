/* Direct LCD FPS display with a bounded saved background. GPL-2.0-or-later. */
#include "fps_overlay.h"
#include <string.h>

enum {LCD_WIDTH=480,HUD_X=2,HUD_Y=2,HUD_WIDTH=96,HUD_HEIGHT=11};
static uint32_t underneath[HUD_WIDTH*HUD_HEIGHT];
static uint32_t *saved_target;
static unsigned saved,baseline,start_tick,start_frames,start_displayed,tick_hz;
static char label[16]="FPS --.-/--.-";

static void publish(void){
#if defined(__mips__)
 /* The frontend supplies the native uncached LCD alias. Presenters publish
  * their cached writes before returning; only uncached stores happen here. */
 __asm__ volatile("sync":::"memory");
#endif
}

void h1_fps_overlay_before_present(uint32_t *lcd){
 if(saved&&lcd==saved_target){
  for(unsigned y=0;y<HUD_HEIGHT;y++)
   for(unsigned x=0;x<HUD_WIDTH;x++)
    lcd[(HUD_Y+y)*LCD_WIDTH+HUD_X+x]=underneath[y*HUD_WIDTH+x];
  publish();
 }
 saved=0;saved_target=0;
}

void h1_fps_overlay_reset(void){
 baseline=0;memcpy(label,"FPS --.-/--.-",14);
}

static char *rate_text(char *out,unsigned rate){
 unsigned whole=rate/10;
 if(whole>=100)*out++=(char)('0'+whole/100);
 if(whole>=10)*out++=(char)('0'+whole/10%10);
 *out++=(char)('0'+whole%10);*out++='.';*out++=(char)('0'+rate%10);
 return out;
}

static unsigned rate(unsigned delta,unsigned hz,unsigned elapsed){
 uint64_t result=((uint64_t)delta*hz*10+elapsed/2)/elapsed;
 return result>9999?9999:(unsigned)result;
}

void h1_fps_overlay_sample(unsigned tick,unsigned hz,unsigned frames,unsigned displayed){
 /* calibrate() accepts 10..2000 Hz. The raw timer's historical function
  * name is not its frequency: current physical H1 units calibrate to 40 Hz. */
 if(hz<10||hz>2000)hz=40;
 if(!baseline||hz!=tick_hz){
  baseline=1;tick_hz=hz;start_tick=tick;start_frames=frames;start_displayed=displayed;return;
 }
 unsigned elapsed=tick-start_tick;
 if(elapsed<hz)return;
 char *next=label;memcpy(next,"FPS ",4);next+=4;
 next=rate_text(next,rate(frames-start_frames,hz,elapsed));*next++='/';
 next=rate_text(next,rate(displayed-start_displayed,hz,elapsed));*next=0;
 start_tick=tick;start_frames=frames;start_displayed=displayed;
}

const char *h1_fps_overlay_text(void){return label;}

/* Five columns, low bit at the top. Only the characters used by the label
 * are included; this needs neither the large menu font nor heap memory. */
static const uint8_t *glyph(char c){
 static const uint8_t digits[10][5]={
  {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
  {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},
  {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e}
 };
 static const uint8_t letters[7][5]={
  {0x7f,9,9,9,1},{0x7f,9,9,9,6},{0x46,0x49,0x49,0x49,0x31},
  {0,0x60,0x60,0,0},{0x20,0x10,8,4,2},{8,8,8,8,8},{0,0,0,0,0}
 };
 if(c>='0'&&c<='9')return digits[c-'0'];
 switch(c){case 'F':return letters[0];case 'P':return letters[1];case 'S':return letters[2];
  case '.':return letters[3];case '/':return letters[4];case '-':return letters[5];default:return letters[6];}
}

void h1_fps_overlay_draw(uint32_t *lcd,unsigned enabled){
 if(!lcd)return;
 /* Also make repeated calls safe if no new presenter output intervened. */
 h1_fps_overlay_before_present(lcd);
 if(!enabled)return;
 for(unsigned y=0;y<HUD_HEIGHT;y++)for(unsigned x=0;x<HUD_WIDTH;x++){
  uint32_t *pixel=lcd+(HUD_Y+y)*LCD_WIDTH+HUD_X+x;
  underneath[y*HUD_WIDTH+x]=*pixel;*pixel=0;
 }
 for(unsigned i=0;label[i]&&i<15;i++){
  const uint8_t *bits=glyph(label[i]);
  for(unsigned x=0;x<5;x++)for(unsigned y=0;y<7;y++)if(bits[x]&(1u<<y))
   lcd[(HUD_Y+2+y)*LCD_WIDTH+HUD_X+2+i*6+x]=0x00ffffffu;
 }
 saved_target=lcd;saved=1;publish();
}
