"""Generate device import metadata/decoders from the fixed GnGeo source."""
from pathlib import Path
import hashlib
from convert import SUPPORTED, driver, ROOT

def main():
    rows=['/* Generated from fixed GnGeo .drv metadata; see UPSTREAM.json. */',
          'typedef struct {const char *name;unsigned id,src,dest,size,crc;} import_rom;',
          'typedef struct {const char *name;unsigned sizes[10],count;const import_rom *roms;} import_driver;']
    for name in sorted(SUPPORTED):
        sizes, entries=driver(name)
        rows.append('static const import_rom rom_'+name+'[]={')
        rows.extend('{"%s",%u,%u,%u,%u,0x%08xu},'%entry for entry in entries)
        rows.append('};')
    rows.append('static const import_driver import_drivers[]={')
    for name in sorted(SUPPORTED):
        sizes, entries=driver(name)
        rows.append('{"%s",{%s},%u,rom_%s},'%(name,','.join(map(str,sizes)),len(entries),name))
    rows+=['};','#define IMPORT_DRIVER_COUNT (sizeof import_drivers/sizeof import_drivers[0])']
    (ROOT/'src/import_drivers.h').write_text('\n'.join(rows)+'\n',encoding='utf-8')
    source=(ROOT/'third_party/gngeo/src/roms.c').read_text()
    license='/* GnGeo graphics decoders from the fixed roms.c; GPL-2.0-or-later.\n * See third_party/gngeo/COPYING and UPSTREAM.json. */'
    tile=source[source.index('static int convert_roms_tile('):source.index('void convert_all_tile(')]
    (ROOT/'src/import_gfx.h').write_text(license+'\n/* Extracted by tools/generate_import_tables.py from roms.c SHA256 '+hashlib.sha256(source.encode()).hexdigest()+' */\n'
        +'typedef uint8_t Uint8;typedef uint32_t Uint32;\n#define TILE_INVISIBLE 1\n'+tile,encoding='utf-8')

if __name__=='__main__':main()
