"""Actual C synthesis and streaming PCM arithmetic; no guest CPU or device model."""
from pathlib import Path
import argparse,ctypes,hashlib,json,math,os,random,shutil,struct,subprocess
ROOT=Path(__file__).resolve().parents[1]
def trunc(v,d):return -(abs(v)//d) if v<0 else v//d
def oracle(pairs,rate):
 mono=[trunc(a+b,2) for a,b in pairs];duration=len(mono)*32000;out=[]
 for start in range(0,duration-rate+1,rate):
  end=start+rate;value=0;k=start//32000
  while k*32000<end:
   value+=mono[k]*(min(end,(k+1)*32000)-max(start,k*32000));k+=1
  out.append(trunc(value,rate))
 start=len(out)*rate;value=0;k=start//32000
 while k<len(mono):value+=mono[k]*((k+1)*32000-max(start,k*32000));k+=1
 return out,duration%rate,value
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--build',required=True);args=ap.parse_args()
 build=ROOT/'build'/args.build;out=ROOT/'build/audio-fast-host';out.mkdir(parents=True,exist_ok=True)
 for name in ('SDL.h','SDL_endian.h'):shutil.copyfile(ROOT/'src/include'/name,out/name)
 (out/'h1_sdk.h').write_text('''#ifndef HOST_H1_SDK
#define HOST_H1_SDK
#include <stdint.h>
#define H1_RUNTIME_SYS_TABLE_SLOT 0
static inline void *h1_runtime_table(unsigned x){return 0;}
static inline void *h1_runtime_entry(void *p,unsigned x){return 0;}
static inline unsigned h1_raw_tick_80hz(void){return 0;}
#endif
''')
 common='''#include <stdint.h>
#include <string.h>
#include <stdarg.h>
void h1_arcade_log(const char *fmt,...){(void)fmt;}
void h1_arcade_check_stop(void){}
void h1_audio_sync(void){}
double timer_get_time(void){return 0;}
'''
 pcm=common+'#include "'+(ROOT/'src/audio.c').as_posix()+'"\n'+'''
void reset_pcm(unsigned hz){active=1;initialized=paused=playing=queued=used=phase=accumulator=0;rate=hz;memset(samples,0xa5,sizeof samples);}
unsigned feed_pcm(const int16_t *p,unsigned n,int16_t *out,unsigned *state){
 gba_audio_write(p,n);memcpy(out,samples[0],used*2);
 state[0]=phase;state[1]=accumulator;state[2]=queued;state[3]=failures;
 unsigned count=used;used=queued=0;memset(samples,0xa5,sizeof samples);return count;
}
'''
 (out/'pcm.c').write_text(pcm)
 source=(build/'core/ym2610/ym2610.c').read_text();source=source[:source.index('void YM2610Update(int *p)')]
 ym=common+'uint16_t play_buffer[16384];\n'+source+'\n#include "ym_audio_fast.h"\n'+'''
static unsigned char fixture_rom[4096];
void init_tone(unsigned rate,unsigned fm){
 YM2610Init(8000000,22050,fixture_rom,sizeof fixture_rom,fixture_rom,sizeof fixture_rom,0,0);
 if(fm){
  for(unsigned slot=0;slot<4;slot++){
   unsigned off=slot*4+1;h1_ym_reg(0x30+off,1);h1_ym_reg(0x40+off,0);
   h1_ym_reg(0x50+off,31);h1_ym_reg(0x60+off,0);h1_ym_reg(0x70+off,0);h1_ym_reg(0x80+off,15);
  }
  h1_ym_reg(0xb1,7);h1_ym_reg(0xb5,0xc0);h1_ym_reg(0xa5,0x22);h1_ym_reg(0xa1,0x64);h1_ym_reg(0x28,0xf1);
 }else{h1_ym_reg(0,113);h1_ym_reg(1,2);h1_ym_reg(7,0x3e);h1_ym_reg(8,15);}
 h1_ym_set_rate(rate);
}
void tone(unsigned n,int16_t *out){YM2610Update_stream(n);memcpy(out,play_buffer,n*4);}
unsigned adpcm_position(unsigned rate){
 YM2610Init(8000000,22050,fixture_rom,sizeof fixture_rom,fixture_rom,sizeof fixture_rom,0,0);
 h1_ym_reg(0x101,0);h1_ym_reg(0x108,0xc0);h1_ym_reg(0x110,0);h1_ym_reg(0x118,0);h1_ym_reg(0x120,15);h1_ym_reg(0x128,0);
 h1_ym_reg(0x100,1);h1_ym_set_rate(rate);
 for(unsigned i=0;i<rate/100;i+=64)YM2610Update_stream(rate/100-i<64?rate/100-i:64);
 return YM2610.adpcma[0].now_addr;
}
'''
 (out/'ym.c').write_text(ym)
 cc=Path(os.environ.get('HOST_CC','gcc'));env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
 inc=[out,ROOT/'src',build/'core/ym2610',build/'core']
 for name in ('pcm','ym'):
  subprocess.run([str(cc),'-O3','-fno-strict-aliasing','-shared','-static-libgcc','-DH1_AUDIO_FAST=1',*[x for p in inc for x in ('-I',str(p))],str(out/(name+'.c')),str(ROOT/'src/audio_fast.c'),'-o',str(out/(name+'.dll'))],check=True,env=env)
 pcm=ctypes.CDLL(str(out/'pcm.dll'));ym=ctypes.CDLL(str(out/'ym.dll'))
 assert pcm.gba_audio_resampler_selftest()==1
 assert ym.h1_ym_rate_selftest()==1
 rng=random.Random(4740);cases=0
 for hz in (8000,16000,22050):
  for fixture in range(24):
   pairs=[(rng.randrange(-32768,32768),rng.randrange(-32768,32768)) for _ in range(1100)]
   if fixture<4:pairs=([(32767,32767),(-32768,-32768),(32767,-32768),(0,0)]*275)
   pcm.reset_pcm(hz);offset=0;actual=[]
   while offset<len(pairs):
    n=min(len(pairs)-offset,rng.choice((1,2,17,127,256)));p=pairs[offset:offset+n]
    raw=struct.pack('<'+'hh'*n,*[v for pair in p for v in pair]);src=(ctypes.c_int16*(n*2)).from_buffer_copy(raw)
    dst=(ctypes.c_int16*2048)();state=(ctypes.c_uint*4)();count=pcm.feed_pcm(src,n,dst,state);actual.extend(dst[:count]);offset+=n
    expected,phase,acc=oracle(pairs[:offset],hz)
    assert actual==expected and state[0]==phase and ctypes.c_int32(state[1]).value==acc and state[2]==count and state[3]==0,(hz,fixture,offset)
   cases+=1
  pcm.reset_pcm(hz);total=0
  for offset in range(0,hz,257):
   n=min(257,hz-offset);src=(ctypes.c_int16*(n*2))();dst=(ctypes.c_int16*2048)();state=(ctypes.c_uint*4)();total+=pcm.feed_pcm(src,n,dst,state)
  assert total==32000,(hz,total)
 tones=[]
 for fm in (0,1):
  peaks=[]
  for hz in (22050,16000,8000):
   ym.init_tone(hz,fm);values=[]
   for offset in range(0,hz,197):
    n=min(197,hz-offset);buf=(ctypes.c_int16*(n*2))();ym.tone(n,buf);values.extend(buf[::2])
   assert max(values)-min(values)>100
   mean=sum(values)/len(values);peak=sum(a<=mean<b for a,b in zip(values,values[1:]));peaks.append(peak)
   tones.append({'kind':'FM' if fm else 'SSG','sample_rate':hz,'rising_crossings_per_second':peak,'rms':math.sqrt(sum(v*v for v in values)/len(values))})
  assert max(peaks)-min(peaks)<=1,(fm,peaks)
 positions=[ym.adpcm_position(hz) for hz in (22050,16000,8000)];assert max(positions)-min(positions)<=2,positions
 report={'ok':True,'environment':'actual C YM2610 and PCM producer on host; firmware services unreachable stubs; no CPU or device simulation','streaming_fixtures':cases,'exact_output_against_independent_box_oracle':True,'output_frames_per_second':32000,'source_rates':[22050,16000,8000],'rate_switch_selftests':48,'tone_peaks':tones,'adpcm_nibble_position_after_10ms':positions,'native_source_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ('src/audio.c','src/ym_audio_fast.h','src/audio_fast.h','tools/prepare.py')}}
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
