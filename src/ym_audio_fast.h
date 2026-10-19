/* Included in the actual YM core. Change sampling density, retain chip phase. */
#if H1_AUDIO_FAST
#include "audio_fast.h"
#include "h1_sdk.h"
static int h1_scale_count(int count,unsigned next,unsigned old){
 if(count<=0||!old)return 1;
 uint64_t value=(uint64_t)(unsigned)count*next/old;
 return value>0x7fffffffu?0x7fffffff:value?(int)value:1;
}
void h1_ym_set_rate(unsigned rate){
 if(rate!=8000&&rate!=16000&&rate!=22050)return;
 if(YM2610.OPN.ST.rate==(int)rate)return;
 unsigned old=YM2610.OPN.ST.rate;
 if(!old)return;
 YM2610ChangeSamplerate(rate);
 for(unsigned ch=0;ch<6;ch++){
  unsigned bf=YM2610.CH[ch].block_fnum;
  YM2610.CH[ch].fc=YM2610.OPN.fn_table[(bf&2047)*2]>>(7-((bf>>11)&7));
  YM2610.CH[ch].SLOT[SLOT1].Incr=(unsigned)-1;
 }
 for(unsigned ch=0;ch<3;ch++){
  unsigned fn=YM2610.OPN.SL3.block_fnum[ch]&2047,blk=YM2610.OPN.SL3.kcode[ch]>>2;
  YM2610.OPN.SL3.fc[ch]=YM2610.OPN.fn_table[fn*2]>>(7-blk);
  unsigned period=(YM2610.regs[ch*2]+256*YM2610.regs[ch*2+1])*SSG.step;
  if(!period)period=SSG.step;
  SSG.count[ch]=h1_scale_count(SSG.count[ch],period,SSG.period[ch]);SSG.period[ch]=period;
 }
 unsigned period=YM2610.regs[SSG_NOISEPER]*SSG.step;if(!period)period=SSG.step;
 SSG.CountN=h1_scale_count(SSG.CountN,period,SSG.PeriodN);SSG.PeriodN=period;
 period=(YM2610.regs[SSG_EFINE]+256*YM2610.regs[SSG_ECOARSE])*SSG.step;if(!period)period=SSG.step/2;
 SSG.CountE=h1_scale_count(SSG.CountE,period,SSG.PeriodE);SSG.PeriodE=period;
 YM2610.OPN.lfo_inc=YM2610.regs[0x22]&8?YM2610.OPN.lfo_freq[YM2610.regs[0x22]&7]:0;
 YM2610.adpcmb.step=(u32)(YM2610.adpcmb.delta*YM2610.adpcmb.freqbase);
}
static void h1_ym_reg(unsigned r,unsigned v){YM2610Write(r>>8?2:0,r&255);YM2610Write(r>>8?3:1,v);}
int h1_ym_rate_selftest(void){
 static unsigned char rom[4096];for(unsigned i=0;i<sizeof rom;i++)rom[i]=(i*37)^0x5a;
 YM2610Init(8000000,22050,rom,sizeof rom,rom,sizeof rom,0,0);
 h1_ym_reg(0,91);h1_ym_reg(1,1);h1_ym_reg(6,17);h1_ym_reg(7,0x3e);h1_ym_reg(8,15);
 h1_ym_reg(11,19);h1_ym_reg(12,1);h1_ym_reg(13,10);h1_ym_reg(0x22,0xf);
 h1_ym_reg(0xa5,0x22);h1_ym_reg(0xa1,0x64);h1_ym_reg(0x19,0xff);h1_ym_reg(0x1a,0x73);
 YM2610Update_stream(127);
 for(unsigned i=0;i<48;i++){
  unsigned next=(unsigned[]){16000,8000,22050}[i%3];u8 registers[512];memcpy(registers,YM2610.regs,sizeof registers);
  unsigned phase=YM2610.CH[1].SLOT[SLOT1].phase,eg=YM2610.OPN.eg_cnt,lfo=YM2610.OPN.lfo_cnt;
  unsigned addr=YM2610.adpcma[0].now_addr,delta=YM2610.adpcmb.now_addr;
  double timer=YM2610.OPN.ST.TimerBase;
  h1_ym_set_rate(next);
  unsigned bf=YM2610.CH[1].block_fnum;
  unsigned bad=YM2610.OPN.ST.rate!=(int)next||memcmp(registers,YM2610.regs,sizeof registers)||
   phase!=YM2610.CH[1].SLOT[SLOT1].phase||eg!=YM2610.OPN.eg_cnt||lfo!=YM2610.OPN.lfo_cnt||
   addr!=YM2610.adpcma[0].now_addr||delta!=YM2610.adpcmb.now_addr||timer!=YM2610.OPN.ST.TimerBase||
   YM2610.CH[1].fc!=(YM2610.OPN.fn_table[(bf&2047)*2]>>(7-(bf>>11)))||
   YM2610.adpcmb.step!=(u32)(YM2610.adpcmb.delta*YM2610.adpcmb.freqbase)||SSG.count[0]<=0||SSG.CountN<=0||SSG.CountE<=0;
  ++h1_audio_fast_stats.tests;if(bad){++h1_audio_fast_stats.failures;return 0;}
  YM2610Update_stream(33);
 }
 return 1;
}
/* Each timed trial begins with the same live chip snapshot. State-copy cost is
 * common to both paths and included; CPU, timers, registers and DMA do not run. */
