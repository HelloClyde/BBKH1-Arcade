/* Included inside the owning Z80 implementation, after all opcode helpers.
 * A verified ROM instruction pattern polls two plain RAM queues. Retain every
 * 117-cycle iteration's counter increment, R refresh, AF, B and EA updates.
 * Timer/IRQ delivery still occurs at the original scanline boundaries. */
#include "speed_core.h"
#include "h1_port.h"
#include <string.h>
volatile unsigned h1_z80_hle=1;
volatile h1_z80_hle_stats_t h1_z80_hle_stats={0x31485a48,1};
extern unsigned char mame_z80mem[65536];
static const unsigned char h1_z80_poll_code[]={
 0x3a,0x9a,0xfe,0x3c,0x32,0x9a,0xfe,0x3a,0xac,0xfe,0x47,
 0x3a,0xab,0xfe,0xb8,0x20,0x1a,0x3a,0x37,0xfe,0x47,
 0x3a,0x36,0xfe,0xb8,0x28,0xe5
};
static unsigned h1_z80_poll_valid;
void h1_z80_hle_init(void){
 h1_z80_poll_valid=memcmp(mame_z80mem+0x122,h1_z80_poll_code,sizeof h1_z80_poll_code)==0;
 h1_z80_hle_stats.matched=h1_z80_poll_valid;
}
static void h1_z80_poll_batch(void){
 if(Z80_ICOUNT<=117 || Z80.after_EI || Z80.irq_state!=CLEAR_LINE || Z80.nmi_state!=CLEAR_LINE)return;
 if(mame_z80mem[0xfeac]!=mame_z80mem[0xfeab] || mame_z80mem[0xfe36]!=mame_z80mem[0xfe37])return;
 unsigned n=(Z80_ICOUNT-1)/117;
 mame_z80mem[0xfe9a]=(UINT8)(mame_z80mem[0xfe9a]+n);
 _A=_B=mame_z80mem[0xfe36];_F=ZF|NF|(_B&(YF|XF));Z80.EA=0xfe36;
 _R=(UINT8)(_R+13*n);Z80_ICOUNT-=117*n;
 h1_z80_hle_stats.loops+=n;h1_z80_hle_stats.cycles+=117*n;
}
/* Compile the verified polling block's remaining instructions directly.
 * Use the original INC/CP flag helpers. Each instruction retains its exact
 * cycle cost, refresh increment, EA, previous PC and final slice overshoot. */
