/* GnGeo graphics decoders from the fixed roms.c; GPL-2.0-or-later.
 * See third_party/gngeo/COPYING and UPSTREAM.json. */
/* Extracted by tools/generate_import_tables.py from roms.c SHA256 c59e3c327260a1ddcdb506869ae7a3271aad8b11b7f4c5aa6d4ad84357e5dd1b */
typedef uint8_t Uint8;typedef uint32_t Uint32;
#define TILE_INVISIBLE 1
static int convert_roms_tile(Uint8 *g, int tileno)
{
  unsigned char swap[128];
  unsigned int *gfxdata;
  int x, y;
  unsigned int pen, usage = 0;
  gfxdata = (Uint32 *) & g[tileno << 7];

  memcpy(swap, gfxdata, 128);

  //filed=1;
  for(y = 0; y < 16; y++) {
    unsigned int dw;

    dw = 0;
    for(x = 0; x < 8; x++) {
      pen = ((swap[64 + (y << 2) + 3] >> x) & 1) << 3;
      pen |= ((swap[64 + (y << 2) + 1] >> x) & 1) << 2;
      pen |= ((swap[64 + (y << 2) + 2] >> x) & 1) << 1;
      pen |= (swap[64 + (y << 2)] >> x) & 1;
      //if (!pen) filed=0;
      dw |= pen << ((7 - x) << 2);
      //memory.pen_usage[tileno]  |= (1 << pen);
      usage |= (1 << pen);
    }
    *(gfxdata++) = dw;

    dw = 0;
    for(x = 0; x < 8; x++) {
      pen = ((swap[(y << 2) + 3] >> x) & 1) << 3;
      pen |= ((swap[(y << 2) + 1] >> x) & 1) << 2;
      pen |= ((swap[(y << 2) + 2] >> x) & 1) << 1;
      pen |= (swap[(y << 2)] >> x) & 1;
      //if (!pen) filed=0;
      dw |= pen << ((7 - x) << 2);
      //memory.pen_usage[tileno]  |= (1 << pen);
      usage |= (1 << pen);
    }
    *(gfxdata++) = dw;
  }

  //if ((usage & ~1) == 0) pen_usage|=(TILE_INVISIBLE<<((tileno&0xF)*2));
  /* TODO transpack support */
  if((usage & ~1) == 0) {
    return (TILE_INVISIBLE << ((tileno & 0xF) * 2));
  }
  else {
    return 0;
  }

}

