#ifndef H1_ARCADE_PORT_H
#define H1_ARCADE_PORT_H
#include <stdint.h>
typedef struct {
    uint32_t magic,version,state,error,frames,displayed,cache_hits,cache_misses;
    uint32_t tick_hz,raw_start,raw_end,rtc_start,rtc_end,stop,pc,audio_samples;
    uint32_t cache_bytes,checksum,keys,paused_frames;
} h1_arcade_stats_t;
extern volatile h1_arcade_stats_t h1_arcade_stats;
extern int h1_arcade_jump[12];
int h1_arcade_setjmp(int *) __attribute__((returns_twice));
void h1_arcade_longjmp(int *) __attribute__((noreturn));
void h1_arcade_escape(int) __attribute__((noreturn));
void h1_arcade_check_stop(void);
void h1_arcade_log(const char *,...);
void h1_arcade_log_flush(void);
unsigned h1_arcade_rtc(void) __attribute__((noinline));
unsigned get_ticks(void);
void pad_update(void);
int h1_neo_load(const char *);
void h1_neo_close(void);
void h1_neo_save(void);
uint8_t *h1_sprite_page(unsigned);
void h1_audio_render(unsigned);
void h1_audio_sync(void);
void h1_audio_reset(void);
#endif
