#ifndef H1_ARCADE_PRESENT_H
#define H1_ARCADE_PRESENT_H
#include <stdint.h>
extern volatile unsigned h1_present_dirty;
extern volatile unsigned h1_present_mxu;
extern volatile unsigned h1_present_direct;
extern volatile unsigned h1_present_cached;
extern volatile unsigned h1_present_scale;
int h1_scale_selftest(void);
typedef struct {unsigned magic,version,calls,pixels,tests,failures;} h1_mxu_stats_t;
extern volatile h1_mxu_stats_t h1_mxu_stats;
typedef struct {unsigned magic,version,request,state,loops,original_ticks,fast_ticks,original_hash,fast_hash,input_unchanged;} h1_present_bench_t;
extern volatile h1_present_bench_t h1_present_bench;
int h1_mxu_selftest(void);
void h1_present_rgb565(uint32_t *out,const uint16_t *source,unsigned pitch,unsigned mode,unsigned clear);
void h1_present_reset(void);
void h1_present_rgb32(uint32_t*,const uint32_t*,unsigned,unsigned,unsigned);
void h1_present32_reset(void);
#endif
