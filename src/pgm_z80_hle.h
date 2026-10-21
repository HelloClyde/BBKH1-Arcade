/* Included in the owning MAME Z80 translation unit after the opcode helpers.
 * PGM KOV's empty command queue is a pure-RAM, 195-T-state polling loop.
 * Full code signatures, queue/stack bounds and interrupt guards are required.
 * Retain at least one complete loop in the original interpreter so slice
 * overshoot and the final instruction boundary remain unchanged. */
#ifndef H1_PGM_Z80_HLE_H
#define H1_PGM_Z80_HLE_H
#include "h1_port.h"
#include <string.h>

volatile unsigned h1_pgm_z80_hle_enabled=1;
volatile struct {
 unsigned magic,version,calls,loops,cycles,tests,failures;
} h1_pgm_z80_hle_stats={0x33485a50,1};

static const unsigned char h1_pgm_z80_queue_main[]={
 0xcd,0x3c,0x2c,0x21,0x01,0x38,0xcd,0xff,0x28,0x7c,0xb5,0xc2,0xed,0x0d
};
static const unsigned char h1_pgm_z80_queue_test[]={
 0xe5,0xeb,0x21,0x0c,0x00,0x19,0x5e,0x23,0x56,0x7a,0xb3,
 0x3e,0x00,0x20,0x01,0x3c,0x6f,0x26,0x00,0xd1,0xc9
};
static const unsigned char h1_pgm_z80_queue_back[]={0xc3,0x29,0x03};

static void h1_pgm_z80_hle_poll(void){
 if(!h1_pgm_z80_hle_enabled || _PCD!=0x329 || Z80_ICOUNT<390 ||
    Z80.after_EI || Z80.HALT || Z80.irq_state!=CLEAR_LINE ||
    Z80.nmi_state!=CLEAR_LINE)return;
 /* Plain RAM only; exclude the uploaded program, queue and wrapping stack.
  * EX DE,HL swaps the complete PAIR including its unused high half. Real
  * PGM state has zero high halves; decline rather than normalizing them. */
 unsigned sp=_SPD;
 if(sp<0x3a04 || sp>0x7ffc || ((_DED|_HLD)&0xffff0000u) ||
    mame_z80mem[0x380d] || mame_z80mem[0x380e] ||
    mame_z80mem[0x2c3c]!=0xc9 ||
    memcmp(mame_z80mem+0x329,h1_pgm_z80_queue_main,sizeof h1_pgm_z80_queue_main) ||
    memcmp(mame_z80mem+0x28ff,h1_pgm_z80_queue_test,sizeof h1_pgm_z80_queue_test) ||
    memcmp(mame_z80mem+0xded,h1_pgm_z80_queue_back,sizeof h1_pgm_z80_queue_back))return;
 unsigned n=(Z80_ICOUNT-195)/195;
 /* Each loop executes CALL/RET 2c3c, CALL 28ff (PUSH/POP HL), and the
  * conditional/back jumps. Stack residues are observable guest RAM. */
 mame_z80mem[sp-2]=0x32;mame_z80mem[sp-1]=0x03;
 mame_z80mem[sp-4]=0x01;mame_z80mem[sp-3]=0x38;
 _A=1;_F=0;_DED=0x3801;_HLD=1;Z80.EA=0x28ff;_PPC=0xded;
 _R=(UINT8)(_R+24*n);Z80_ICOUNT-=195*n;
 ++h1_pgm_z80_hle_stats.calls;h1_pgm_z80_hle_stats.loops+=n;
 h1_pgm_z80_hle_stats.cycles+=195*n;
}

