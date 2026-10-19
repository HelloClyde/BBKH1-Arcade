/* Included in the dual renderer translation unit. Runs on the app thread. */
#if H1_PALETTE32
void h1_palette32_benchmark(void){
 unsigned loops=h1_palette32_bench.request,saved=h1_palette32_enabled,dirty=h1_present_dirty;
 h1_palette32_bench.request=0;h1_palette32_bench.state=1;
 if(loops<20||loops>800||h1_palette32_bench.mode>2){h1_palette32_bench.state=3;return;}
 unsigned old_mode=h1_palette32_mode(h1_palette32_bench.mode);
 unsigned input=h1_render_hash(memory.vid.ram,sizeof memory.vid.ram)^h1_render_hash(memory.vid.pal_host,sizeof memory.vid.pal_host);
 h1_palette32_bench.loops=loops*2;h1_palette32_bench.ticks16=h1_palette32_bench.ticks32=0;
 h1_palette32_bench.source_differences=0;h1_present_dirty=0;
 h1_render_bench_only=h1_palette32_bench.compose_only;
 for(unsigned pass=0;pass<4;pass++){
  unsigned rgb32=pass==1||pass==2;h1_palette32_enabled=rgb32;draw_screen();
  unsigned start=h1_raw_tick_80hz();
  for(unsigned i=0;i<loops;i++){draw_screen();if(!(i&15))h1_arcade_check_stop();}
  unsigned elapsed=h1_raw_tick_80hz()-start,hash;
  /* Hashes and full source comparison are outside the timed loop. */
  h1_render_bench_only=0;screen_flip();hash=h1_palette32_lcd_hash();h1_render_bench_only=h1_palette32_bench.compose_only;
  if(rgb32){h1_palette32_bench.ticks32+=elapsed;h1_palette32_bench.hash32=hash;h1_palette32_bench.source_differences+=h1_palette32_compare_source();}
  else{h1_palette32_bench.ticks16+=elapsed;h1_palette32_bench.hash16=hash;}
 }
 h1_palette32_bench.input_unchanged=input==(h1_render_hash(memory.vid.ram,sizeof memory.vid.ram)^h1_render_hash(memory.vid.pal_host,sizeof memory.vid.pal_host));
 h1_palette32_enabled=saved;h1_present_dirty=dirty;h1_render_bench_only=0;h1_palette32_mode(old_mode);draw_screen();
 h1_palette32_bench.state=h1_palette32_bench.source_differences||h1_palette32_bench.hash16!=h1_palette32_bench.hash32||!h1_palette32_bench.input_unchanged?3:2;
}
int h1_palette32_selftest(void){
 unsigned short *ref=malloc((352*256+16)*2);if(!ref)return 0;
 extern uint32_t framebuffer32[352*256+16];uint32_t *out=framebuffer32;
 static unsigned graphics[128],palette[4096],expanded[4096];
 typedef void(*draw_fn)(unsigned,int,int,int,int,int,int,int,unsigned char*);
 typedef void(*line_fn)(unsigned,int,int,int,int,int,int,unsigned char*);
 draw_fn a[]={draw_tile,draw_tile_50,draw_tile_25},b[]={draw_tile32,draw_tile_5032,draw_tile_2532};
 line_fn al[]={draw_scanline_tile,draw_scanline_tile_50,draw_scanline_tile_25},bl[]={draw_scanline_tile32,draw_scanline_tile_5032,draw_scanline_tile_2532};
 SDL_Surface local={.pixels=0,.pitch=704,.w=352,.h=256},*saved=buffer;
 unsigned *saved_pal=current_pc_pal,*saved32=h1_pc_pal32;char saved_y[17],*saved_x=dda_x_skip,xskip[16];
 memcpy(saved_y,dda_y_skip,sizeof saved_y);buffer=&local;unsigned seed=4740,failed=0;
 for(unsigned test=0;test<512;test++){
  unsigned g=test/3,mode=test%3,zx=g&1?16:1+(g/2)%15,zy=1+(g/2)%16,xf=(g>>5)&1,yf=(g>>6)&1;
  if(test>=384){g=test-384;mode=0;zx=zy=16;xf=(g>>1)&1;yf=(g>>2)&1;}
  for(unsigned i=0;i<128;i++){seed=seed*1664525u+1013904223u;graphics[i]=g%6==0?0:g%6==1?0x11111111u:g%6==2?seed|0x11111111u:seed;}
  for(unsigned i=0;i<4096;i++){seed=seed*1664525u+1013904223u;palette[i]=seed&65535;expanded[i]=h1_expand565(palette[i]);}
  memory.rom.tiles.p=(uint8_t*)graphics;memory.nb_of_tiles=4;current_pc_pal=palette;h1_pc_pal32=expanded;
  dda_y_skip[0]=0;for(unsigned i=1;i<zy;i++)dda_y_skip[i]=i*16/zy-(i-1)*16/zy;
  for(unsigned i=0;i<16;i++)xskip[i]=i<zx;dda_x_skip=xskip;
  unsigned tile=(test>>1)&3,color=(test*37)&255,x=16+(test&15);
  for(unsigned scan=0;scan<2;scan++){
   for(unsigned i=0;i<352*256+16;i++){ref[i]=(uint16_t)(i*331u+test);out[i]=h1_expand565(ref[i]);}
   local.pitch=704;h1_render_fast=0;
   if(scan)al[mode](tile,zy-1,x,20,zx,color,xf,(unsigned char*)(ref+8));
   else a[mode](tile,x,20,zx,zy,color,xf,yf,(unsigned char*)(ref+8));
   local.pitch=1408;h1_render_fast=1;
   unsigned control=h1_mxu_control(),before,after;
   __asm__ volatile("mfc0 %0,$12":"=r"(before));
   if(scan)bl[mode](tile,zy-1,x,20,zx,color,xf,(unsigned char*)(out+8));
   else b[mode](tile,x,20,zx,zy,color,xf,yf,(unsigned char*)(out+8));
   __asm__ volatile("mfc0 %0,$12":"=r"(after));
   failed=control!=h1_mxu_control()||before!=after;
   for(unsigned i=0;i<352*256+16&&!failed;i++)failed|=out[i]!=h1_expand565(ref[i]);
   ++h1_palette32_stats.tests;
   if(failed){++h1_palette32_stats.failures;h1_arcade_log("PALETTE32_FAIL case=%u scan=%u",test,scan);break;}
  }
  if(failed)break;
 }
 free(ref);buffer=saved;current_pc_pal=saved_pal;h1_pc_pal32=saved32;dda_x_skip=saved_x;memcpy(dda_y_skip,saved_y,sizeof saved_y);
 memset(&memory,0,sizeof memory);h1_render_fast=1;
 if(!failed)h1_arcade_log("PALETTE32_OK cases=%u",h1_palette32_stats.tests);
 return !failed;
}
#endif
