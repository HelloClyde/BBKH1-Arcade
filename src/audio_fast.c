#include "audio_fast.h"
#if H1_SPEED_CORE
volatile unsigned h1_audio_quality=2;
volatile unsigned h1_audio_source_rate=8000;
#else
volatile unsigned h1_audio_quality=1;
volatile unsigned h1_audio_source_rate=16000;
#endif
volatile h1_audio_fast_stats_t h1_audio_fast_stats={0x31414648,1};
volatile h1_audio_bench_t h1_audio_bench={0x31424148,1};
