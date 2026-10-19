/* Both paths run on physical H1; the baseline is the real A68K interpreter. */
static int h1_scan_selftest(uint16_t *program){
 static uint8_t reference_ram[65536];
 for(unsigned test=0;test<2048;test++){
  context initial,reference;memset(&initial,0,sizeof initial);
  for(unsigned i=0;i<8;i++){initial.a[i]=random_word();initial.d[i]=random_word();}
  unsigned ar=test&7,dr=(test>>3)&7,width=test&64?4:2;
  unsigned high=random_word()&0xff000000,mirror=0x100000+((random_word()%16)<<16);
  unsigned offset=test%7==0?(unsigned[]){0,0xfffc,0xfffe,0xfff0}[(test/7)&3]:random_word()&0xfffe;
  initial.a[7]=0x10f000;initial.a[ar]=high|mirror|offset;
  initial.pc=0x1b00+(test&128?2:0);initial.srh=0x27;initial.ccr=random_word()&31;
  if(test%23==0)initial.irq=1;
  initial.ack=ack;
  program[0x1b00/2]=(width==2?0x3018:0x2018)|(dr<<9)|ar;
  program[0x1b02/2]=test%17?0x67fc:0x66fc;
  program[0x1b04/2]=0x4e72;program[0x1b06/2]=0x2700;
  unsigned available=(65536-offset)/width,zeros=random_word()%65;
  if(available&&zeros>=available)zeros=available-1;
  if(!available)zeros=0;
  unsigned budget=1+random_word()%1800;
  memset(memory.ram,0x5a,sizeof memory.ram);memset(memory.ram+offset,0,zeros*width);
  M68000_regs=initial;change_pc(initial.pc);h1_idle_enabled=0;h1_scan_hle=0;
  int expected=cpu_68k_run(budget);reference=M68000_regs;memcpy(reference_ram,memory.ram,sizeof reference_ram);
  M68000_regs=initial;change_pc(initial.pc);h1_scan_hle=1;
  int got=cpu_68k_run(budget);++h1_scan_stats.tests;
  if(expected!=got||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,memory.ram,sizeof reference_ram)){
   ++h1_scan_stats.failures;
   for(unsigned j=0;j<sizeof reference/4;j++)if(((unsigned*)&reference)[j]!=((unsigned*)&M68000_regs)[j])
    h1_arcade_log("SCAN_CONTEXT word=%u expected=%x got=%x",j,((unsigned*)&reference)[j],((unsigned*)&M68000_regs)[j]);
   h1_arcade_log("SCAN_HLE_FAIL case=%u cycles=%d/%d pc=%x/%x a=%x/%x d=%x/%x",test,expected,got,reference.pc,M68000_regs.pc,reference.a[ar],M68000_regs.a[ar],reference.d[dr],M68000_regs.d[dr]);return 0;
  }
 }
 h1_scan_hle=1;h1_scan_stats.calls=h1_scan_stats.words=h1_scan_stats.cycles=0;
 h1_arcade_log("SCAN_HLE_OK cases=%u full_context_and_RAM=1",h1_scan_stats.tests);return 1;
}
