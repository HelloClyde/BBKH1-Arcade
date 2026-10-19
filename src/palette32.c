/* Incremental palette conversion at guest writes, not once per output pixel. */
#include "palette32.h"
#include "memory.h"
#include "video.h"
volatile unsigned h1_palette32_enabled=1;
volatile h1_palette32_stats_t h1_palette32_stats={0x32504c48,1};
volatile h1_palette32_bench_t h1_palette32_bench={0x32424c48,1};
uint32_t h1_palette32[2][4096];
uint32_t *h1_palette32_current(void){return h1_palette32[current_pc_pal==(uint32_t*)memory.vid.pal_host[1]];}
void h1_palette32_store(uint32_t *where,uint32_t value){
 uintptr_t address=(uintptr_t)where,base=(uintptr_t)memory.vid.pal_host[0];
 unsigned bank=address<base||address>=base+4096*4;
 if(bank)base=(uintptr_t)memory.vid.pal_host[1];
 *where=value;
 h1_palette32[bank][(address-base)/4]=h1_expand565(value);
 ++h1_palette32_stats.stores;
}
