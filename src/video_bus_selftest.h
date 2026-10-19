/* Own 68000 word/long port traffic; executed by both native interpreter paths. */
static int h1_video_bus_selftest(uint16_t *program){
 static VIDEO reference_video;
 static const uint16_t fixture[]={
  0x3080,0x3141,2,0x3142,4,0x3083,0x3144,2,0x3410,0x3628,2,
  0x2085,0x3146,2,0x3087,0x3140,2,0x4e72,0x2700
 };
 memcpy(program+0x1700/2,fixture,sizeof fixture);
 for(unsigned test=0;test<256;test++){
  context initial,reference;memset(&initial,0,sizeof initial);
  for(unsigned i=0;i<8;i++){initial.d[i]=random_word();initial.a[i]=random_word();}
  initial.a[0]=((test%5)?0x3c0000:0x3c0010)|(random_word()&0xff000000);
  initial.a[7]=0x10f000;initial.pc=0x1700;initial.ccr=random_word()&31;
  initial.srh=0x27;initial.ack=ack;
  unsigned pattern=random_word()&255,budget=1+random_word()%800;
  unsigned vp=(unsigned[]){0,1,0x7fff,0x8000,0xffff}[test%5];
  for(unsigned pass=0;pass<2;pass++){
   memset(&memory.vid,0,sizeof memory.vid);memset(memory.vid.ram,pattern,sizeof memory.vid.ram);
   memory.vid.vptr=vp;memory.vid.rbuf=pattern*257;
   memory.vid.modulo=(int16_t)(test*1009);
   M68000_regs=initial;change_pc(initial.pc);h1_idle_enabled=0;h1_video_bus_enabled=pass;
   int got=cpu_68k_run(budget);
   if(!pass){reference=M68000_regs;reference_video=memory.vid;program[0x17f0/2]=(uint16_t)got;}
   else{
    ++h1_video_bus_stats.tests;
    if(got!=(int16_t)program[0x17f0/2] || memcmp(&reference,&M68000_regs,sizeof reference) ||
       memcmp(&reference_video,&memory.vid,sizeof reference_video)){
     ++h1_video_bus_stats.failures;
     h1_arcade_log("VIDEO_BUS_FAIL case=%u cycles=%d/%d pc=%x/%x",test,(int16_t)program[0x17f0/2],got,reference.pc,M68000_regs.pc);return 0;
    }
   }
  }
 }
 h1_video_bus_enabled=1;h1_video_bus_stats.calls=0;
 h1_arcade_log("VIDEO_BUS_OK cases=%u full_context_and_VIDEO=1",h1_video_bus_stats.tests);return 1;
}
