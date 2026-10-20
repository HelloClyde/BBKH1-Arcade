/* Musashi to GnGeo bus adapter. GPL-2.0-or-later. */
#include "memory.h"
#if H1_PGM
#include "pgm.h"
#endif
#if !H1_A68K
#include "m68k.h"
#endif
static unsigned total_cycles;
#define READ_CASES(bits) \
 case 0:return mem68k_fetch_cpu_##bits(a); \
 case 1:return mem68k_fetch_ram_##bits(a); \
 case 2:return mem68k_fetch_bk_normal_##bits(a); \
 case 4:return mem68k_fetch_pal_##bits(a); \
 case 8:return mem68k_fetch_memcrd_##bits(a); \
 case 12:return mem68k_fetch_bios_##bits(a); \
 case 13:return mem68k_fetch_sram_##bits(0xd00000+(a&65535));
unsigned m68k_read_memory_8(unsigned a){
#if H1_PGM
 if(h1_pgm_active)return h1_pgm_read8(a);
#endif
 a&=0xffffff;
 switch(a>>20){READ_CASES(byte)
 case 3:switch(a>>12){
 case 0x300:return mem68k_fetch_ctl1_byte(a);
 case 0x320:return mem68k_fetch_coin_byte(a);
 case 0x340:return mem68k_fetch_ctl2_byte(a);
 case 0x380:return mem68k_fetch_ctl3_byte(a);
 case 0x3c0:return (mem68k_fetch_video_word(a&~1u)>>((a&1)?0:8))&255;
 } }
 return 255;
}
unsigned m68k_read_memory_16(unsigned a){
#if H1_PGM
 if(h1_pgm_active)return h1_pgm_read16(a);
#endif
 a&=0xffffff;
 switch(a>>20){READ_CASES(word)
 case 3:if((a>>12)==0x3c0)return mem68k_fetch_video_word(a);
 }
 return (m68k_read_memory_8(a)<<8)|m68k_read_memory_8(a+1);
}
unsigned m68k_read_memory_32(unsigned a){return (m68k_read_memory_16(a)<<16)|m68k_read_memory_16(a+2);}
#define WRITE_CASES(bits) \
 case 1:mem68k_store_ram_##bits(a,v);return; \
 case 2:mem68k_store_bk_normal_##bits(a,v);return; \
 case 4:mem68k_store_pal_##bits(a,v);return; \
 case 8:mem68k_store_memcrd_##bits(a,v);return; \
 case 13:mem68k_store_sram_##bits(0xd00000+(a&65535),v);return;
void m68k_write_memory_8(unsigned a,unsigned v){
#if H1_PGM
 if(h1_pgm_active){h1_pgm_write8(a,v);return;}
#endif
 a&=0xffffff;
 switch(a>>20){WRITE_CASES(byte)
 case 3:switch(a>>12){
 case 0x300:memory.watchdog=0;return;
 case 0x320:mem68k_store_z80_byte(a,v);return;
 case 0x380:mem68k_store_pd4990_byte(a,v);return;
 case 0x3a0:mem68k_store_setting_byte(a,v);return;
 case 0x3c0:mem68k_store_video_byte(a,v);return;
 } }
}
void m68k_write_memory_16(unsigned a,unsigned v){
#if H1_PGM
 if(h1_pgm_active){h1_pgm_write16(a,v);return;}
#endif
 a&=0xffffff;
 switch(a>>20){WRITE_CASES(word)
 case 3:switch(a>>12){
 case 0x300:memory.watchdog=0;return;
 case 0x320:mem68k_store_z80_word(a,v);return;
 case 0x380:mem68k_store_pd4990_word(a,v);return;
 case 0x3a0:mem68k_store_setting_byte(a|1u,v);return;
 case 0x3c0:mem68k_store_video_word(a,v);return;
 } }
}
void m68k_write_memory_32(unsigned a,unsigned v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
#if !H1_A68K
void cpu_68k_init(void){m68k_init();m68k_set_cpu_type(M68K_CPU_TYPE_68000);}
void cpu_68k_reset(void){m68k_pulse_reset();}
int cpu_68k_run(uint32_t n){int used=m68k_execute(n);total_cycles+=used;return used-(int)n;}
void cpu_68k_interrupt(int n){m68k_set_irq(n);}
uint32_t cpu_68k_getpc(void){return m68k_get_reg(0,M68K_REG_PC);}
int cpu_68k_getcycle(void){return total_cycles+m68k_cycles_run();}
void cpu_68k_bankswitch(uint32_t a){bankaddress=a;}
#endif
