"""Avoid work outside the H1 viewport without skipping sprite state updates."""
from video_patch import once


def patch_video(source):
    source = '#include "h1_render.h"\n' + source
    split = source.index('void draw_screen_scanline(')
    full, raster = source[:split], source[split:]
    full = once(full, '  SDL_FillRect(buffer, NULL, current_pc_pal[4095]);', '''  if(h1_render_visible && buffer->pitch==704){
    if(h1_render_visible==2 && !((uintptr_t)buffer->pixels&3))
      h1_render_clear_visible(buffer->pixels,current_pc_pal[4095]);
    else SDL_FillRect(buffer,&visible_area,current_pc_pal[4095]);
  }else SDL_FillRect(buffer,NULL,current_pc_pal[4095]);''')
    full = once(full,
        'if(tileno < memory.nb_of_tiles && sx >= -16 && sx + 15 < 336 && sy >= 0 && sy + 15 < 256)',
        '''if(tileno < memory.nb_of_tiles && sx >= -16 && sx + 15 < 336 && sy >= 0 && sy + 15 < 256
         && (!h1_render_visible || (sx+rzx>0 && sx<320 && sy+yskip>16 && sy<240)))''')
    # Fixed layer uses 8x8 characters; the first and last two rows are outside
    # [16,240). Apply after upstream's start/end calculation, before its loop.
    full = once(full, '  for(y = ystart; y < yend; y++)', '''  if(h1_render_visible){
    if(ystart<2)ystart=2;
    if(yend>30)yend=30;
  }
  for(y = ystart; y < yend; y++)''')
    return full + raster
