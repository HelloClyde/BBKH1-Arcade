"""Execute actual C FPS arithmetic and LCD drawing; no guest CPU/device model."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess

ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''#include "fps_overlay.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define N (480*272)
static uint32_t guarded[N+2],reference[N],second[N];
static unsigned samples;
static void sample(unsigned tick,unsigned hz,unsigned frames,unsigned displayed,const char *want){
 h1_fps_overlay_sample(tick,hz,frames,displayed);
 assert(!strcmp(h1_fps_overlay_text(),want));++samples;
}
int main(void){
 uint32_t *lcd=guarded+1;guarded[0]=0x12345678;guarded[N+1]=0x89abcdef;
 /* Actual 40 Hz calibration: one second, then a two-second sample window. */
 h1_fps_overlay_reset();sample(100,40,1000,500,"FPS --.-/--.-");
 sample(139,40,1060,530,"FPS --.-/--.-");sample(140,40,1060,530,"FPS 60.0/30.0");
 sample(220,40,1179,590,"FPS 59.5/30.0");
 /* Menu time and redraw frames precede the new baseline, so do not count. */
 h1_fps_overlay_reset();sample(9999,40,1179,594,"FPS --.-/--.-");
 sample(10039,40,1239,624,"FPS 60.0/30.0");
 /* Both the raw timer and monotonically increasing counters can wrap. */
 h1_fps_overlay_reset();sample(UINT32_MAX-19,40,UINT32_MAX-29,UINT32_MAX-14,"FPS --.-/--.-");
 sample(20,40,30,15,"FPS 60.0/30.0");
 /* A different calibrated rate resets the baseline instead of hardcoding 40. */
 sample(100,80,30,15,"FPS 60.0/30.0");sample(180,80,90,45,"FPS 60.0/30.0");
 h1_fps_overlay_reset();sample(0,0,0,0,"FPS --.-/--.-");sample(40,0,1000000,1000000,"FPS 999.9/999.9");
 for(unsigned i=0;i<N;i++)lcd[i]=reference[i]=i*1664525u+1013904223u;
 h1_fps_overlay_draw(lcd,0);assert(!memcmp(lcd,reference,sizeof reference));
 h1_fps_overlay_draw(lcd,1);unsigned changed=0;
 for(unsigned y=0;y<272;y++)for(unsigned x=0;x<480;x++)if(lcd[y*480+x]!=reference[y*480+x]){
  changed++;assert(y>=2&&y<13&&x>=2&&x<98);
 }
 assert(changed>700);assert(guarded[0]==0x12345678&&guarded[N+1]==0x89abcdef);
 h1_fps_overlay_before_present(lcd);assert(!memcmp(lcd,reference,sizeof reference));
 /* Restore before dirty output; pixels which the presenter leaves alone
  * must remain the underlying game, rather than the previous HUD. */
 for(unsigned i=4*480+4;i<4*480+20;i++)lcd[i]=reference[i]=0x12345;
 h1_fps_overlay_draw(lcd,1);h1_fps_overlay_draw(lcd,0);assert(!memcmp(lcd,reference,sizeof reference));
 h1_fps_overlay_draw(lcd,1);h1_fps_overlay_before_present(lcd);
 /* A menu can overwrite all pixels after restore. Sampling reset cannot
  * cause an old saved game rectangle to reappear over the menu. */
 memset(lcd,0x5a,sizeof reference);h1_fps_overlay_reset();h1_fps_overlay_before_present(lcd);
 for(unsigned i=0;i<N;i++)assert(lcd[i]==0x5a5a5a5a);
 memcpy(reference,lcd,sizeof reference);h1_fps_overlay_draw(lcd,1);
 memcpy(second,reference,sizeof second);h1_fps_overlay_before_present(second);
 assert(!memcmp(second,reference,sizeof reference));
 assert(guarded[0]==0x12345678&&guarded[N+1]==0x89abcdef);
 printf("FPS_OVERLAY_RESULT {\"sample_cases\":%u,\"lcd_pixels\":%u,\"hud_changed_pixels\":%u}\n",samples,N,changed);
 return 0;
}
'''

def main():
 out=ROOT/'build/fps-overlay-host';out.mkdir(parents=True,exist_ok=True)
 harness=out/'test.c';harness.write_text(HARNESS,encoding='utf-8')
 cc=Path(os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc'))
 env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
 exe=out/'test.exe'
 subprocess.run([str(cc),'-std=c99','-O2','-Wall','-Wextra','-Werror',
                 '-I'+str(ROOT/'src'),str(ROOT/'src/fps_overlay.c'),str(harness),'-o',str(exe)],
                check=True,cwd=ROOT,env=env)
 result=subprocess.run([str(exe)],check=True,cwd=ROOT,env=env,capture_output=True,text=True)
 counts=json.loads(next(line.removeprefix('FPS_OVERLAY_RESULT ') for line in result.stdout.splitlines()
                        if line.startswith('FPS_OVERLAY_RESULT ')))
 sources=('src/fps_overlay.c','src/fps_overlay.h','tests/fps_overlay_host.py')
 report={'ok':True,**counts,'lcd_size':[480,272],'hud_bounds':[2,2,96,11],
         'scope':'Actual C: calibrated 40/80 Hz sampling, one/two-second windows, paused/menu baseline reset, timer/frame-counter wrap, rate formatting and upper clamp, framebuffer guards, bounded HUD drawing, disabled output, exact underlying pixel restoration across dirty output, repeated draw/disable, menu overwrite, and changed target. No guest CPU or firmware execution, no physical device access.',
         'stdout':result.stdout,'source_sha256':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources}}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 print(result.stdout,end='')

if __name__=='__main__':main()