static void h1_z80_poll_run(void){
 if(Z80_ICOUNT<=0 || Z80.after_EI || Z80.HALT || Z80.irq_state!=CLEAR_LINE || Z80.nmi_state!=CLEAR_LINE)return;
 while(Z80_ICOUNT>0){
  unsigned pc=_PCD;
  if(pc==0x122)h1_z80_poll_batch();
  /* Invalid entry into an operand byte must use the original decoder. */
  switch(pc){
  case 0x122:case 0x125:case 0x126:case 0x129:case 0x12c:case 0x12d:
  case 0x130:case 0x131:case 0x133:case 0x136:case 0x137:case 0x13a:case 0x13b:break;
  default:return;
  }
  _PPC=_PCD;++_R;
  switch(pc){
  case 0x122:Z80.EA=0xfe9a;_A=mame_z80mem[0xfe9a];_PC=0x125;Z80_ICOUNT-=13;break;
  case 0x125:_A=INC(_A);_PC=0x126;Z80_ICOUNT-=4;break;
  case 0x126:Z80.EA=0xfe9a;mame_z80mem[0xfe9a]=_A;_PC=0x129;Z80_ICOUNT-=13;break;
  case 0x129:Z80.EA=0xfeac;_A=mame_z80mem[0xfeac];_PC=0x12c;Z80_ICOUNT-=13;break;
  case 0x12c:_B=_A;_PC=0x12d;Z80_ICOUNT-=4;break;
  case 0x12d:Z80.EA=0xfeab;_A=mame_z80mem[0xfeab];_PC=0x130;Z80_ICOUNT-=13;break;
  case 0x130:CP(_B);_PC=0x131;Z80_ICOUNT-=4;break;
  case 0x131:if(_F&ZF){_PC=0x133;Z80_ICOUNT-=7;}else{_PC=0x14d;Z80_ICOUNT-=12;}break;
  case 0x133:Z80.EA=0xfe37;_A=mame_z80mem[0xfe37];_PC=0x136;Z80_ICOUNT-=13;break;
  case 0x136:_B=_A;_PC=0x137;Z80_ICOUNT-=4;break;
  case 0x137:Z80.EA=0xfe36;_A=mame_z80mem[0xfe36];_PC=0x13a;Z80_ICOUNT-=13;break;
  case 0x13a:CP(_B);_PC=0x13b;Z80_ICOUNT-=4;break;
  default:if(_F&ZF){_PC=0x122;Z80_ICOUNT-=12;}else{_PC=0x13d;Z80_ICOUNT-=7;}break;
  }
 }
}
int h1_z80_hle_selftest(void){
 unsigned char seed_map[65536],reference_map[65536];
 static Z80_Regs initial,reference;
 unsigned rng=4740;
 z80_init();
 for(unsigned test=0;test<1600;test++){
  memset(seed_map,0x76,sizeof seed_map);
  memcpy(seed_map+0x122,h1_z80_poll_code,sizeof h1_z80_poll_code);
  if(test%19==0)seed_map[0x124]=0xfd; /* ROM mismatch must decline HLE. */
  for(unsigned i=0xf800;i<65536;i++){rng=rng*1664525u+1013904223u;seed_map[i]=rng>>24;}
  if(test%3){seed_map[0xfeac]=seed_map[0xfeab];seed_map[0xfe36]=seed_map[0xfe37];}
  memset(&initial,0,sizeof initial);
  for(unsigned i=0;i<sizeof initial/4;i++){rng=rng*1664525u+1013904223u;((unsigned*)&initial)[i]=rng;}
  static const unsigned starts[]={0x122,0x125,0x126,0x129,0x12c,0x12d,0x130,0x131,0x133,0x136,0x137,0x13a,0x13b,0x123,0x124,0x132,0x13c};
  initial.PC.d=starts[test%17];initial.SP.d=0xff00;
  initial.HALT=0;initial.after_EI=test%17==0;
  initial.irq_state=test%23==0?ASSERT_LINE:CLEAR_LINE;
  initial.nmi_state=test%29==0?ASSERT_LINE:CLEAR_LINE;
  initial.extra_cycles=test%7;initial.irq_callback=0;initial.irq_max=0;
  unsigned budget=1+(test*83)%2048;
  if(test>=1536){budget=1;initial.extra_cycles=2+test%7;}
  memcpy(mame_z80mem,seed_map,sizeof seed_map);Z80=initial;h1_z80_hle_init();h1_z80_hle=0;
  int expected=z80_execute(budget);reference=Z80;memcpy(reference_map,mame_z80mem,sizeof reference_map);
  memcpy(mame_z80mem,seed_map,sizeof seed_map);Z80=initial;h1_z80_hle_init();h1_z80_hle=1;
  int got=z80_execute(budget);++h1_z80_hle_stats.tests;
  if(expected!=got || memcmp(&reference,&Z80,sizeof reference) || memcmp(reference_map,mame_z80mem,sizeof reference_map)){
   ++h1_z80_hle_stats.failures;
   h1_arcade_log("Z80_HLE_FAIL case=%u pc=%x/%x cycles=%d/%d AF=%x/%x BC=%x/%x R=%x/%x EA=%x/%x",test,reference.PC.d,Z80.PC.d,expected,got,reference.AF.d,Z80.AF.d,reference.BC.d,Z80.BC.d,reference.R,Z80.R,reference.EA,Z80.EA);return 0;
  }
 }
 memset(&Z80,0,sizeof Z80);memset(mame_z80mem,0,sizeof mame_z80mem);
 h1_z80_poll_valid=0;h1_z80_hle_stats.matched=0;
 h1_z80_hle_stats.loops=h1_z80_hle_stats.cycles=0;
 h1_arcade_log("Z80_HLE_OK cases=%u full_context_and_RAM=1",h1_z80_hle_stats.tests);return 1;
}
