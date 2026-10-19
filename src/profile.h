/* Optional exclusive wall-time sampling using the native 40 Hz clock. */
#ifndef H1_ARCADE_PROFILE_H
#define H1_ARCADE_PROFILE_H
enum { HP_OTHER,HP_INPUT,HP_68K,HP_Z80,HP_YM,HP_DRAW,HP_LCD,HP_IO,HP_WAIT,HP_TIMER,HP_PCM,HP_COUNT };
#if H1_PROFILE
void h1_profile_start(void);
void h1_profile_stop(void);
unsigned h1_profile_set(unsigned phase);
#else
static inline void h1_profile_start(void){}
static inline void h1_profile_stop(void){}
static inline unsigned h1_profile_set(unsigned phase){(void)phase;return 0;}
#endif
#endif
