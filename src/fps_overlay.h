/* Small LCD FPS display, independent of guest rendering. GPL-2.0-or-later. */
#ifndef H1_FPS_OVERLAY_H
#define H1_FPS_OVERLAY_H
#include <stdint.h>

/* Restore the saved pixels before every real presenter call. This preserves
 * unchanged-row presenters without invalidating their full source image. */
void h1_fps_overlay_before_present(uint32_t *lcd);
/* Only gameplay frames contribute; reset after menus and diagnostic redraws. */
void h1_fps_overlay_reset(void);
void h1_fps_overlay_sample(unsigned tick,unsigned hz,unsigned frames,unsigned displayed);
/* Called after an actual gameplay output, never from a paused benchmark. */
void h1_fps_overlay_draw(uint32_t *lcd,unsigned enabled);
const char *h1_fps_overlay_text(void);
#endif
