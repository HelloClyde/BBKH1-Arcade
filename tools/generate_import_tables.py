"""Generate device import metadata/decoders from the fixed GnGeo source."""
from pathlib import Path
import hashlib
from convert import driver, members_required, supported_drivers, ROOT

def main():
    rows=['/* Generated from fixed GnGeo .drv metadata; see UPSTREAM.json. */',
          'typedef struct {const char *name;unsigned id,src,dest,size,crc,bytes;} import_rom;',
          'typedef struct {const char *name;unsigned sizes[10],count;const import_rom *roms;} import_driver;']
    for name in supported_drivers():
        sizes, entries=driver(name)
        lengths=members_required(entries)
        rows.append('static const import_rom rom_'+name+'[]={')
        for entry in sorted(entries,key=lambda entry:entry[1]!=8):
            rows.append('{"%s",%u,%u,%u,%u,0x%08xu,%u},'%(*entry,lengths[entry[0].lower(),entry[-1]]))
        rows.append('};')
    rows.append('static const import_driver import_drivers[]={')
    for name in supported_drivers():
        sizes, entries=driver(name)
        rows.append('{"%s",{%s},%u,rom_%s},'%(name,','.join(map(str,sizes)),len(entries),name))
    rows+=['};','#define IMPORT_DRIVER_COUNT (sizeof import_drivers/sizeof import_drivers[0])']
    (ROOT/'src/import_drivers.h').write_text('\n'.join(rows)+'\n',encoding='utf-8',newline='\r\n')
    source=(ROOT/'third_party/gngeo/src/roms.c').read_text()
    license='/* GnGeo graphics decoders from the fixed roms.c; GPL-2.0-or-later.\n * See third_party/gngeo/COPYING and UPSTREAM.json. */'
    tile=source[source.index('static int convert_roms_tile('):source.index('void convert_all_tile(')]
    (ROOT/'src/import_gfx.h').write_text(license+'\n/* Extracted by tools/generate_import_tables.py from roms.c SHA256 '+hashlib.sha256(source.encode()).hexdigest()+' */\n'
        +'typedef uint8_t Uint8;typedef uint32_t Uint32;\n#define TILE_INVISIBLE 1\n'+tile,encoding='utf-8',newline='\r\n')

if __name__=='__main__':main()
