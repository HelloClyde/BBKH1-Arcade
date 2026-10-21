/* H1 bus binding for the optional OpenDingux MIPS A68K experiment.
 * Only this adapter is GPL-2.0-or-later; upstream A68K retains its own terms. */
#include "memory.h"
#include "h1_port.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#if H1_PGM
#include "pgm.h"
#endif
#if H1_SPEED_CORE
#include "speed_core.h"
volatile unsigned h1_video_bus_enabled=1;
volatile h1_video_bus_stats_t h1_video_bus_stats={0x31425648,1};
volatile unsigned h1_rom_fast_enabled=1;
unsigned h1_bus_rom_active;
uintptr_t h1_bus_rom_map[4];
static void refresh_rom_map(void){
 unsigned size=memory.rom.cpu_m68k.size,bank=bankaddress;
 h1_bus_rom_map[0]=size>=4?(uintptr_t)memory.rom.cpu_m68k.p:0;
 h1_bus_rom_map[1]=size<0x100000?size:0x100000;
 unsigned left=size>bank?size-bank:0;
 h1_bus_rom_map[2]=left>=4&&!memory.bksw_unscramble?(uintptr_t)(memory.rom.cpu_m68k.p+bank):0;
 h1_bus_rom_map[3]=left<0x100000?left:0x100000;
}
const uintptr_t h1_vram_descriptor[]={
 (uintptr_t)memory.vid.ram,(uintptr_t)&memory.vid.vptr,(uintptr_t)&memory.vid.modulo,
 (uintptr_t)&memory.vid.rbuf,(uintptr_t)&h1_video_bus_stats.calls
};
#endif
typedef struct {
 unsigned d[8],a[8],isp,srh,ccr,xc,pc,irq,sr;
 int (*ack)(int);
 unsigned ppc;
 int (*reset)(void),(*rte)(void),(*cmp)(unsigned,int);
 unsigned sfc,dfc,usp,vbr,bank,version,fullpc;
} context;
extern context M68000_regs;
extern uintptr_t a68k_memory_intf[13];
extern void M68000_RUN(void),M68000_RESET(void);
extern unsigned m68k_read_memory_8(unsigned),m68k_read_memory_16(unsigned),m68k_read_memory_32(unsigned);
extern void m68k_write_memory_8(unsigned,unsigned),m68k_write_memory_16(unsigned,unsigned),m68k_write_memory_32(unsigned,unsigned);
_Static_assert(sizeof(context)==0x8c && offsetof(context,pc)==0x50,"A68K assembly ABI");
unsigned char *OP_ROM,*OP_RAM;
int m68k_ICount;
unsigned mem_amask=0xffffff,mame_debug,illegal_op,illegal_pc,opcode_entry,cur_mrhard;
static unsigned total,requested;
#if H1_PGM
volatile unsigned h1_pgm_idle_enabled=1;
volatile struct {unsigned magic,version,calls,loops,cycles,tests,failures;} h1_pgm_idle_stats={0x32444950,1};
volatile unsigned h1_pgm_idle_branch_enabled=1;
volatile struct {unsigned magic,version,calls,loops,cycles,tests,failures;} h1_pgm_branch_stats={0x33444950,1};
volatile unsigned h1_pgm_banking_enabled=1;
unsigned h1_pgm_banking_active;
volatile struct {unsigned magic,version,hits,tests,failures;} h1_pgm_banking_stats={0x34425050,1};
/* Available for diagnostics; KOV battle measurements had no useful hits. */
volatile unsigned h1_pgm_dbra_enabled=0;
unsigned h1_pgm_dbra_active;
volatile struct {unsigned magic,version,calls,hits,loops,cycles,tests,failures,rejected;} h1_pgm_dbra_stats={0x35424450,1};
typedef struct {unsigned pc,a,b;} pgm_idle_descriptor;
static pgm_idle_descriptor pgm_branch_cache[2];
static unsigned pgm_branch_next,pgm_cpu_running;
static int pgm_idle_zero(unsigned a,unsigned b){
 uint8_t *ram=h1_pgm_bus_pages[0x80];
 return !*(uint16_t*)(ram+a)&&!*(uint16_t*)(ram+a+2)&&!ram[b^1];
}
static unsigned pgm_idle_at(unsigned pc,unsigned budget,unsigned ccr,pgm_idle_descriptor *match){
 if(!h1_pgm_idle_enabled||budget<112||M68000_regs.irq)return 0;
 pc&=0xffffff;unsigned starts[]={pc,pc-6,pc-28,pc-34};
 for(unsigned i=0;i<4;i++){
  unsigned start=starts[i];if((start&65535)>65500)continue;
  uint16_t *p=(uint16_t*)h1_pgm_code(start);if(!p)continue;
  /* TST.L plain_RAM; BEQ +20; ...; TST.B plain_RAM; BEQ back.
   * Both zero flags can change only at a later CPU/interrupt boundary.
   * No callback, I/O, RAM counter, guest clock or IRQ work is omitted. */
  if(p[0]!=0x4ab9||p[3]!=0x6714||p[14]!=0x4a39||p[17]!=0x67dc)continue;
  if((i==1||i==3)&&!(ccr&4))continue;
  unsigned a=((unsigned)p[1]<<16)|p[2],b=((unsigned)p[15]<<16)|p[16];a&=0xffffff;b&=0xffffff;
  if((a>>20)!=8||(b>>20)!=8||(a&1)||(a&0x1ffff)>0x1fffc)continue;
  a&=0x1ffff;b&=0x1ffff;if(!pgm_idle_zero(a,b))continue;
  if(match)*match=(pgm_idle_descriptor){pc,a,b};
  unsigned n=(budget-56)/56;++h1_pgm_idle_stats.calls;h1_pgm_idle_stats.loops+=n;h1_pgm_idle_stats.cycles+=n*56;
  return n*56;
 }
 return 0;
}
static unsigned pgm_idle_cycles(unsigned budget){
 return pgm_idle_at(M68000_regs.pc,budget,M68000_regs.ccr,0);
}
static void pgm_idle_branch(unsigned pc,const uint8_t *code){
 /* A68K MemoryBanking already publishes its live instruction count before
  * calling change_pc, and reloads it afterward. No extra assembly callback
  * or live register changes are needed. The current jump's timing has not
  * necessarily been deducted yet: reserve 512 T states, well beyond the
  * largest 68000 jump/exception cost, plus one real complete 56-T loop. */
 if(!pgm_cpu_running||!h1_pgm_idle_branch_enabled||!h1_pgm_idle_enabled||
    m68k_ICount<624||M68000_regs.irq)return;
 unsigned batch=0;
 for(unsigned i=0;i<2;i++)if(pgm_branch_cache[i].pc==pc){
  pgm_idle_descriptor *d=&pgm_branch_cache[i];
  if(!pgm_idle_zero(d->a,d->b))return;
  batch=((unsigned)m68k_ICount-512-56)/56*56;
  ++h1_pgm_idle_stats.calls;h1_pgm_idle_stats.loops+=batch/56;h1_pgm_idle_stats.cycles+=batch;
  break;
 }
 if(!batch){
  /* Most taken branches do not target TST.L/B absolute-long. Avoid the
   * multi-position signature check until that cheap opcode filter matches. */
  unsigned opcode=*(const uint16_t*)code;
  if(opcode!=0x4ab9&&opcode!=0x4a39)return;
  pgm_idle_descriptor d;
  batch=pgm_idle_at(pc,(unsigned)m68k_ICount-512,0,&d);
  /* Only immutable BIOS/program code may cache a validated signature.
   * Guest RAM fixtures and self-modifying code are checked on every visit.
   * cpu_68k_reset invalidates the two entries before a newly loaded game. */
  if(batch&&(pc<0x20000||(pc>=0x100000&&pc<0x4f0000)))
   pgm_branch_cache[pgm_branch_next++&1]=d;
 }
 if(batch){
  m68k_ICount-=(int)batch;++h1_pgm_branch_stats.calls;
  h1_pgm_branch_stats.loops+=batch/56;h1_pgm_branch_stats.cycles+=batch;
 }
}
unsigned h1_pgm_dbra_batch(const uint16_t *code,unsigned counter_offset,unsigned budget){
 if(!h1_pgm_dbra_active||!h1_pgm_dbra_enabled||M68000_regs.irq||budget<560)return 0;
 ++h1_pgm_dbra_stats.calls;
 unsigned pc=((uintptr_t)code-(uintptr_t)OP_ROM)&0xffffff;
 if(counter_offset>=32||(counter_offset&3)||(pc&65535)>65530||
    h1_pgm_code(pc)!=(const uint8_t*)code)goto reject;
 unsigned op=code[0],kind=op&0xf1f8,dr=counter_offset/4;
 if(code[1]!=(0x51c8|dr)||code[2]!=0xfffc)goto reject;
 unsigned copy=kind==0x20d8||kind==0x30d8;
 if(!copy&&kind!=0x20c0&&kind!=0x30c0)goto reject;
 unsigned ar=(op>>9)&7,sr=op&7,width=(op>>12)==2?4:2;
 if((copy&&ar==sr)||(!copy&&sr==dr))goto reject;
 unsigned cycles=10+(copy?(width==4?20:12):(width==4?12:8));
 unsigned n=(budget-512)/cycles,count=M68000_regs.d[dr]&65535;
 if(n>count)n=count;if(!n)goto reject;
 unsigned a=M68000_regs.a[ar]&0xffffff,b=copy?M68000_regs.a[sr]&0xffffff:0;
 if((a>>20)!=8||(a&1)||(copy&&((b>>20)!=8||(b&1))))goto reject;
 unsigned dst=a&0x1ffff,src=b&0x1ffff,bytes=n*width;
 if(bytes>0x20000-dst||(copy&&bytes>0x20000-src))goto reject;
 if(copy&&src<dst+bytes&&dst<src+bytes)goto reject;
 if((pc>>20)==8){unsigned at=pc&0x1ffff;if(dst<at+6&&at<dst+bytes)goto reject;}
 uint8_t *ram=h1_pgm_bus_pages[0x80];
 uint16_t *out=(uint16_t*)(ram+dst),*in=(uint16_t*)(ram+src);
 unsigned value=M68000_regs.d[sr];
 for(unsigned i=0;i<n;i++){
  if(width==4){unsigned hi=copy?in[0]:value>>16,lo=copy?in[1]:value;
   out[0]=hi;out[1]=lo;out+=2;in+=2;
  }else{*out++=copy?*in++:value;}
 }
 M68000_regs.a[ar]+=bytes;if(copy)M68000_regs.a[sr]+=bytes;
 M68000_regs.d[dr]=(M68000_regs.d[dr]&0xffff0000u)|((count-n)&65535);
 ++h1_pgm_dbra_stats.hits;h1_pgm_dbra_stats.loops+=n;h1_pgm_dbra_stats.cycles+=n*cycles;
 /* Live MOVE flags are deliberately untouched: the terminal MOVE and DBRA
  * remain real, including final flags and the original exit timing. */
 return n*cycles;
reject:
 ++h1_pgm_dbra_stats.rejected;return 0;
}
#endif
volatile unsigned h1_bus_fast=H1_FAST_BUS;
uint8_t *volatile h1_bus_ram=memory.ram;
volatile unsigned h1_idle_enabled=H1_IDLE;
volatile struct {unsigned magic,version,calls,batched,cycles,tests,failures;} h1_idle_stats={0x314c4449,1};
static uint8_t *code_pointer(unsigned pc){
 pc&=0xffffff;
 if(pc&1)return 0;
 switch(pc>>20){
 case 0:if(memory.rom.cpu_m68k.p&&(pc&0xfffff)+1<memory.rom.cpu_m68k.size)return memory.rom.cpu_m68k.p+(pc&0xfffff);break;
 case 1:return memory.ram+(pc&0xffff);
 case 2:if(bankaddress+(pc&0xfffff)+1<memory.rom.cpu_m68k.size)return memory.rom.cpu_m68k.p+bankaddress+(pc&0xfffff);break;
 case 12:if(memory.rom.bios_m68k.p&&(pc&0x1ffff)+1<memory.rom.bios_m68k.size)return memory.rom.bios_m68k.p+(pc&0x1ffff);break;
 }
 return 0;
}
static unsigned idle_cycles(unsigned budget){
#if H1_PGM
 if(h1_pgm_active)return pgm_idle_cycles(budget);
#endif
 /* Only a RAM byte can be stable until the next scanline event. No MMIO,
  * instruction side effects, fixed game addresses or skipped IRQ delivery.
  * Leave >=one whole loop to the original core: it produces the exact final
  * flags, PC, full-PC metadata and instruction-boundary cycle overshoot. */
 if(!h1_idle_enabled||budget<44||M68000_regs.irq)return 0;
 unsigned pc=M68000_regs.pc&0xffffff;uint8_t *p=code_pointer(pc);
 if(!p)return 0;
 if(*(uint16_t*)p==0x67fa){
  if(!(M68000_regs.ccr&4)||pc<4)return 0;
  pc-=4;p=code_pointer(pc);if(!p)return 0;
 }
 /* Original A68K fetches sequential instructions from its current host base.
  * Never batch across a mirrored RAM/bank boundary. */
 if((pc&0xffff)>0xfffa)return 0;
 unsigned opcode=*(uint16_t*)p;
 if((opcode&0xfff8)!=0x4a28)return 0;
 uint8_t *extension=code_pointer(pc+2),*branch=code_pointer(pc+4);
 if(!extension||!branch||*(uint16_t*)branch!=0x67fa)return 0;
 unsigned address=(M68000_regs.a[opcode&7]+(int16_t)*(uint16_t*)extension)&0xffffff;
 if((address>>20)!=1||memory.ram[(address&0xffff)^1])return 0;
 return ((budget-22)/22)*22;
}
#if H1_SPEED_CORE
volatile unsigned h1_scan_hle=1;
volatile h1_scan_stats_t h1_scan_stats={0x314e4353,1};
static unsigned scan_cycles(unsigned budget){
#if H1_PGM
 if(h1_pgm_active)return 0;
#endif
 /* MOVE.W/L (An)+,Dn; BEQ -4 scans zero words in privately owned guest RAM.
  * Consume only complete zero iterations and retain one for the original
  * interpreter's flags, PC metadata and exact final cycle overshoot. */
 if(!h1_scan_hle||budget<36||M68000_regs.irq||(M68000_regs.pc&0xff000000u))return 0;
 unsigned pc=M68000_regs.pc&0xffffff;uint8_t *p=code_pointer(pc);
 if(!p)return 0;
 if(*(uint16_t*)p==0x67fc){
  if(!(M68000_regs.ccr&4)||pc<2)return 0;
  pc-=2;p=code_pointer(pc);if(!p)return 0;
 }
 if((pc&65535)>65532)return 0;
 unsigned op=*(uint16_t*)p,kind=op&0xf1f8;
 if(kind!=0x3018&&kind!=0x2018)return 0;
 uint8_t *b=code_pointer(pc+2);if(!b||*(uint16_t*)b!=0x67fc)return 0;
 unsigned ar=op&7,dr=(op>>9)&7,address=M68000_regs.a[ar]&0xffffff;
 if((address>>20)!=1||(address&1))return 0;
 unsigned width=kind==0x3018?2:4,cycles=width==2?18:22;
 if(budget<cycles*2)return 0;
 unsigned limit=(budget-cycles)/cycles,offset=address&65535;
 unsigned available=(65536-offset)/width;if(limit>available)limit=available;
 const uint16_t *s=(const uint16_t*)(memory.ram+offset);unsigned n=0;
 while(n<limit){if(s[0]||(width==4&&s[1]))break;s+=width/2;++n;}
 if(!n)return 0;
 /* Every omitted iteration includes a taken BEQ. A68K records its target
  * in FULLPC; the remaining real iteration may leave through an untaken
  * branch, so it must not inherit the pre-loop FULLPC value. */
 M68000_regs.fullpc=pc;
 M68000_regs.a[ar]+=n*width;
 M68000_regs.d[dr]=width==2?M68000_regs.d[dr]&0xffff0000u:0;
 ++h1_scan_stats.calls;h1_scan_stats.words+=n*(width/2);h1_scan_stats.cycles+=n*cycles;
 return n*cycles;
}
#endif
static void change_pc(unsigned pc){
 pc&=0xffffff;uint8_t *p=0;
#if H1_PGM
 if(h1_pgm_active){p=h1_pgm_code(pc);goto mapped;}
#endif
 switch(pc>>20){
 case 0:p=memory.rom.cpu_m68k.p+(pc&0xfffff);break;
 case 1:p=memory.ram+(pc&0xffff);break;
 case 2:if(bankaddress+(pc&0xfffff)<memory.rom.cpu_m68k.size)p=memory.rom.cpu_m68k.p+bankaddress+(pc&0xfffff);break;
 case 12:p=memory.rom.bios_m68k.p+(pc&0x1ffff);break;
 }
mapped:
 if(!p){h1_arcade_log("A68K_BAD_FETCH pc=%08x",pc);h1_arcade_escape(110);}
 OP_ROM=(unsigned char*)((uintptr_t)p-pc);OP_RAM=OP_ROM;
#if H1_PGM
 /* Keep ordinary branches out of the larger signature/cache helper. */
 if(h1_pgm_active&&pgm_cpu_running&&h1_pgm_idle_branch_enabled&&m68k_ICount>=624){
  unsigned opcode=*(const uint16_t*)p;
  if(opcode==0x4ab9||opcode==0x4a39)pgm_idle_branch(pc,p);
 }
#endif
}
static int ack(int level){(void)level;M68000_regs.irq=0;return -1;}
void cpu_68k_reset(void){
#if H1_PGM
 pgm_branch_cache[0].pc=pgm_branch_cache[1].pc=~0u;pgm_branch_next=0;
#endif
#if H1_SPEED_CORE
 refresh_rom_map();
#endif
 memset(&M68000_regs,0,sizeof M68000_regs);M68000_RESET();
 M68000_regs.ack=ack;M68000_regs.a[7]=m68k_read_memory_32(0);
 M68000_regs.pc=m68k_read_memory_32(4);M68000_regs.srh=0x27;
 change_pc(M68000_regs.pc);
}
void cpu_68k_init(void){
#if H1_SPEED_CORE
 refresh_rom_map();
#endif
 uintptr_t handlers[]={0,(uintptr_t)m68k_read_memory_8,(uintptr_t)m68k_read_memory_16,(uintptr_t)m68k_read_memory_32,
  (uintptr_t)m68k_write_memory_8,(uintptr_t)m68k_write_memory_16,(uintptr_t)m68k_write_memory_32,(uintptr_t)change_pc,
  (uintptr_t)m68k_read_memory_8,(uintptr_t)m68k_read_memory_16,(uintptr_t)m68k_read_memory_32,
  (uintptr_t)m68k_read_memory_16,(uintptr_t)m68k_read_memory_32};
 memcpy(a68k_memory_intf,handlers,sizeof handlers);
}
int cpu_68k_run(uint32_t n){
#if H1_PGM
 h1_pgm_banking_active=h1_pgm_banking_enabled&&h1_pgm_active&&mem_amask==0xffffff;
 h1_pgm_dbra_active=h1_pgm_dbra_enabled&&h1_pgm_active&&mem_amask==0xffffff;
#endif
#if H1_FAST_BUS
 /* One mode update per scanline, rather than a flag load at every bus access. */
 h1_bus_ram=h1_bus_fast?memory.ram:0;
#if H1_PGM
 if(h1_pgm_active)h1_bus_ram=0;
#endif
#if H1_SPEED_CORE
 h1_bus_rom_active=h1_bus_fast&&h1_rom_fast_enabled;
 /* The shared leaf selects PGM from its own map. Keep the independent
  * Neo Geo video toggle intact, including its reference selftest pass. */
#endif
#endif
 ++h1_idle_stats.calls;
 unsigned batch=idle_cycles(n);
 if(batch){++h1_idle_stats.batched;h1_idle_stats.cycles+=batch;}
#if H1_SPEED_CORE
 if(!batch)batch=scan_cycles(n);
#endif
 requested=n;m68k_ICount=n-batch;
 if(M68000_regs.irq==0x80){total+=n;return 0;}
#if H1_PGM
 pgm_cpu_running=1;
#endif
 M68000_RUN();
#if H1_PGM
 pgm_cpu_running=0;
#endif
 int used=(int)n-m68k_ICount;total+=used;return used-(int)n;
}
void cpu_68k_interrupt(int n){M68000_regs.irq=n;}
uint32_t cpu_68k_getpc(void){return M68000_regs.pc;}
int cpu_68k_getcycle(void){return total+requested-m68k_ICount;}
void cpu_68k_bankswitch(uint32_t a){bankaddress=a;
#if H1_SPEED_CORE
 refresh_rom_map();
#endif
 change_pc(M68000_regs.pc);}

