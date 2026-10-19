import os
"""Deploy, measure and control H1Arcade on the physical H1; no CPU simulation."""
from pathlib import Path
import argparse,contextlib,hashlib,json,struct,sys,time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,os.environ.get('H1DEBUG_TOOLS',str(ROOT.parent/'BBKH1-debug/tools')))
from h1_debug import Client
from hardware import FIELDS
PHASES=('other','input','68000','z80','ym_audio','draw','lcd','sprite_io','wait','timer','pcm_output')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build',default='release-profile');ap.add_argument('--name',required=True)
    ap.add_argument('--deploy',action='store_true');ap.add_argument('--run',action='store_true')
    ap.add_argument('--device-path',default='A:\\应用\\程序\\H1Arcade.bda',help='native BDA target; allows a separate temporary profiling app')
    ap.add_argument('--keys',nargs='+');ap.add_argument('--hold',type=float,default=.3)
    ap.add_argument('--sequence',nargs='+',help='separate key pulses with explicit releases')
    ap.add_argument('--skip',type=int);ap.add_argument('--display',type=int,choices=(0,1,2))
    ap.add_argument('--auto',type=int,choices=(0,1))
    ap.add_argument('--idle',type=int,choices=(0,1));ap.add_argument('--dirty',type=int,choices=(0,1))
    ap.add_argument('--bus',type=int,choices=(0,1))
    ap.add_argument('--render',type=int,choices=(0,1))
    ap.add_argument('--visible',type=int,choices=(0,1,2))
    ap.add_argument('--z80-hle',type=int,choices=(0,1));ap.add_argument('--video-bus',type=int,choices=(0,1))
    ap.add_argument('--rom-fast',type=int,choices=(0,1));ap.add_argument('--compact',type=int,choices=(0,1))
    ap.add_argument('--mxu',type=int,choices=(0,1))
    ap.add_argument('--direct',type=int,choices=(0,1))
    ap.add_argument('--cached',type=int,choices=(0,1))
    ap.add_argument('--scale',type=int,choices=(0,1))
    ap.add_argument('--menu',type=int,choices=(0,1),help='disable only for bare-pause pixel/performance diagnostics')
    ap.add_argument('--palette32',type=int,choices=(0,1))
    ap.add_argument('--audio-quality',type=int,choices=(0,1,2),help='0=22.05 kHz, 1=16 kHz, 2=8 kHz; applied at the next frame boundary')
    ap.add_argument('--timer-fast',type=int,choices=(0,1))
    ap.add_argument('--scan-hle',type=int,choices=(0,1))
    ap.add_argument('--volume',type=int,choices=range(101),default=10,metavar='0..100',help='set audio preference at the picker (default 10 for quiet hardware testing)')
    ap.add_argument('--measure',type=float,default=0);ap.add_argument('--screen',action='store_true')
    ap.add_argument('--pause-after-measure',action='store_true',help='pause the running game before screenshot transfer')
    ap.add_argument('--measure-keys',nargs='+',help='hold ordinary game buttons during the measurement, always release afterwards')
    ap.add_argument('--wait-state',type=int);ap.add_argument('--wait-frames',type=int)
    ap.add_argument('--wait-timeout',type=float,default=180,help='allow native startup differential selftests to finish')
    ap.add_argument('--save-files',action='store_true',help='download flushed log and per-game NVRAM after normal exit')
    args=ap.parse_args()
    assert 0<=args.hold<=30 and 0<=args.measure<=60
    build=ROOT/'build'/args.build;meta=json.loads((build/'H1Arcade.build.json').read_text())
    assert hashlib.sha256((build/'H1Arcade.bda').read_bytes()).hexdigest()==meta['sha256']
    out=ROOT/'build/performance-hardware'/args.build;out.mkdir(parents=True,exist_ok=True)
    def save(data):
        (out/(args.name+'.json')).write_text(json.dumps(data,indent=2),encoding='utf-8')
        print(json.dumps({k:v for k,v in data.items() if k in ('bda_sha256','after','measurement','profile','desktop')},indent=2),flush=True)
    with Client('COM4',timeout=15) as c:
        status=c.status();hdr=struct.unpack('<20I',c.read_memory(status['resident_base'],80))
        assert c.identity=='H1DEBUG/1 V1.41 RESIDENT' and hdr[0]==0x31444248 and status['usb_fault']==0
        result={'hardware':True,'environment':'physical H1 via H1Debug','bda_sha256':meta['sha256'],
                'identity':c.identity,'resident_image_id':f'{hdr[19]:08x}','device_path':args.device_path}
        if 'h1_clock_stats' in meta['symbols']:result['usb_diag_start']=c.usb_diagnostics()
        if args.deploy:
            result['deploy']=c.deploy(build/'H1Arcade.bda',args.device_path,
                                      wait_seconds=180,receipt_path=out/(args.name+'-deploy.json'))
        elif args.run:result['run']=c.desktop_run(args.device_path)
        def stats():
            s=dict(zip(FIELDS,struct.unpack('<20I',c.read_memory(meta['symbols']['h1_arcade_stats'],80))))
            assert s['magic']==0x31435241 and s['version']==1
            return s
        def profile():
            if 'h1_arcade_profile' not in meta['symbols']:return None
            header=struct.unpack('<2I',c.read_memory(meta['symbols']['h1_arcade_profile'],8))
            assert header[0]==0x31465250 and header[1] in (1,2)
            phases=PHASES if header[1]==2 else PHASES[:9]
            words=struct.unpack('<'+'I'*(6+len(phases)),c.read_memory(meta['symbols']['h1_arcade_profile'],(6+len(phases))*4))
            return dict(zip(('magic','version','running','phase','last','calls',*phases),words))
        def extras():
            result={}
            for name,fields in (('h1_idle_stats',('magic','version','calls','batched','cycles','tests','failures')),
                                ('h1_z80_hle_stats',('magic','version','tests','failures','loops','cycles','matched')),
                                ('h1_video_bus_stats',('magic','version','tests','failures','calls')),
                                ('h1_timer_fast_stats',('magic','version','tests','failures')),
                                ('h1_frameskip_stats',('magic','version','delayed')),
                                ('h1_scan_stats',('magic','version','calls','words','cycles','tests','failures')),
                                ('h1_input_stats',('magic','version','scans','queries','reused')),
                                ('h1_audio_fast_stats',('magic','version','tests','failures','switches')),
                                ('h1_palette32_stats',('magic','version','tests','failures','tiles','stores')),
                                ('h1_tile_mxu_stats',('magic','version','tiles','palette_rebuilds')),
                                ('h1_mxu_stats',('magic','version','calls','pixels','tests','failures')),
                                ('h1_scale_stats',('magic','version','calls','pixels','tests','failures')),
                                ('h1_bus_stats',('magic','version','tests','failures')),
                                ('h1_render_stats',('magic','version','opaque_rows','tests','failures')),
                                ('h1_present_stats',('magic','version','frames','written','unchanged','invalidations'))):
                if name in meta['symbols']:
                    if name=='h1_tile_mxu_stats' and struct.unpack('<2I',c.read_memory(meta['symbols'][name],8))[1]==2:
                        fields=(*fields,'cache_hits','decodes')
                    if name=='h1_present_stats' and struct.unpack('<2I',c.read_memory(meta['symbols'][name],8))[1]==2:
                        fields=(*fields,'pixels_written','pixels_unchanged')
                    result[name]=dict(zip(fields,struct.unpack('<'+'I'*len(fields),c.read_memory(meta['symbols'][name],len(fields)*4))))
            result['audio']={name:struct.unpack('<I',c.read_memory(meta['symbols'][name],4))[0]
                            for name in ('audio_ready','underruns','submitted','failures','batches','queued','nonzero') if name in meta['symbols']}
            if 'get_volume' in meta['symbols']:
                getter=struct.unpack('<I',c.read_memory(meta['symbols']['get_volume'],4))[0]
                if getter:
                    code=struct.unpack('<3I',c.read_memory(getter,12))
                    assert code[0]&0xffff0000==0x3c020000 and code[1]==0x03e00008 and code[2]&0xffff0000==0x8c420000
                    low=struct.unpack('<h',struct.pack('<H',code[2]&65535))[0]
                    address=((code[0]&65535)<<16)+low
                    assert 0x80000000<=address<0x84000000
                    result['audio']['native_volume']=struct.unpack('<I',c.read_memory(address,4))[0]
            if 'h1_clock_stats' in meta['symbols']:
                words=struct.unpack('<52I',c.read_memory(meta['symbols']['h1_clock_stats'],208))
                result['clock']={'magic':words[0],'requested_mhz':words[1],'state':words[2],'error':words[3],
                                 'words':list(words),'original':list(words[8:19]),'target':list(words[19:30]),
                                 'running':list(words[30:41]),'restored':list(words[41:52])}
            return result
        @contextlib.contextmanager
        def measure_keys():
            if args.measure_keys:
                result['measure_input']=c.press(args.measure_keys,ticks=4000)
                time.sleep(.15)
            try:yield
            finally:
                if args.measure_keys:c.release()
        if args.wait_state is not None or args.wait_frames is not None:
            started=time.monotonic()
            while True:
                current=stats()
                if (args.wait_state is None or current['state']==args.wait_state) and (args.wait_frames is None or current['frames']>=args.wait_frames):break
                if current['error'] or current['state']==5:raise RuntimeError(current)
                if time.monotonic()-started>=args.wait_timeout:raise TimeoutError(current)
                time.sleep(.5)
            result['wait_seconds']=time.monotonic()-started
        result['before']=stats()
        if args.volume is not None and result['before']['state']==1:
            for symbol,value in (('volume',args.volume),('volume_requested',1)):
                c.write_memory(meta['symbols'][symbol],struct.pack('<I',value))
            result['volume_percent']=args.volume
        for symbol,value in (('skip',args.skip),('display_mode',args.display),('h1_idle_enabled',args.idle),('h1_present_dirty',args.dirty),('h1_bus_fast',args.bus),('h1_render_fast',args.render),('h1_render_visible',args.visible),('h1_z80_hle',args.z80_hle),('h1_video_bus_enabled',args.video_bus),('h1_rom_fast_enabled',args.rom_fast),('h1_render_compact',args.compact),('h1_present_mxu',args.mxu),('h1_palette32_enabled',args.palette32),('h1_audio_quality',args.audio_quality),('h1_timer_fast',args.timer_fast)):
            if value is not None:
                assert result['before']['state'] in (1,2,3) and (symbol!='skip' or 0<=value<=59)
                address=meta['symbols'][symbol];c.write_memory(address,struct.pack('<I',value))
                assert struct.unpack('<I',c.read_memory(address,4))[0]==value
                result[symbol]=value
        auto=args.auto if args.auto is not None else 0 if args.skip is not None else None
        if args.direct is not None:
            c.write_memory(meta['symbols']['h1_present_direct'],struct.pack('<I',args.direct))
        if args.cached is not None:
            c.write_memory(meta['symbols']['h1_present_cached'],struct.pack('<I',args.cached))
        if args.scale is not None:
            c.write_memory(meta['symbols']['h1_present_scale'],struct.pack('<I',args.scale))
        if args.menu is not None:
            c.write_memory(meta['symbols']['h1_pause_menu_enabled'],struct.pack('<I',args.menu))
        if args.scan_hle is not None:
            c.write_memory(meta['symbols']['h1_scan_hle'],struct.pack('<I',args.scan_hle))
        if auto is not None and 'h1_auto_frameskip' in meta['symbols']:
            c.write_memory(meta['symbols']['h1_auto_frameskip'],struct.pack('<I',auto))
            result['h1_auto_frameskip']=auto
        if args.keys:
            result['input']=c.press(args.keys,ticks=max(80,int(args.hold*80)))
            try:time.sleep(args.hold);result['held']=stats()
            finally:c.release()
            time.sleep(.2)
        if args.sequence:
            result['sequence']=[]
            for key in args.sequence:
                event={'key':key,'input':c.press([key],ticks=max(80,int(args.hold*80)))}
                try:time.sleep(args.hold);event['held']=stats()
                finally:c.release()
                time.sleep(.2);result['sequence'].append(event)
        if args.measure:
            with measure_keys():
                result['measure_start']=stats();result['profile_start']=profile();result['extra_start']=extras()
                started=time.monotonic();time.sleep(args.measure)
                result['profile_end']=profile();result['measure_end']=stats();elapsed=time.monotonic()-started
                result['extra_end']=extras()
                a,b=result['measure_start'],result['measure_end']
                result['measurement']={'pc_seconds':elapsed,'logic_frames':b['frames']-a['frames'],
                                       'displayed_frames':b['displayed']-a['displayed'],
                                       'logic_fps':(b['frames']-a['frames'])/elapsed,
                                       'display_fps':(b['displayed']-a['displayed'])/elapsed,
                                       'cache_misses':b['cache_misses']-a['cache_misses']}
                native_ticks=b['raw_end']-a['raw_end'];hz=b['tick_hz']
                result['measurement'].update(native_raw_ticks=native_ticks,native_tick_hz=hz,
                    native_seconds=native_ticks/hz if hz else None,
                    native_logic_fps=(b['frames']-a['frames'])*hz/native_ticks if native_ticks and hz else None)
                for field in ('underruns','failures'):
                    if field in result['extra_start']['audio']:
                        result['measurement']['audio_'+field]=result['extra_end']['audio'][field]-result['extra_start']['audio'][field]
                if result['profile_start']:
                    delta={p:result['profile_end'][p]-result['profile_start'][p] for p in PHASES if p in result['profile_start']}
                    total=sum(delta.values());result['profile']={'native_raw_ticks':delta,'total_ticks':total,
                        'percent':{p:100*n/total if total else 0 for p,n in delta.items()},
                        'method':'coarse native tick boundaries, exclusive wall time, debugger active'}
        if args.pause_after_measure:
            assert args.measure and result['measurement']['logic_frames']>0
            result['pause_after_measure']=c.press(['P'],ticks=80)
            try:time.sleep(.3)
            finally:c.release()
            time.sleep(.2)
        if args.screen:result['screen']=c.screenshot(out/(args.name+'.png'))
        if args.save_files:
            started=time.monotonic()
            while stats()['state']!=5 or not c.desktop_status()['menu_idle']:
                if time.monotonic()-started>30:raise TimeoutError('normal exit/save still in progress')
                time.sleep(.2)
        result.update(after=stats(),profile_after=profile(),status=c.status(),desktop=c.desktop_status())
        result['settings']={symbol:struct.unpack('<I',c.read_memory(meta['symbols'][symbol],4))[0]
                            for symbol in ('skip','display_mode','muted','nvseq','h1_idle_enabled','h1_present_dirty','h1_bus_fast','h1_render_fast','h1_render_visible','h1_z80_hle','h1_video_bus_enabled','h1_rom_fast_enabled','h1_bus_rom_active','h1_render_compact','h1_present_mxu','h1_palette32_enabled','h1_audio_quality','h1_audio_source_rate','h1_timer_fast','volume','volume_requested','volume_owned','original_volume') if symbol in meta['symbols']}
        if 'h1_auto_frameskip' in meta['symbols']:
            result['settings']['h1_auto_frameskip']=struct.unpack('<I',c.read_memory(meta['symbols']['h1_auto_frameskip'],4))[0]
        for name in ('h1_present_direct','h1_present_direct_pixels','h1_scan_hle','h1_present_cached','h1_present_cached_pixels','h1_present_scale','h1_pause_menu_enabled'):
            if name in meta['symbols']:result['settings'][name]=struct.unpack('<I',c.read_memory(meta['symbols'][name],4))[0]
        if 'last_target' in meta['symbols']:
            result['settings']['lcd_target']=struct.unpack('<I',c.read_memory(meta['symbols']['last_target'],4))[0]
        result['extras']=extras()
        if 'h1_clock_stats' in meta['symbols']:result['usb_diag_end']=c.usb_diagnostics()
        if args.save_files:
            assert result['after']['state']==5 and result['desktop']['menu_idle']==1
            result['files']={}
            for file in ('h1arcade.log','kof97.h1neo.nv0','kof97.h1neo.nv1'):
                result['files'][file]=c.file_get('A:\\ARCADE\\'+file,out/file)
        save(result)
if __name__=='__main__':main()
