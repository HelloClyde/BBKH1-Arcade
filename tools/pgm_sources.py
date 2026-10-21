"""Extract KOV decryption from the already pinned FBA dependency, without modifying it."""
from pathlib import Path

def prepare_pgm(root: Path, build: Path, core: Path):
    upstream = root/'third_party/fba-a320/src/burn/pgm/pgm_crypt.cpp'
    text = upstream.read_text()
    text = text[text.index('static const unsigned char kov_tab'):text.index('static const unsigned char kovsh_tab')]
    text = text.replace('void pgm_kov_decrypt()', 'void h1_pgm_decrypt(unsigned char *program,unsigned bytes)')
    text = text.replace('nPGM68KROMLen', 'bytes').replace('PGM68KROM', 'program')
    target = build/'pgm_decrypt.c'
    target.write_text('/* Extracted from pinned FBA-A320 pgm_crypt.cpp; original terms apply. */\n'+text)
    sound=(root/'third_party/fba-a320/src/burn/ics2115.cpp').read_text()
    for header in ('burnint.h','msm6295.h','burn_sound.h','ics2115.h'):
        sound=sound.replace(f'#include "{header}"','')
    sound='#include "pgm_sound.h"\n'+sound[:sound.index('void ics2115_scan(')]
    sound=sound.replace('#define\tICS2115_RATE\t\t\t\t33075','#define ICS2115_RATE 8000')
    assert '#define ICS2115_RATE 8000' in sound
    sound=sound.replace('int /*length*/','int length')
    sound=sound.replace('unsigned int delta = chip->voice[osc].fc << 2;',
        'unsigned int delta = (uint64_t)chip->voice[osc].fc * 4 * 33075 / 8000;')
    sound=sound.replace('i<ICS2115_FRAME_BUFFER_SIZE','i<length')
    sound=sound.replace('chip->rom[badr|(adr >> 12)]','h1_pgm_sample(badr|(adr >> 12))')
    sound=sound.replace('pos+=nSoundDelta','pos+=65536')
    snd=build/'pgm_ics2115.c';snd.write_text(sound)
    # PGM uploads the complete Z80 program through the main CPU bus. Reuse
    # the existing interpreter, routing its 16-bit I/O ports to the ICS chip.
    p=core/'mamez80_interf.c';s=p.read_text()
    s='#include "pgm.h"\n#include "pgm_sound.h"\n'+s
    s=s.replace('    z80_port_write(port, value);','    if(h1_pgm_active){h1_pgm_z80_write(port,value);return;}\n    z80_port_write(port, value);')
    s=s.replace('    return z80_port_read(port);','    if(h1_pgm_active)return h1_pgm_z80_read(port);\n    return z80_port_read(port);')
    p.write_text(s)
    p=core/'mamez80/z80.c';s=p.read_text()
    s='#include "pgm.h"\n'+s
    s=s.replace('h1_z80_hle && h1_z80_poll_valid','h1_z80_hle && !h1_pgm_active && h1_z80_poll_valid')
    if '#include "z80_hle.h"' in s:
        s=s.replace('#include "z80_hle.h"','#include "z80_hle.h"\n#include "pgm_z80_hle.h"')
        marker='\t\tif(Z80_ICOUNT>0 && _PCD>=0x122'
        assert marker in s
        s=s.replace(marker,'\t\tif(h1_pgm_active && _PCD==0x329)h1_pgm_z80_hle_poll();\n'+marker)
    p.write_text(s)
    return [target,snd]
