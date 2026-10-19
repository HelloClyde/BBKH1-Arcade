# H1 Arcade notices

The H1 frontend, bus adapter, checked package reader, converter, build scripts
and test programs are provided under GPL-2.0-or-later, with the additional
independent-module linking permission in LICENSE-EXCEPTION, unless a file states otherwise.
Existing H1 entry, libc, PCM and keyboard adapters were captured from this
workspace's H1 GB/GBC/GBA and H1 PS1 ports; their original notices are retained.
The touch pause menu adapts the workspace GBA port's layout and calibrated
touch interaction. Its Noto Sans CJK SC bitmap glyph subset is licensed under
OFL-1.1, independently of the program code. The complete font license is in
`assets/licenses/NotoSansCJK-OFL.txt`; source provenance and regeneration are
recorded in `assets/README.md`.

This executable combines components with different licenses. In particular,
the historic MAME-derived Z80 and YM2610 components have noncommercial terms.
Do not describe the complete combined executable as uniformly MIT or GPL-only.

- GnGeo: Peponas Mathieu, Steward Fu and contributors; pinned Apaczer fork.
  The upstream COPYING and file notices are retained. Its README describes
  the combination of GPL code and independently licensed MAME components.
- Z80: Juergen Buchmueller, portable Z80 V3.3; the source header permits
  redistribution with credit for noncommercial purposes. Its complete notice
  remains in `third_party/gngeo/src/mamez80/z80.c`.
- YM2610: Tatsuyuki Satoh, Jarek Burczynski and MAME contributors; the original
  notices remain in `third_party/gngeo/src/ym2610/ym2610.c`.
- Musashi: Karl Stenerud and contributors, MIT-style permission notice retained
  in upstream headers. This build enables only the 68000; no FPU/MMU is linked.
- A68K MIPS core: Mike Coates, Darren Olafson, Manuel Geran,
  Dmitry Smagin and contributors. The MIPS32r1 generator is taken unmodified
  from `dmitrysmagin/fba-a320`, commit
  `68af7cc0065757c688595adc409f5d47977793ae`. Its full notices are retained in
  `third_party/fba-a320/src/cpu/a68k/mips32r1/fba_make68k.c` and emitted assembly.
  The original author notices are included in assets/licenses/A68K-copyright.txt.
  The fast-bus build patches a build copy to emit guarded work-RAM accesses;
  the pinned upstream checkout is unchanged. The release source archive includes
  the pinned upstream sources and our transformation scripts. Upstream code is
  not relabelled under the H1 port's GPL license. See docs/licensing.md.
- H1 SDK: MrDefinition1999 and contributors, Apache-2.0. Fixed commit is
  recorded in `UPSTREAM.json` and the BDA build metadata.
- Device ZIP inflater: miniz by Rich Geldreich / Tenacious Software,
  RAD Game Tools and Valve Software, under its retained MIT notice.
  The two unmodified files in `src/import_vendor/` are copied from
  `rofl0r/gnuboy` commit `c367bb4ba96fb07cd62f72f5ecb43aeff7012564`.
  See `assets/licenses/miniz-MIT.txt` and `docs/device-import.md`.

`tools/prepare.py` applies documented changes only to build copies: H1 page
cache hooks, bounds checks, odd-lane memory-card byte access, bounded Z80 flag
tables, sample rate / timer configuration, integer initialization tables and
source compatibility fixes. Build copies also skip cache requests for fully
transparent tiles and test timer activity before a software double comparison.
The upstream repositories remain unmodified.
The optional speed-core preset adds guarded shared MIPS RAM/ROM/VRAM bus
leaves, scheduled RAM reads, a signature-gated Z80 polling block and tail,
and ordinary-RAM zero-scan loop batching. Original cycle and complete register
semantics are checked against the original interpreters on the physical H1.
Its conservative timer comparison filter retains the original double clock
and deadline bits. Native CPU clocks and interrupt boundaries remain unchanged.
Native-size MXU output can write directly to the LCD RAM, using its cached
alias only after ownership, alignment and range checks, with explicit data-cache
writeback/invalidation before LCD DMA reads. The preset selects fixed skip1,
native 320x224 and optional 8 kHz synthesis with 32 kHz mono output; adaptive
display scheduling is disabled by default. The final binary passed the native
selftests and a bounded full-speed KOF97 battle window via H1Debug. These checks
do not establish full speed for every stage, display/audio setting or game.
The final evidence is recorded separately from the earlier speed investigation.
The optional scale-output preset adds an exact 320-to-362 MXU expansion,
selecting repeated pixels directly from converted registers while retaining
dirty blocks and explicit cache publication. Its startup differential tests
run on the physical H1. The separate 408 MHz option reuses the workspace's
PS1 clock_control.c/h without changes, retains their notices, stops native
PCM/DMA at transitions, and restores the original clocks before exit saves.
Its three recorded gameplay windows include an initial 2-underrun window;
later full-speed windows do not establish uniform speed or long-term stability.
Clock restoration caused one software USB reenumeration; retained state was
recovered after explicit PC reconnection, without repeating the exit action.
The H1 cache now uses a bounded fully associative LRU with a direct page index.
The frontend precomputes horizontal scaling coordinates and reduces drawing
during the initial BIOS self-test without skipping CPU or RTC work.
The optional adapter batches RAM-only TST.B/BEQ idle loops within the existing
scanline budget, retaining original instruction cycles and interrupt boundaries.
Its differential test executes both paths on the physical H1. The RGB565
presenter compares 16-source-pixel segments and writes only changed LCD ranges;
mode, buffer, explicit clear and optimization-toggle changes invalidate its cache.
The fixed framebuffer and pixel history use the checked BDA memory window.
Optional O3 and C-core LTO builds retain upstream notices. The fast-bus build
copy adds MIPS work-RAM loads/stores with byte-lane, alignment and boundary
guards; all other bus accesses retain their original callbacks. A null RAM
mapping selects the original path for physical differential checks.
The delivered read-only variant keeps the original write callbacks and limits
generated fast paths to work-RAM reads. A further 2048 physical cases compare
cycle overshoot, full CPU context and all work RAM, including IRQ/RTE handling.
The optional drawing-template copy specializes opaque, full-width rows, keeps
transparent and horizontally zoomed rows on the original path, and eliminates
tile modulo only after the existing caller bounds checks. Original and fast
drawing paths are compared on the host and on the physical H1 before loading
the cartridge. These additions do not lower guest CPU clocks or audio quality.
The opaque-row specialization was slower in a frozen-state physical benchmark
and is disabled in the delivered read-only variant; that variant removes only
the redundant tile modulo. The original optimization BDA remains available.
The PC graphics decoder is extracted from the same pinned GnGeo source.
Game/BIOS data is supplied locally by the user and is not redistributed.
The included test cartridge and its boot code are original test material.
