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

The v0.2.2 release separates automatic startup diagnostics from normal use.
H1_STARTUP_SELFTEST defaults to 0; only an explicit diagnostic build using
--cpu-selftest --startup-selftest runs the full native suite before the file
picker and after PGM loading. The older v0.2.1 image was observed spending
tens of seconds in these developer checks before showing the picker.
The new BDA is 1,240,380 bytes, SHA256
4663b6c25059c31b5a05820292d1fc6572235a2a965023200b09a24a2c34cae2.
At the user's request, this session installs it through H1Debug with run=False
and does not execute the new image, test either game system, or claim new
startup, native-check, rendering or performance verification. Physical-H1
validation by the user remains pending. The installation and release records
are docs/evidence/arcade-startup-installation-20261022.json and
docs/evidence/v0.2.2-release-verification.json. Screenshots and physical
check/performance results below belong to their stated historical BDA hashes
and do not verify this new hash. Both BIOS archives introduced in v0.2.1
remain included in the v0.2.2 installer.

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
Game data is supplied locally by the user and is not redistributed. Historical
packages through v0.2.0 did not redistribute BIOS data; v0.2.1 adds the two
publisher-supplied BIOS archives described below.
The included test cartridge and its boot code are original test material.

PGM support (enabled by the v0.2.0 release preset; `--pgm` in the generic
builder) additionally derives ROM layouts,
KOV program decryption, ASIC28 protection HLE, graphics formats, and ICS2115
sound behavior from the same pinned dmitrysmagin/fba-a320 revision listed
above: src/burn/pgm/{d_pgm,pgm_run,pgm_draw,pgm_prot,pgm_crypt}.cpp and
src/burn/ics2115.cpp. The original FBA, PGM, ASIC28 and ICS2115 source
notices and terms remain applicable; their adaptations are not relabelled
under the H1 adapters' GPL. PGM work is
credited upstream to ElSemi, David Haywood, OopsWare and FBA contributors;
the ICS2115 source credits O. Galibert, ElSemi and OopsWare. The build extracts
decryption and sound into temporary build copies, retaining the upstream
sound attribution. The sound copy uses paged samples and 8 kHz synthesis
with adjusted oscillator increments. New H1 filesystem, bus, cache and
frontend adapters are GPL-2.0-or-later. No PGM game data is included. The BIOS
archive added to the v0.2.1 installer is separate from this source-derived code.

The H1 PGM optimization adds bounded caches of decoded palette indices,
preconverted RGB32 palette colors, MXU1 screen copies, plain-memory MIPS
bus leaves and a signature-guarded RAM polling loop batch. The original
RGB565 renderer remains available for frozen-frame physical comparison;
generated graphics tests contain no game data. Playback omits page/region
CRC computation at the user's request; conversion checks remain separate.

The PGM sprite row composers are new MIPS32 and MXU Release-1 assembly.
They retain palette-index transparency, clipping, zoom and flip behavior;
unscaled rows avoid the per-pixel column map. The MXU sprite leaves preserve
XR1 and MXU_CR and the PGM screen-copy leaf preserves its MXU context, while
leaving interrupt enable unchanged as requested by the device owner.
Generated row fixtures and frozen gameplay frames are compared on physical
H1; no game/BIOS data is included in these tests.

The v0.2.0 PGM preset adds 448-pixel row composition followed immediately
by LCD scaling, bounded sprite/background/text plans and cache reservation,
ordinary MIPS fixed-coordinate scaling and targeted MXU1 spans/copies.
Signature-guarded 68000 and Z80 idle-loop batches preserve the emulated
registers, instruction timing and interrupt boundaries. The new PGM MXU
leaves save and restore the XR registers and MXU control state they use,
without changing interrupt enable. Upstream CPU, protection and sound
attributions remain in the complete corresponding source archive.

Physical-H1 evidence for the exact v0.2.0 BDA is recorded in
`docs/evidence/pgm-stream-hardware-20261021.json`. It covers KOV V117,
including a bounded 60 logical / 30 displayed FPS player-action window;
cold resource loads and other stages are not guaranteed full speed.
The earlier Neo Geo gameplay statements above describe their historical
builds. The exact v0.2.0 image passed native startup checks, but Neo Geo
gameplay was not rerun on that image in this release session. Game ROMs,
H1 firmware, preprocessed ROM caches and personal saves are not distributed.

The v0.2.1 release changes packaging only and retains the exact v0.2.0 BDA
(SHA256 0f3bfd95fe53f331308102a34a64be0218b39054871d73e2af1a72cbae39b2e0).
It adds the publisher's existing pgm.zip and neogeo.zip, unchanged, from
assets/bios/ to ARCADE/ in the public installer. Both archives are committed
as fixed CI packaging inputs. The publisher explicitly requested their
inclusion in this public release and future CI packages on 2026-10-21.
That instruction is recorded as the publication basis; it is not an
independently verified copyright license from the BIOS rightsholders.
The supplied archives contain no accompanying license documents. PGM BIOS
data remains attributable to IGS; Neo Geo system BIOS/resources and the
included Universe BIOS variants retain their respective SNK and Universe
BIOS author copyrights. The H1 code's GPL and linking exception do not
relicense these separate data files. See docs/licensing.md for archive
identification and the recorded scope of this packaging change. Archive/member
hashes and publication provenance are recorded in
[assets/bios/manifest.json](assets/bios/manifest.json).