void h1_audio_benchmark(void){
 static struct ym2610_t original,low;
 static struct SSG_t original_ssg,low_ssg;
 unsigned loops=h1_audio_bench.request;h1_audio_bench.request=0;h1_audio_bench.state=1;
 if(loops<100||loops>4000){h1_audio_bench.state=3;return;}
 memcpy(&original,&YM2610,sizeof original);memcpy(&original_ssg,&SSG,sizeof SSG);
 s32 scratch[23]={m2,c1,c2,mem,out_ssg,LFO_AM,LFO_PM};
 memcpy(scratch+7,out_fm,sizeof out_fm);memcpy(scratch+15,out_adpcma,sizeof out_adpcma);memcpy(scratch+19,out_delta,sizeof out_delta);
 h1_ym_set_rate(16000);memcpy(&low,&YM2610,sizeof low);memcpy(&low_ssg,&SSG,sizeof SSG);
 h1_audio_bench.loops=loops*2;h1_audio_bench.ticks22=h1_audio_bench.ticks16=0;
 h1_audio_bench.nonzero22=h1_audio_bench.nonzero16=0;
 for(unsigned pass=0;pass<4;pass++){
  unsigned fast=pass==1||pass==2;
  memcpy(&YM2610,&original,sizeof YM2610);memcpy(&SSG,&original_ssg,sizeof SSG);
  if(fast)h1_ym_set_rate(16000);else h1_ym_set_rate(22050);
  /* Cache a seed at each rate outside the timed loop. */
  struct ym2610_t *seed=fast?&low:&original;struct SSG_t *seed_ssg=fast?&low_ssg:&original_ssg;
  static struct ym2610_t high;static struct SSG_t high_ssg;
  if(!fast){memcpy(&high,&YM2610,sizeof high);memcpy(&high_ssg,&SSG,sizeof high_ssg);seed=&high;seed_ssg=&high_ssg;}
  unsigned n=(fast?16000u:22050u)*16896u/1000000u,start=h1_raw_tick_80hz();
  for(unsigned i=0;i<loops;i++){
   memcpy(&YM2610,seed,sizeof YM2610);memcpy(&SSG,seed_ssg,sizeof SSG);YM2610Update_stream(n);
   if(!(i&31))h1_arcade_check_stop();
  }
  unsigned elapsed=h1_raw_tick_80hz()-start,nonzero=0;for(unsigned i=0;i<n*2;i++)nonzero+=play_buffer[i]!=0;
  if(fast){h1_audio_bench.ticks16+=elapsed;h1_audio_bench.nonzero16+=nonzero;h1_audio_bench.samples16=n;}
  else{h1_audio_bench.ticks22+=elapsed;h1_audio_bench.nonzero22+=nonzero;h1_audio_bench.samples22=n;}
 }
 memcpy(&YM2610,&original,sizeof YM2610);memcpy(&SSG,&original_ssg,sizeof SSG);
 m2=scratch[0];c1=scratch[1];c2=scratch[2];mem=scratch[3];out_ssg=scratch[4];LFO_AM=scratch[5];LFO_PM=scratch[6];memcpy(out_fm,scratch+7,sizeof out_fm);memcpy(out_adpcma,scratch+15,sizeof out_adpcma);memcpy(out_delta,scratch+19,sizeof out_delta);
 h1_audio_bench.restored=!memcmp(&YM2610,&original,sizeof YM2610)&&!memcmp(&SSG,&original_ssg,sizeof SSG);
 h1_audio_bench.state=h1_audio_bench.restored?2:3;
}
#endif
