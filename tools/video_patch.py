"""Keep sprite-column position updates even when a tile needs no drawing."""

def once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError('video patch context changed: ' + old[:90])
    return source.replace(old, new, 1)


def patch_video(source):
    split = source.index('void draw_screen_scanline(')
    full, raster = source[:split], source[split:]
    # Position and vertical zoom state must advance for every tile, including
    # invisible and invalid ones. Only the eventual draw/cache access is skipped.
    full = once(full, '''      if(tileno > memory.nb_of_tiles) {
        //printf("Tno %04x Tat %04x %d\\n",tileno,tileatr,memory.nb_of_tiles);
        continue;
      }
''', '')
    full = once(full,
        'if(sx >= -16 && sx + 15 < 336 && sy >= 0 && sy + 15 < 256)',
        'if(tileno < memory.nb_of_tiles && sx >= -16 && sx + 15 < 336 && sy >= 0 && sy + 15 < 256)')
    raster = once(raster, '      penusage = PEN_USAGE(tileno);',
        '      if(tileno >= memory.nb_of_tiles)continue; /* H1: bound raster access */\n      penusage = PEN_USAGE(tileno);')
    # No early continue here: full-frame drawing advances sy below this block.
    # Invisible tiles still avoid page IO and fall through the empty switch.
    guard = '        if(memory.vid.spr_cache.data) {'
    full = once(full, guard,
        '        if(memory.vid.spr_cache.data && penusage != TILE_INVISIBLE) {')
    raster = once(raster, '      if(memory.vid.spr_cache.data) {',
        '      if(memory.vid.spr_cache.data && penusage != TILE_INVISIBLE) {')
    return full + raster
