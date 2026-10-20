"""Build H1Arcade's independent bare-metal MIPS32 BDA."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse,hashlib,json,os,re,shutil,subprocess,sys
from prepare import prepare
from fast_bus import generate_copy
from dependencies import verify_dependencies
ROOT=Path(__file__).resolve().parents[1]
WORK=ROOT
def run(args):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True)
    if p.returncode:raise RuntimeError(p.stdout+p.stderr)
    return p.stdout
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--toolchain',default=os.environ.get('H1_GNU_BIN',str(ROOT/'.tools/toolchain/bin')))
    ap.add_argument('--host-cc',default=os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc'))
    ap.add_argument('--test-frames',type=int,default=0)
    ap.add_argument('--benchmark',action='store_true')
    ap.add_argument('--pgm',action='store_true',help='experimental PGM / KOV V117, paged ROMs and ICS2115 audio')
    ap.add_argument('--profile',action='store_true',help='coarse native tick partition; physical H1 profiling')
    ap.add_argument('--core',choices=('musashi','a68k'),default='musashi',help='CPU backend; the release preset selects the A68K MIPS32r1 core')
    ap.add_argument('--idle',action='store_true',help='batch verified RAM-polling TST/BEQ idle loops within one scanline')
    ap.add_argument('--dirty-present',action='store_true',help='preserve exact LCD pixels while eliding unchanged source rows')
    ap.add_argument('--cpu-selftest',action='store_true',help='run CPU batching differential checks on the physical H1 before opening game picker')
    ap.add_argument('--optimize',choices=('2','3'),default='2',help='C compiler optimization level; physical measurements decide')
    ap.add_argument('--lto',action='store_true',help='link-time optimization across C CPU, bus, graphics and sound core (keep frontend diagnostics addressable)')
    ap.add_argument('--fast-bus',action='store_true',help='inline plain RAM reads/writes in the generated MIPS A68K handlers')
    ap.add_argument('--bus-reads-only',action='store_true',help='limit inline RAM access to reads, keeping write handlers compact')
    ap.add_argument('--fast-render',action='store_true',help='specialize fully opaque, unscaled-width sprite rows')
    ap.add_argument('--tile-direct',action='store_true',help='remove tile modulo after the existing caller bounds checks')
    ap.add_argument('--mxu',action='store_true',help='use audited JZ4740 MXU1 RGB565 conversion on cached scanlines')
    ap.add_argument('--mxu-render',action='store_true',help='MXU1 lookup and packing for full-size opaque tiles')
    ap.add_argument('--palette32',action='store_true',help='experimental direct LCD-color palette and RGB32 composition with RGB565 reference')
    ap.add_argument('--audio-fast',action='store_true',help='lossy 16 kHz YM synthesis with exact 2x mono output; T switches to 22.05 kHz')
    ap.add_argument('--visible-render',action='store_true',help='cull invisible border tiles and clear only the visible RGB565 area')
    ap.add_argument('--speed-core',action='store_true',help='verified Z80 polling HLE, compact MXU register packing and VRAM word bus assembly')
    ap.add_argument('--scale-output',action='store_true',help='fuse exact 4:3 MXU conversion/scaling directly into LCD RAM')
    ap.add_argument('--cpu-mhz',type=int,choices=(336,408),default=336,help='PS1-verified 408 MHz backend; stop PCM at transitions and restore original clocks on exit')
    args=ap.parse_args()
    os.environ["HOST_CC"]=str(Path(args.host_cc).resolve()) if Path(args.host_cc).is_file() else args.host_cc
    if (args.idle or args.cpu_selftest or args.fast_bus) and args.core!='a68k':ap.error('idle/bus/selftest requires A68K')
    if args.bus_reads_only and not args.fast_bus:ap.error('bus-reads-only requires fast-bus')
    if args.mxu_render and (not args.mxu or args.fast_render):ap.error('mxu-render requires mxu and excludes the old fast-render experiment')
    if args.palette32 and not (args.mxu_render and args.cpu_selftest):ap.error('palette32 requires mxu-render and cpu-selftest')
    if args.visible_render and (not args.mxu_render or args.palette32):ap.error('visible-render requires mxu-render and excludes palette32')
    if args.speed_core and not (args.visible_render and args.cpu_selftest and args.fast_bus):ap.error('speed-core requires visible-render, cpu-selftest and fast-bus')
    if args.scale_output and not args.speed_core:ap.error('scale-output requires speed-core')
    if args.cpu_mhz!=336 and not args.scale_output:ap.error('cpu-mhz 408 requires the checked scale-output preset')
    if not 0<=args.test_frames<=36000 or (args.benchmark and not args.test_frames):ap.error('benchmark needs bounded frames')
    deps=verify_dependencies()
    name='release' if not args.test_frames else f'test{args.test_frames}'+('-bench' if args.benchmark else '')
    if args.profile:name+='-profile'
    if args.core!='musashi':name+='-'+args.core
    if args.idle:name+='-idle'
    if args.dirty_present:name+='-dirty'
    if args.cpu_selftest:name+='-selftest'
    if args.optimize!='2':name+='-o'+args.optimize
    if args.lto:name+='-lto'
    if args.fast_bus:name+='-bus'
    if args.bus_reads_only:name+='-reads'
    if args.fast_render:name+='-render'
    if args.tile_direct:name+='-tiles'
    if args.mxu:name+='-mxu-spritefix'
    if args.mxu_render:name+='-drawmxu4'
    if args.palette32:name+='-palette32'
    if args.audio_fast:name+='-audio16'
    if args.visible_render:name+='-visible'
    if args.speed_core:name+='-speed'
    if args.scale_output:name+='-scale'
    if args.cpu_mhz!=336:name+='-cpu'+str(args.cpu_mhz)
    name+='-menu'
    if args.pgm:name+='-pgm'
    build=ROOT/'build'/name;build.mkdir(parents=True,exist_ok=True)
    core,musashi=prepare(build,fast_render=args.fast_render,tile_direct=args.tile_direct,mxu_render=args.mxu_render,palette32=args.palette32,audio_fast=args.audio_fast,visible_render=args.visible_render,speed_core=args.speed_core)
    tc=Path(args.toolchain).resolve()
    sdk=ROOT/'sdk'
    includes=[ROOT/'src/include',ROOT/'src',sdk/'sdk/include',core,core/'ym2610',musashi]
    flags=['-EL','-march=mips32','-mabi=32','-msoft-float','-mno-abicalls','-G0','-fno-pic',
           '-O'+args.optimize,'-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections',
           '-fdata-sections','-fno-strict-aliasing','-fgnu89-inline','-g','-Wall','-Wno-sign-compare',
           '-Wno-unused-variable','-Wno-unused-function','-DUSE_MAMEZ80','-DHAVE_CONFIG_H=0',
           '-DM68K_EMULATE_010=0','-DM68K_EMULATE_EC020=0','-DM68K_EMULATE_020=0',
           '-DM68K_EMULATE_030=0','-DM68K_EMULATE_040=0','-DM68K_EMULATE_PMMU=0',
           f'-DH1_TEST_FRAMES={args.test_frames}',f'-DH1_BENCHMARK={int(args.benchmark)}',
           f'-DH1_PROFILE={int(args.profile)}',
           f'-DH1_PGM={int(args.pgm)}',
           f'-DH1_A68K={int(args.core=="a68k")}',
           f'-DH1_IDLE={int(args.idle)}',f'-DH1_DIRTY_PRESENT={int(args.dirty_present)}',
           f'-DH1_CPU_SELFTEST={int(args.cpu_selftest)}',
           f'-DH1_FAST_BUS={int(args.fast_bus)}',
           f'-DH1_FAST_RENDER={int(args.fast_render)}',
           f'-DH1_MXU={int(args.mxu)}',
           f'-DH1_MXU_RENDER={int(args.mxu_render)}',
           f'-DH1_PALETTE32={int(args.palette32)}',
           f'-DH1_AUDIO_FAST={int(args.audio_fast)}',
           f'-DH1_VISIBLE_RENDER={int(args.visible_render)}',
           f'-DH1_SPEED_CORE={int(args.speed_core)}',
           f'-DH1_SCALE_OUTPUT={int(args.scale_output)}',
           f'-DH1_CPU_MHZ={args.cpu_mhz}',
           f'-ffile-prefix-map={ROOT}=h1-arcade']
    # HAVE_CONFIG_H must be absent for legacy #ifdef users.
    flags.remove('-DHAVE_CONFIG_H=0')
    flags += [x for p in includes for x in ('-I',str(p))]
    sources=[ROOT/'src'/p for p in ('entry.S','startup.c','jump.S','libc.c','libc_extra.c','audio.c','cpu.c','package.c','frontend.c','arcade_config.c','arcade_menu.c','profile.c','present.c','render.c','import_game.c','import_zip.c')]
    if args.pgm:
        from pgm_sources import prepare_pgm
        sources += [ROOT/'src'/p for p in ('pgm.c','pgm_import.c','pgm_protection.c')]
        sources += prepare_pgm(ROOT,build,core)
    if args.mxu:
        mxu_source=ROOT/'src/mxu.S'
        if args.speed_core:
            mxu_source=build/'mxu-speed.S'
            mxu_source.write_text('.set H1_TILE_COMPACT,1\n.include "'+(ROOT/'src/mxu.S').as_posix()+'"\n')
        sources += [mxu_source]
        if args.speed_core:
            legacy=(ROOT/'src/mxu.S').read_text()
            for symbol in re.findall(r'^\.globl\s+(\w+)',legacy,re.M):
                legacy=re.sub(r'\b'+re.escape(symbol)+r'\b',symbol+'_original',legacy)
            legacy_source=build/'mxu-original.S';legacy_source.write_text(legacy)
            sources += [legacy_source]
    if args.palette32:sources += [ROOT/'src/palette32.c',ROOT/'src/palette32_mxu.S',ROOT/'src/present32.c']
    if args.audio_fast:sources += [ROOT/'src/audio_fast.c']
    if args.speed_core:sources += [ROOT/'src/video_bus.S']
    if args.scale_output:sources += [ROOT/'src/mxu_scale.S']
    if args.cpu_mhz!=336:sources += [ROOT/'src/clock_control.c']
    sources += [core/p for p in ('memory.c','video.c','pd4990a.c','timer.c','mamez80_interf.c','mamez80/z80.c','ym2610_interf.c','ym2610/ym2610.c')]
    if args.core=='musashi':sources += [musashi/'m68kcpu.c',musashi/'m68kops.c']
    else:
        upstream=ROOT/'third_party/fba-a320'
        commit=deps['a68k']['commit']
        if commit!='68af7cc0065757c688595adc409f5d47977793ae':ap.error('A68K upstream commit mismatch')
        host=Path(os.environ['HOST_CC'])
        env=dict(os.environ);env['PATH']=str(host.parent)+os.pathsep+env.get('PATH','')
        exe=build/'make68k.exe'
        generator=upstream/'src/cpu/a68k/mips32r1/fba_make68k.c'
        if args.fast_bus:
            patched=build/'make68k-fast.c';generate_copy(generator,patched,read_only=args.bus_reads_only,video=args.speed_core,rom=args.speed_core);generator=patched
        subprocess.run([str(host),'-O2','-static',str(generator),'-o',str(exe)],check=True,env=env)
        run([exe,build/'a68k.S',(build/'a68ktbl.inc').as_posix()])
        sources += [ROOT/'src/a68k.c',build/'a68k.S']
    headers=hashlib.sha256(b''.join(p.read_bytes() for directory in includes[:6] for p in sorted(directory.rglob('*.h')))).digest()
    def compile(item):
        i,source=item;obj=build/f'{i:02d}-{source.stem}.o'
        lto=['-flto'] if args.lto and (source.is_relative_to(core) or source.is_relative_to(musashi) or source==ROOT/'src/cpu.c') else []
        cmd=[tc/'mipsel-none-elf-gcc.exe',*flags,*lto,'-std=gnu11','-c',source,'-o',obj]
        digest=hashlib.sha256(str(cmd).encode()+source.read_bytes()+headers).hexdigest();stamp=obj.with_suffix('.sha256')
        if not obj.exists() or not stamp.exists() or stamp.read_text()!=digest:run(cmd);stamp.write_text(digest)
        return obj
    with ThreadPoolExecutor(max_workers=6) as pool:
        results=list(pool.map(compile,enumerate(sources)))
    elf,raw,bda=build/'H1Arcade.elf',build/'H1Arcade.bin',build/'H1Arcade.bda'
    run([tc/'mipsel-none-elf-gcc.exe',*flags,*(['-flto'] if args.lto else []),'-nostdlib','-Wl,--gc-sections','-Wl,--build-id=none',
         f'-Wl,-T,{ROOT/"src/h1.ld"}',f'-Wl,-Map,{build/"H1Arcade.map"}','-o',elf,*results,'-lgcc'])
    run([tc/'mipsel-none-elf-objcopy.exe','-O','binary',elf,raw])
    sys.path.insert(0,str(sdk))
    from h1_bda.header import HeaderFields,encode_header
    from h1_bda.resources import PAYLOAD_OFFSET,RESOURCE_OFFSET,RESOURCE_SIZES,build_icon_resources
    from h1_bda.validate import validate_bda
    icon=ROOT/'assets/icon.png'
    if not icon.exists():
        raise FileNotFoundError('Missing GPT-generated application icon: '+str(icon))
    payload=raw.read_bytes();pad=(-(PAYLOAD_OFFSET+len(payload)))&3
    mxu_audit=None
    if args.mxu:
        from mxu_isa import audit
        functions=[]
        for line in run([tc/'mipsel-none-elf-nm.exe','-S','--defined-only',elf]).splitlines():
            f=line.split()
            if len(f)==4 and f[2].lower()=='t':functions.append((f[3],int(f[0],16),int(f[1],16)))
        mxu_audit=audit(payload,functions,0x83c00020)
    header=HeaderFields(category=0x47,file_size_minus_4=PAYLOAD_OFFSET+len(payload)+pad-4,
                        payload_offset=PAYLOAD_OFFSET,resource_offset=RESOURCE_OFFSET,resource_sizes=RESOURCE_SIZES)
    data=encode_header(header,title='街机游戏',build_time='2026-10-18 00:00:00')+build_icon_resources(icon)+payload+bytes(pad)
    bda.write_bytes(data);assert validate_bda(bda)['ok']
    symbols={f[2]:int(f[0],16) for line in run([tc/'mipsel-none-elf-nm.exe',elf]).splitlines() if len(f:=line.split())==3}
    meta={'version':(ROOT/'VERSION').read_text().strip(),'sha256':hashlib.sha256(data).hexdigest(),'size':len(data),'options':vars(args),
          'title':'街机游戏','icon_sha256':hashlib.sha256(icon.read_bytes()).hexdigest(),
          'mxu_isa_audit':mxu_audit,
          'symbols':symbols,'dependencies':deps,'hardware_verified':False,'stats_words':20,
          'sdk_commit':deps['h1_sdk']['commit'],
          'source_sha256':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
             for directory in (ROOT/'src',ROOT/'tools') for p in sorted(directory.rglob('*')) if p.is_file() and p.suffix in ('.c','.h','.S','.ld','.py')}}
    bda.with_suffix('.build.json').write_text(json.dumps(meta,indent=2)+'\n')
    print(json.dumps({'bda':str(bda),'size':len(data),'sha256':meta['sha256'],'sections':run([tc/'mipsel-none-elf-size.exe',elf])},indent=2))
if __name__=='__main__':main()
