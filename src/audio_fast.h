#ifndef H1_ARCADE_AUDIO_FAST_H
#define H1_ARCADE_AUDIO_FAST_H
#include <stdint.h>
extern volatile unsigned h1_audio_quality;
extern volatile unsigned h1_audio_source_rate;
typedef struct {unsigned magic,version,tests,failures,switches;} h1_audio_fast_stats_t;
extern volatile h1_audio_fast_stats_t h1_audio_fast_stats;
void h1_ym_set_rate(unsigned rate);
int h1_ym_rate_selftest(void);
int gba_audio_resampler_selftest(void);
void gba_audio_source_rate(unsigned rate);
typedef struct {unsigned magic,version,request,state,loops,ticks22,ticks16,samples22,samples16,restored,nonzero22,nonzero16;} h1_audio_bench_t;
extern volatile h1_audio_bench_t h1_audio_bench;
void h1_audio_benchmark(void);
#endif