int h1_pgm_z80_hle_selftest(void){
 /* Use the private 512 KiB application stack; no permanent 192 KiB BSS. */
 unsigned char saved_map[65536],seed_map[65536],reference_map[65536];
 Z80_Regs saved=Z80,initial,reference;
 unsigned old_enabled=h1_pgm_z80_hle_enabled,old_pgm=h1_pgm_active,rng=4740;
 int old_count=Z80_ICOUNT,ok=1;
 memcpy(saved_map,mame_z80mem,sizeof saved_map);h1_pgm_active=1;
 for(unsigned test=0;test<2048;test++){
  memset(seed_map,0x76,sizeof seed_map);
  memcpy(seed_map+0x329,h1_pgm_z80_queue_main,sizeof h1_pgm_z80_queue_main);
  memcpy(seed_map+0x28ff,h1_pgm_z80_queue_test,sizeof h1_pgm_z80_queue_test);
  memcpy(seed_map+0xded,h1_pgm_z80_queue_back,sizeof h1_pgm_z80_queue_back);
  seed_map[0x2c3c]=0xc9;
  for(unsigned i=0x3800;i<0x8000;i++){
   rng=rng*1664525u+1013904223u;seed_map[i]=rng>>24;
  }
  seed_map[0x380d]=seed_map[0x380e]=0;
  if(test%7==0)seed_map[0x380d]=1; /* Nonempty queue must use the core. */
  if(test%11==0)seed_map[0x380e]=1;
  if(test%29==0)seed_map[0x2c3c]=0x76; /* Decline altered code safely. */
  if(test%31==0)seed_map[0x28ff]=0x76;
  if(test%37==0)seed_map[0xded]=0x76;
  if(test%41==0)seed_map[0x32a]=0x3d; /* CALL unchanged HALT destination. */
  memset(&initial,0,sizeof initial);
  for(unsigned i=0;i<13;i++){
   rng=rng*1664525u+1013904223u;((unsigned*)&initial)[i]=rng;
  }
  static const unsigned starts[]={0x329,0x329,0x329,0x32c,0x32f,0x28ff,
    0x2900,0x2901,0x2910,0x2912,0x2913,0x332,0x333,0x334,0xded,0x337};
  initial.PC.d=starts[test%16];initial.SP.d=0x3c00+(test&255);
  initial.DE.d&=65535;initial.HL.d&=65535;
  if(test%43==0)initial.DE.d|=0x12340000u;
  if(test%47==0)initial.HL.d|=0x56780000u;
  initial.R=rng>>24;initial.R2=rng&0x80;
  initial.IFF1=(test>>1)&1;initial.IFF2=test&1;
  initial.IM=1;initial.HALT=test%53==0;
  initial.after_EI=test%59==0;
  initial.irq_state=test%61==0?ASSERT_LINE:CLEAR_LINE;
  initial.nmi_state=test%67==0?ASSERT_LINE:CLEAR_LINE;
  initial.extra_cycles=test%7;
  /* The partial-helper fixtures have already entered a real CALL/PUSH.
   * Their synthetic return addresses all lead to the same safe main loop. */
  unsigned sp=initial.SP.d;
  seed_map[sp]=0x32;seed_map[sp+1]=0x03;
  seed_map[sp+2]=0x32;seed_map[sp+3]=0x03;
  unsigned budget=1+(test*83)%2048;
  if(test%23==0)budget=1+(test*137)%70667;
  if(test>=2000){budget=1;initial.extra_cycles=2+test%7;}
  memcpy(mame_z80mem,seed_map,sizeof seed_map);Z80=initial;h1_pgm_z80_hle_enabled=0;
  int expected=z80_execute(budget);reference=Z80;
  memcpy(reference_map,mame_z80mem,sizeof reference_map);
  memcpy(mame_z80mem,seed_map,sizeof seed_map);Z80=initial;h1_pgm_z80_hle_enabled=1;
  int actual=z80_execute(budget);++h1_pgm_z80_hle_stats.tests;
  if(expected!=actual || memcmp(&reference,&Z80,sizeof reference) ||
     memcmp(reference_map,mame_z80mem,sizeof reference_map)){
   ++h1_pgm_z80_hle_stats.failures;ok=0;
   h1_arcade_log("PGM_Z80_HLE_FAIL case=%u pc=%x/%x cycles=%d/%d R=%x/%x AF=%x/%x EA=%x/%x",test,
    reference.PC.d,Z80.PC.d,expected,actual,reference.R,Z80.R,reference.AF.d,Z80.AF.d,reference.EA,Z80.EA);
   break;
  }
 }
 Z80=saved;memcpy(mame_z80mem,saved_map,sizeof saved_map);Z80_ICOUNT=old_count;
 h1_pgm_z80_hle_enabled=old_enabled;h1_pgm_active=old_pgm;
 h1_arcade_log("PGM_Z80_HLE_TEST cases=%u failures=%u loops=%u",h1_pgm_z80_hle_stats.tests,
  h1_pgm_z80_hle_stats.failures,h1_pgm_z80_hle_stats.loops);h1_arcade_log_flush();
 return ok;
}
#endif