#if H1_CPU_SELFTEST
#include "bus_test_program.h"
volatile struct {unsigned magic,version,tests,failures;} h1_bus_stats={0x31535542,1};
#if H1_PGM
volatile struct {unsigned magic,version,tests,failures;} h1_pgm_bus_stats={0x32425550,1};
static void pgm_branch_fixture(uint8_t *work,unsigned test){
 memset(work,0,0x20000);uint16_t *p=(uint16_t*)(work+0x2000);
 for(unsigned i=0;i<20;i++)p[i]=0x4e71;
 p[0]=0x4ab9;p[1]=0x81;p[2]=0x2800;p[3]=0x6714;p[4]=0x4e72;p[5]=0x2700;
 p[14]=0x4a39;p[15]=0x81;p[16]=0x2900;p[17]=0x67dc;p[18]=0x4e72;p[19]=0x2700;
 if(test%9==0)work[0x12800]=1;if(test%11==0)work[0x12901]=1;
 if(test%17==0)p[0]=0x4a79;
 p=(uint16_t*)(work+0x2100);
 for(unsigned i=0;i<16;i++)p[i]=0x4e71;
 p[16]=0x4ef9;p[17]=0x80;p[18]=0x2000; /* ISR/prefix enters the idle loop. */
 p=(uint16_t*)(work+0x2200);p[0]=0x4e72;p[1]=0x2700;
 /* The A68K exception interface supports its VBR field. Point a synthetic
  * vector table at plain RAM instead of patching BIOS or invoking game I/O. */
 p=(uint16_t*)(work+0x78);p[0]=0x80;p[1]=0x2100;
}
static unsigned pgm_dbra_fixture(uint8_t *work,unsigned test,context *initial){
 memset(work,(test*37)&255,0x20000);memset(initial,0,sizeof *initial);
 unsigned value=test*1664525u+1013904223u;
 for(unsigned i=0;i<8;i++){value=value*1664525u+1013904223u;initial->d[i]=value;initial->a[i]=value;}
 unsigned type=test&3,copy=type<2,width=(type&1)?2:4,dr=(test>>2)&7;
 unsigned ar=(test>>4)&7,sr=copy?(ar+1)&7:(dr+1)&7;
 unsigned counts[]={0,1,2,3,7,31,255,1023,65535};
 initial->a[7]=0x81f000;initial->a[ar]=copy?0x812000:0x808000;
 if(copy)initial->a[sr]=0x804000;
 if(copy&&test%13==0)initial->a[ar]=initial->a[sr]+2;
 if(test%17==0){if(copy){ar=sr;initial->a[ar]=0x804000;}else sr=dr;}
 if(test%19==0)initial->a[ar]|=1;
 if(copy&&test%23==0)initial->a[sr]|=1;
 if(test%29==0)initial->a[ar]=0x81fff0;
 if(copy&&test%31==0)initial->a[sr]=0x81fffe;
 if(test%43==0){if(copy)initial->a[sr]=0x1800;else initial->a[ar]=0x101000;}
 if(copy&&test%47==0){initial->a[sr]=0x824000;initial->a[ar]=0x804000;}
 if(test&64){initial->a[ar]|=0xff000000u;if(copy)initial->a[sr]|=0xff000000u;}
 initial->d[dr]=(initial->d[dr]&0xffff0000u)|counts[(test/64)%9];
 initial->pc=0x802000+((test>>4)&1)*2;initial->ccr=test&31;
 initial->fullpc=0xab000000u|initial->pc;initial->srh=0x27;initial->ack=ack;
 if(test%53==0)initial->irq=1;
 uint16_t *p=(uint16_t*)(work+0x2000);p[-1]=0x4e71;
 p[0]=(width==4?0x2000:0x3000)|(ar<<9)|0xc0|(copy?0x18:0)|sr;
 p[1]=0x51c8|dr;p[2]=test%37==0?0xfffa:0xfffc;p[3]=0x4e71;p[4]=0x60fe;
 if(test%41==0)p[0]=0x4e71; /* Unsupported body stays in the interpreter. */
 unsigned limits[]={1,10,18,22,30,31,559,560,561,1024,2049,4096,166667,333333,57,8192};
 unsigned budget=limits[(test/4)%16];
 if(test%29==0||(copy&&test%31==0))budget=1+(test*83)%800;
 return budget;
}
int h1_pgm_bus_selftest(void){
 /* Run real A68K twice on physical H1. Preserve the complete game RAM and
  * CPU state. The synthetic fixture executes from RAM, never patched ROM. */
 uint8_t *work=h1_pgm_bus_pages[0x80],*saved=malloc(0x20000),*reference_ram=malloc(0x20000);
 if(!saved||!reference_ram){free(saved);free(reference_ram);return 0;}
 memcpy(saved,work,0x20000);context old=M68000_regs;
 unsigned oldtotal=total,oldrequested=requested,oldfast=h1_bus_fast,oldidle=h1_pgm_idle_enabled;
 unsigned oldbranch=h1_pgm_idle_branch_enabled,oldbank=h1_pgm_banking_enabled;
 unsigned oldbankactive=h1_pgm_banking_active;int oldcount=m68k_ICount,ok=1;
 unsigned olddbra=h1_pgm_dbra_enabled,olddbraactive=h1_pgm_dbra_active;
 h1_pgm_dbra_enabled=0;
 for(unsigned test=0;test<1024;test++){
  context initial,reference;memset(&initial,0,sizeof initial);
  unsigned value=test*1664525u+1013904223u;
  for(unsigned i=0;i<8;i++){value=value*1664525u+1013904223u;initial.d[i]=value;initial.a[i]=value;}
  unsigned high=value&0xff000000,mirror=0x800000+((test&7)<<17),boundary=test%4==0;
  unsigned offset=boundary?(unsigned[]){0,0xfffc,0xfffe,0x1fffc,0x1fffe}[(test/4)%5]:0x8000+(value&0x7e00);
  initial.a[0]=high|mirror|offset;initial.a[1]=high|mirror|0x18000;initial.a[7]=0x81f000;
  if(test%11==0){initial.a[0]=high|0x1800;boundary=1;}
  if(test%13==0){initial.a[0]=high|0x121ffe;boundary=1;}
  if(test%17==0){initial.a[0]=high|0x4efffe;boundary=1;}
  initial.pc=0x801400+(boundary?58:0);initial.ccr=value&31;initial.srh=0x27;initial.ack=ack;
  unsigned budget=1+value%1800,pattern=value>>24;
  memset(work,pattern,0x20000);memcpy(work+0x1400,bus_test_program,sizeof bus_test_program);
  M68000_regs=initial;change_pc(initial.pc);h1_bus_fast=0;h1_pgm_banking_enabled=0;
  int expected=cpu_68k_run(budget);reference=M68000_regs;memcpy(reference_ram,work,0x20000);
  memset(work,pattern,0x20000);memcpy(work+0x1400,bus_test_program,sizeof bus_test_program);
  M68000_regs=initial;change_pc(initial.pc);h1_bus_fast=1;h1_pgm_banking_enabled=1;
  int actual=cpu_68k_run(budget);++h1_pgm_bus_stats.tests;
  if(expected!=actual||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,work,0x20000)){
   ++h1_pgm_bus_stats.failures;ok=0;h1_arcade_log("PGM_BUS_FAIL case=%u pc=%08x/%08x budget=%u used=%d/%d address=%08x",test,reference.pc,M68000_regs.pc,budget,expected,actual,initial.a[0]);break;}
  if(h1_arcade_stats.stop){ok=0;break;}
 }
 for(unsigned test=0;ok&&test<1024;test++){
  memset(work,0,0x20000);uint16_t *p=(uint16_t*)(work+0x2000);
  for(unsigned i=0;i<20;i++)p[i]=0x4e71;
  p[0]=0x4ab9;p[1]=0x81;p[2]=0x2800;p[3]=0x6714;p[4]=0x4e72;p[5]=0x2700;
  p[14]=0x4a39;p[15]=0x81;p[16]=0x2900;p[17]=0x67dc;p[18]=0x4e72;p[19]=0x2700;
  if(test%9==0)work[0x12800]=1;if(test%11==0)work[0x12901]=1;
  if(test%17==0)p[0]=0x4a79; /* different operand size must not batch */
  memcpy(reference_ram,work,0x20000);
  context initial={0},reference;initial.pc=0x802000+(unsigned[]){0,6,28,34}[test&3];initial.a[7]=0x81f000;initial.srh=0x27;initial.ccr=test&31;initial.ack=ack;
  if(test%23==0)initial.irq=1;unsigned budget=test%64==0?166667:1+(test*83)%4096;
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_idle_enabled=0;h1_pgm_banking_enabled=0;
  int expected=cpu_68k_run(budget);reference=M68000_regs;
  /* The fixture only reads memory, but compare all work RAM as well. */
  unsigned clean=!memcmp(reference_ram,work,0x20000);memcpy(work,reference_ram,0x20000);
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_idle_enabled=1;h1_pgm_banking_enabled=1;
  int actual=cpu_68k_run(budget);++h1_pgm_idle_stats.tests;
  if(!clean||expected!=actual||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,work,0x20000)){
   ++h1_pgm_idle_stats.failures;ok=0;h1_arcade_log("PGM_IDLE_FAIL case=%u pc=%08x/%08x budget=%u used=%d/%d",test,reference.pc,M68000_regs.pc,budget,expected,actual);}
 }
 unsigned branch_hits=0,irq_hits=0;
 for(unsigned test=0;ok&&test<1024;test++){
  pgm_branch_fixture(work,test);
  context initial={0},reference;initial.pc=0x802100+2*(test&15);
  initial.a[7]=0x81f000;initial.srh=0x27;initial.ccr=test&31;initial.ack=ack;
  if(test%5==0){initial.pc=0x802200;initial.srh=0x20;initial.irq=6;initial.vbr=0x800000;}
  else if(test%23==0)initial.irq=1; /* A masked pending IRQ must decline. */
  unsigned limits[]={1,55,56,57,111,112,623,624,625,680,681,2048,4096,166667,333333};
  unsigned budget=test%16==15?1+(test*83)%8192:limits[test%16];
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_idle_enabled=0;h1_pgm_idle_branch_enabled=0;h1_pgm_banking_enabled=0;
  int expected=cpu_68k_run(budget);reference=M68000_regs;memcpy(reference_ram,work,0x20000);
  pgm_branch_fixture(work,test);
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_idle_enabled=1;h1_pgm_idle_branch_enabled=1;h1_pgm_banking_enabled=1;
  unsigned before=h1_pgm_branch_stats.calls;
  int actual=cpu_68k_run(budget);++h1_pgm_branch_stats.tests;
  if(h1_pgm_branch_stats.calls!=before){++branch_hits;if(initial.irq==6)++irq_hits;}
  if(expected!=actual||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,work,0x20000)){
   ++h1_pgm_branch_stats.failures;ok=0;
   h1_arcade_log("PGM_BRANCH_FAIL case=%u pc=%08x/%08x budget=%u used=%d/%d irq=%u",test,
    reference.pc,M68000_regs.pc,budget,expected,actual,initial.irq);
  }
 }
 if(ok&&(!branch_hits||!irq_hits)){++h1_pgm_branch_stats.failures;ok=0;}
 unsigned bank_hits=0,bank_mirrors=0,bank_rom=0;
 /* BRA through 64 KiB page edges and 128 KiB mirrored-RAM group edges.
  * Every fetched instruction is wholly within its original mapped page. */
 for(unsigned test=0;ok&&test<256;test++){
  unsigned which=test&3,src=(unsigned[]){0x80fffe,0x810000,0x81fffe,0x820000}[which];
  unsigned dst=(unsigned[]){0x810002,0x80fffc,0x820002,0x81fffc}[which];
  unsigned high=(test&4)?0xff000000u:0;
  memset(work,0,0x20000);
  *(uint16_t*)(work+(src&0x1ffff))=which&1?0x60fa:0x6002;
  uint16_t *p=(uint16_t*)(work+(dst&0x1ffff));p[0]=0x4e72;p[1]=0x2700;
  context initial={0},reference;initial.pc=src;initial.a[7]=0x81f000;
  initial.srh=0x27;initial.ccr=test&31;initial.ack=ack;
  /* JMP abs.l exercises masking high address bits without a signed-PC
   * fetch from an unconnected mirror. It ends at the same safe STOP. */
  if(high){initial.pc=0x802400;p=(uint16_t*)(work+0x2400);p[0]=0x4ef9;p[1]=(high|dst)>>16;p[2]=dst;}
  unsigned budget=(test&8)?1:40;
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_banking_enabled=0;
  int expected=cpu_68k_run(budget);reference=M68000_regs;memcpy(reference_ram,work,0x20000);
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_banking_enabled=1;
  unsigned before=h1_pgm_banking_stats.hits;
  int actual=cpu_68k_run(budget);++h1_pgm_banking_stats.tests;
  if(h1_pgm_banking_stats.hits!=before)++bank_hits;
  if(which>=2)++bank_mirrors;
  if(expected!=actual||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,work,0x20000)){
   ++h1_pgm_banking_stats.failures;ok=0;
   h1_arcade_log("PGM_BANK_FAIL case=%u pc=%08x/%08x used=%d/%d",test,reference.pc,M68000_regs.pc,expected,actual);
  }
 }
 for(unsigned test=0;ok&&test<64;test++){
  unsigned hole=test&1;uint8_t *oldpage=h1_pgm_bus_pages[0x81];
  memset(work,0,0x20000);uint16_t *p;
  context initial={0},reference;initial.a[7]=0x81f000;initial.srh=0x27;
  initial.ccr=test&31;initial.ack=ack;
  if(hole){
   initial.pc=0x80fffe;*(uint16_t*)(work+0xfffe)=0x6002;
   p=(uint16_t*)(work+0x10002);p[0]=0x4e72;p[1]=0x2700;
   /* A missing acceleration page must retain the authoritative C map. */
   h1_pgm_bus_pages[0x81]=0;
  }else{
   initial.pc=0x802400;initial.vbr=0x800000;
   p=(uint16_t*)(work+0x2400);p[0]=0x4ef9;p[1]=0x80;p[2]=0x8001;
   p=(uint16_t*)(work+0x0c);p[0]=0x80;p[1]=0x2600;
   p=(uint16_t*)(work+0x2600);p[0]=0x4e72;p[1]=0x2700;
  }
  unsigned budget=(test&2)?1:1000;
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_banking_enabled=0;
  int expected=cpu_68k_run(budget);reference=M68000_regs;memcpy(reference_ram,work,0x20000);
  /* The address-error fixture pushes an exception frame: erase it before
   * the optimized run so a missing write cannot inherit the reference. */
  memset(work+0x1ef00,0,0x100);
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_banking_enabled=1;
  unsigned before=h1_pgm_banking_stats.hits;
  int actual=cpu_68k_run(budget);++h1_pgm_banking_stats.tests;
  unsigned wrong_hit=hole&&h1_pgm_banking_stats.hits!=before;
  h1_pgm_bus_pages[0x81]=oldpage;
  if(wrong_hit||expected!=actual||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,work,0x20000)){
   ++h1_pgm_banking_stats.failures;ok=0;
   h1_arcade_log("PGM_BANK_GUARD_FAIL case=%u hole=%u pc=%08x/%08x used=%d/%d",test,hole,
    reference.pc,M68000_regs.pc,expected,actual);
  }
 }
 /* Execute only a single taken branch from read-only game ROM, with a
  * one-T-state budget. The target is prefetched but never executed, so this
  * tests real contiguous ROM pages without patching ROM or invoking I/O. */
 for(unsigned page=0x10;ok&&page<0x4f&&bank_rom<32;page++){
  for(unsigned i=0;i<128&&bank_rom<32;i++){
   unsigned pc=(page<<16)+(i<64?i*2:65536-128+(i-64)*2);
   const uint16_t *p=(const uint16_t*)h1_pgm_code(pc);unsigned op=*p,kind=op>>8;
   if(kind!=0x60&&kind!=0x66&&kind!=0x67)continue;
   int displacement=(int8_t)op;
   if(!displacement){if(!h1_pgm_code(pc+2))continue;displacement=(int16_t)p[1];}
   unsigned dst=(pc+2+displacement)&0xffffff;
   if((dst&1)||(dst>>16)==page)continue;
   const uint8_t *q=h1_pgm_code(dst);
   if(!q||(uintptr_t)q-dst!=(uintptr_t)p-pc)continue;
   context initial={0},reference;initial.pc=pc;initial.a[7]=0x81f000;
   initial.srh=0x27;initial.ccr=kind==0x67?4:0;initial.ack=ack;
   M68000_regs=initial;change_pc(initial.pc);h1_pgm_banking_enabled=0;
   int expected=cpu_68k_run(1);reference=M68000_regs;memcpy(reference_ram,work,0x20000);
   M68000_regs=initial;change_pc(initial.pc);h1_pgm_banking_enabled=1;
   unsigned before=h1_pgm_banking_stats.hits;
   int actual=cpu_68k_run(1);++h1_pgm_banking_stats.tests;++bank_rom;
   if(h1_pgm_banking_stats.hits!=before)++bank_hits;
   if(expected!=actual||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,work,0x20000)){
    ++h1_pgm_banking_stats.failures;ok=0;
    h1_arcade_log("PGM_BANK_ROM_FAIL pc=%08x target=%08x used=%d/%d",pc,dst,expected,actual);break;
   }
  }
 }
 if(ok&&(!bank_hits||!bank_rom)){++h1_pgm_banking_stats.failures;ok=0;}
 unsigned dbra_hits=0;
 for(unsigned test=0;ok&&test<1024;test++){
  context initial,reference;unsigned budget=pgm_dbra_fixture(work,test,&initial);
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_dbra_enabled=0;
  int expected=cpu_68k_run(budget);reference=M68000_regs;memcpy(reference_ram,work,0x20000);
  pgm_dbra_fixture(work,test,&initial);
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_dbra_enabled=1;
  unsigned before=h1_pgm_dbra_stats.hits;
  int actual=cpu_68k_run(budget);++h1_pgm_dbra_stats.tests;
  if(h1_pgm_dbra_stats.hits!=before)++dbra_hits;
  if(expected!=actual||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,work,0x20000)){
   ++h1_pgm_dbra_stats.failures;ok=0;
   h1_arcade_log("PGM_DBRA_FAIL case=%u pc=%08x/%08x budget=%u used=%d/%d ccr=%x/%x",test,
    reference.pc,M68000_regs.pc,budget,expected,actual,reference.ccr,M68000_regs.ccr);
  }
 }
 /* Reject destinations aliasing the executing RAM code without letting the
  * reference interpreter overwrite its own instruction stream. These are
  * native guard checks: the only correct result is zero, with no changes. */
 for(unsigned test=0;ok&&test<4;test++){
  memset(work,0,0x20000);context initial={0};initial.pc=0x802000;initial.srh=0x27;
  initial.a[0]=0x804000;initial.a[1]=0x802000;initial.a[7]=0x81f000;initial.d[2]=64;
  uint16_t *p=(uint16_t*)(work+0x2000);
  p[0]=(test&1?0x3000:0x2000)|0x200|0xc0|(test&2?0:0x18);
  p[1]=0x51ca;p[2]=0xfffc;
  M68000_regs=initial;change_pc(initial.pc);h1_pgm_dbra_enabled=h1_pgm_dbra_active=1;
  memcpy(reference_ram,work,0x20000);unsigned skipped=h1_pgm_dbra_batch(p,8,4096);
  ++h1_pgm_dbra_stats.tests;
  if(skipped||memcmp(&initial,&M68000_regs,sizeof initial)||memcmp(reference_ram,work,0x20000)){
   ++h1_pgm_dbra_stats.failures;ok=0;h1_arcade_log("PGM_DBRA_GUARD_FAIL case=%u cycles=%u",test,skipped);
  }
 }
 if(ok&&!dbra_hits){++h1_pgm_dbra_stats.failures;ok=0;}
 memcpy(work,saved,0x20000);free(saved);free(reference_ram);M68000_regs=old;change_pc(old.pc);
 total=oldtotal;requested=oldrequested;m68k_ICount=oldcount;h1_bus_fast=oldfast;h1_pgm_idle_enabled=oldidle;
 h1_pgm_idle_branch_enabled=oldbranch;
 h1_pgm_banking_enabled=oldbank;h1_pgm_banking_active=oldbankactive;
 h1_pgm_dbra_enabled=olddbra;h1_pgm_dbra_active=olddbraactive;
 h1_arcade_log("PGM_DBRA_TEST cases=%u failures=%u hit_cases=%u loops=%u",h1_pgm_dbra_stats.tests,
  h1_pgm_dbra_stats.failures,dbra_hits,h1_pgm_dbra_stats.loops);
 h1_arcade_log("PGM_BANK_TEST cases=%u failures=%u hit_cases=%u mirror_cases=%u rom_cases=%u",h1_pgm_banking_stats.tests,
  h1_pgm_banking_stats.failures,bank_hits,bank_mirrors,bank_rom);
 h1_arcade_log("PGM_BRANCH_TEST cases=%u failures=%u hit_cases=%u irq_hit_cases=%u",h1_pgm_branch_stats.tests,
  h1_pgm_branch_stats.failures,branch_hits,irq_hits);
 h1_arcade_log("PGM_IDLE_TEST cases=%u failures=%u",h1_pgm_idle_stats.tests,h1_pgm_idle_stats.failures);
 h1_arcade_log("PGM_BUS_TEST cases=%u failures=%u",h1_pgm_bus_stats.tests,h1_pgm_bus_stats.failures);h1_arcade_log_flush();return ok;
}
#endif
static unsigned test_random=4740;
static unsigned random_word(void){test_random=test_random*1664525u+1013904223u;return test_random;}
#if H1_FAST_BUS
static int bus_selftest(uint16_t *program){
 static uint8_t reference_ram[65536];
 const unsigned base=0x1400;memcpy(program+base/2,bus_test_program,sizeof bus_test_program);
 /* Autovector IRQ1: write RAM, then return through the real RTE/stack path. */
 program[0x64/2]=0;program[0x66/2]=0x1600;program[0x1600/2]=0x2280;program[0x1602/2]=0x4e73;
 for(unsigned test=0;test<2048;test++){
  context initial,reference;memset(&initial,0,sizeof initial);
  for(unsigned i=0;i<8;i++){initial.d[i]=random_word();initial.a[i]=random_word();}
  unsigned mirror=0x100000+((random_word()%15)<<16),high=random_word()&0xff000000;
  unsigned boundary=test%4==0,offset=boundary?(unsigned[]){0,0xfffc,0xfffe,0x7ffe}[(test/4)&3]:(random_word()&0xfe00)+0x80;
  initial.a[0]=high|mirror|offset;initial.a[1]=high|mirror|0x8000;
  if(test%23==0){boundary=1;initial.a[0]=0x1800;} /* ignored ROM stores and normal ROM reads */
#if H1_SPEED_CORE
  if(test%29==0){boundary=1;initial.a[0]=high|0x201800;}
  if(test%31==0){boundary=1;initial.a[0]=0x1ffe;} /* guarded fallback at a short ROM tail */
#endif
  initial.a[7]=0x10f000;initial.pc=base+(boundary?58:0);initial.ccr=random_word()&31;
  initial.srh=0x27;initial.ack=ack;if(test%17==0){initial.irq=1;initial.srh=0x20;}
  program[0x1800/2]=(uint16_t)random_word();program[0x1802/2]=(uint16_t)random_word();
  unsigned budget=1+random_word()%1800,pattern=random_word()&255;
  memset(memory.ram,pattern,sizeof memory.ram);M68000_regs=initial;change_pc(initial.pc);
  h1_idle_enabled=0;h1_bus_fast=0;int expected=cpu_68k_run(budget);reference=M68000_regs;
  memcpy(reference_ram,memory.ram,sizeof reference_ram);
  memset(memory.ram,pattern,sizeof memory.ram);M68000_regs=initial;change_pc(initial.pc);
  h1_bus_fast=1;int got=cpu_68k_run(budget);++h1_bus_stats.tests;
  if(expected!=got||memcmp(&reference,&M68000_regs,sizeof reference)||memcmp(reference_ram,memory.ram,sizeof reference_ram)){
   ++h1_bus_stats.failures;h1_arcade_log("BUS_TEST_FAIL case=%u budget=%u pc=%08x/%08x used=%d/%d a0=%08x",test,budget,reference.pc,M68000_regs.pc,expected,got,initial.a[0]);return 0;
  }
 }
 h1_arcade_log("BUS_TEST_OK cases=%u full_context_and_RAM=1",h1_bus_stats.tests);return 1;
}
#endif
#if H1_SPEED_CORE
#include "video_bus_selftest.h"
#include "scan_hle_selftest.h"
#endif
int h1_cpu_selftest(void){
 /* Both paths execute the actual assembled MIPS A68K on the physical H1.
  * Test code is original TST/BEQ/NOP/STOP, never extracted game code. */
 static uint16_t program[(0x2000+4)/2];
 for(unsigned i=0;i<sizeof program/sizeof program[0];i++)program[i]=0x4e71;
 memory.rom.cpu_m68k.p=(uint8_t*)program;memory.rom.cpu_m68k.size=sizeof program-4;
 cpu_68k_init();
 for(unsigned test=0;test<4096;test++){
  context initial,reference;memset(&initial,0,sizeof initial);
  for(unsigned i=0;i<8;i++){initial.d[i]=random_word();initial.a[i]=random_word();}
  unsigned reg=test&7,base=0x1000+((test&31)*16),address=0x100800+(random_word()&0xf000);
  int16_t displacement=(int16_t)random_word();
  initial.a[reg]=address-(int32_t)displacement;
  initial.a[7]=reg==7?initial.a[7]:0x10f000;
  initial.pc=base+((test&8)?4:0);initial.ccr=random_word()&31;
  initial.srh=0x27;initial.ack=ack;
  unsigned value=(test%5)?0:(random_word()&255)|1;
  memory.ram[(address&65535)^1]=(uint8_t)value;
  program[base/2]=0x4a28|reg;program[base/2+1]=(uint16_t)displacement;program[base/2+2]=0x67fa;
  /* Branch fall-through stops with interrupts masked, keeping tests bounded. */
  program[base/2+3]=0x4e72;program[base/2+4]=0x2700;
  if(test%13==0)program[base/2+2]=0x66fa; /* must reject a different branch */
  if(test%17==0)initial.irq=1; /* pending masked IRQ must use original core */
  unsigned budget=1+random_word()%1800;
  M68000_regs=initial;change_pc(initial.pc);h1_idle_enabled=0;h1_bus_fast=0;int expected=cpu_68k_run(budget);reference=M68000_regs;
  M68000_regs=initial;change_pc(initial.pc);h1_idle_enabled=1;h1_bus_fast=H1_FAST_BUS;int got=cpu_68k_run(budget);
  ++h1_idle_stats.tests;
  if(expected!=got||memcmp(&reference,&M68000_regs,sizeof reference)){
   ++h1_idle_stats.failures;h1_arcade_log("IDLE_TEST_FAIL case=%u budget=%u used=%d/%d pc=%08x/%08x ccr=%x/%x full=%x/%x",test,budget,expected,got,reference.pc,M68000_regs.pc,reference.ccr,M68000_regs.ccr,reference.fullpc,M68000_regs.fullpc);return 0;
  }
 }
 h1_arcade_log("IDLE_TEST_OK cases=%u batched=%u cycles=%u",h1_idle_stats.tests,h1_idle_stats.batched,h1_idle_stats.cycles);
#if H1_FAST_BUS
 if(!bus_selftest(program))return 0;
#endif
#if H1_SPEED_CORE
 if(!h1_video_bus_selftest(program))return 0;
 if(!h1_scan_selftest(program))return 0;
#endif
 memset(&memory,0,sizeof memory);memset(&M68000_regs,0,sizeof M68000_regs);total=requested=0;h1_idle_enabled=H1_IDLE;h1_bus_fast=H1_FAST_BUS;
 return 1;
}
#endif
