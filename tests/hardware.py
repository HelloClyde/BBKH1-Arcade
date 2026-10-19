import os
"""Observe and control an already deployed H1Arcade through the real H1Debug."""
from pathlib import Path
import argparse,hashlib,json,struct,sys,time
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,os.environ.get('H1DEBUG_TOOLS',str(ROOT.parent/'BBKH1-debug/tools')))
from h1_debug import Client
FIELDS=('magic','version','state','error','frames','displayed','cache_hits','cache_misses',
        'tick_hz','raw_start','raw_end','rtc_start','rtc_end','stop','pc','audio_samples',
        'cache_bytes','checksum','keys','paused_frames')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--name',default='snapshot');ap.add_argument('--keys',nargs='+')
    ap.add_argument('--hold',type=float,default=.5);ap.add_argument('--measure',type=float,default=0)
    ap.add_argument('--no-screen',action='store_true');args=ap.parse_args()
    if not args.name.replace('-','').replace('_','').isalnum():ap.error('invalid output name')
    meta=json.loads((ROOT/'build/release/H1Arcade.build.json').read_text())
    out=ROOT/'build/kof97-hardware';out.mkdir(parents=True,exist_ok=True)
    with Client('COM4',timeout=10) as c:
        resident=c.status()
        header=c.read_memory(resident['resident_base'],80)
        words=struct.unpack('<20I',header)
        assert words[:2]==(0x31444248,1) and words[3:5]==(resident['resident_base'],resident['resident_size'])
        def stats():
            data=c.read_memory(meta['symbols']['h1_arcade_stats'],80)
            result=dict(zip(FIELDS,struct.unpack('<20I',data)))
            assert result['magic']==0x31435241 and result['version']==1,'target binary not loaded'
            return result
        result={'environment':'physical H1/Y100 V1.41 via USB H1Debug','hardware':True,
                'bda_sha256':meta['sha256'],'identity':c.identity,
                'resident_image_id':f'{words[19]:08x}','before':stats()}
        if args.keys:
            result['input']=c.press(args.keys,ticks=min(4000,max(80,int(args.hold*80))))
            try:
                time.sleep(args.hold)
                result['held']=stats()
            finally:c.release()
            time.sleep(.3)
        if args.measure:
            result['measure_start']=stats();started=time.monotonic();time.sleep(args.measure)
            result['measure_end']=stats();elapsed=time.monotonic()-started
            result['measurement']={'pc_seconds':elapsed,
                'frames':result['measure_end']['frames']-result['measure_start']['frames'],
                'displayed':result['measure_end']['displayed']-result['measure_start']['displayed']}
            result['measurement']['frames_per_second']=result['measurement']['frames']/elapsed
        if not args.no_screen:result['screen']=c.screenshot(out/(args.name+'.png'))
        result.update(after=stats(),device_status=c.status(),desktop=c.desktop_status())
        (out/(args.name+'.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(json.dumps(result,indent=2))

if __name__=='__main__':main()
