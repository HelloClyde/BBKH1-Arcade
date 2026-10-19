#ifndef H1_SPEED_CORE_H
#define H1_SPEED_CORE_H
extern volatile unsigned h1_z80_hle,h1_video_bus_enabled,h1_rom_fast_enabled;
typedef struct {unsigned magic,version,tests,failures,loops,cycles,matched;} h1_z80_hle_stats_t;
extern volatile h1_z80_hle_stats_t h1_z80_hle_stats;
typedef struct {unsigned magic,version,tests,failures,calls;} h1_video_bus_stats_t;
extern volatile h1_video_bus_stats_t h1_video_bus_stats;
typedef struct {unsigned magic,version,tests,failures;} h1_timer_fast_stats_t;
extern volatile unsigned h1_timer_fast;
extern volatile h1_timer_fast_stats_t h1_timer_fast_stats;
int h1_timer_selftest(void);
typedef struct {unsigned magic,version,calls,words,cycles,tests,failures;} h1_scan_stats_t;
extern volatile unsigned h1_scan_hle;
extern volatile h1_scan_stats_t h1_scan_stats;
int h1_z80_hle_selftest(void);
void h1_z80_hle_init(void);
#endif
