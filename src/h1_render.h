#ifndef H1_RENDER_H
#define H1_RENDER_H
#ifndef H1_SPEED_CORE
#define H1_SPEED_CORE 0
#endif
#ifndef H1_VISIBLE_RENDER
#define H1_VISIBLE_RENDER 0
#endif
extern volatile unsigned h1_render_visible;
#if H1_SPEED_CORE
extern volatile unsigned h1_render_compact;
#endif
void h1_render_clear_visible(void *pixels,unsigned color);
extern volatile unsigned h1_render_fast;
extern volatile unsigned h1_render_bench_only;
typedef struct {unsigned magic,version,tiles,palette_rebuilds,cache_hits,decodes;} h1_tile_mxu_stats_t;
extern volatile h1_tile_mxu_stats_t h1_tile_mxu_stats;
void h1_render_begin(void);
int h1_render_tile16(void *dst,const unsigned *gfx,const unsigned *pal,unsigned color,unsigned flags);
typedef struct {unsigned magic,version,opaque_rows,tests,failures;} h1_render_stats_t;
extern volatile h1_render_stats_t h1_render_stats;
typedef struct {unsigned magic,version,request,state,loops,original_ticks,fast_ticks,original_hash,fast_hash,input_unchanged,pixel_differences;} h1_render_bench_t;
extern volatile h1_render_bench_t h1_render_bench;
void h1_render_benchmark(void);
#endif
