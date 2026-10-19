#ifndef H1_PALETTE32_H
#define H1_PALETTE32_H
#include <stdint.h>
static inline uint32_t h1_expand565(uint32_t p){return ((p&0xf800)<<8)|((p&0x7e0)<<5)|((p&31)<<3);}
static inline uint16_t h1_pack565(uint32_t p){return ((p>>8)&0xf800)|((p>>5)&0x7e0)|((p>>3)&31);}
typedef struct {unsigned magic,version,tests,failures,tiles,stores;} h1_palette32_stats_t;
extern volatile h1_palette32_stats_t h1_palette32_stats;
extern volatile unsigned h1_palette32_enabled;
extern uint32_t h1_palette32[2][4096];
uint32_t *h1_palette32_current(void);
void h1_palette32_store(uint32_t*,uint32_t);
void h1_palette32_surface(unsigned);
void h1_palette32_benchmark(void);
int h1_palette32_selftest(void);
unsigned h1_palette32_lcd_hash(void);
unsigned h1_palette32_mode(unsigned);
unsigned h1_palette32_compare_source(void);
extern int h1_sprite_tile_opaque(const unsigned*);
extern void h1_mxu_tile16_palette32(void*,const unsigned*,const unsigned*,unsigned);
typedef struct {unsigned magic,version,request,state,loops,ticks16,ticks32,hash16,hash32,input_unchanged,mode,compose_only,source_differences;} h1_palette32_bench_t;
extern volatile h1_palette32_bench_t h1_palette32_bench;
#endif
