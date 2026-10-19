import os
"""Compare paused physical H1 LCD pixels with an independent full redraw."""
from pathlib import Path
import argparse,hashlib,json,struct,sys,time
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,os.environ.get('H1DEBUG_TOOLS',str(ROOT.parent/'BBKH1-debug/tools')))
from h1_debug import Client
from hardware import FIELDS

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build',required=True);args=ap.parse_args()
    build=ROOT/'build'/args.build;meta=json.loads((build/'H1Arcade.build.json').read_text())
    assert hashlib.sha256((build/'H1Arcade.bda').read_bytes()).hexdigest()==meta['sha256']
    out=ROOT/'build/performance-hardware'/args.build
    with Client('COM4',timeout=15) as c:
        assert c.identity=='H1DEBUG/1 V1.41 RESIDENT' and not c.status()['usb_fault']
        def stats():return dict(zip(FIELDS,struct.unpack('<20I',c.read_memory(meta['symbols']['h1_arcade_stats'],80))))
        a=stats();time.sleep(.3);b=stats()
        assert a['state']==b['state']==3 and a['frames']==b['frames'] and b['paused_frames']>a['paused_frames']
        rgb32=meta['options'].get('palette32',False) and struct.unpack('<I',c.read_memory(meta['symbols']['h1_palette32_enabled'],4))[0]!=0
        raw=c.read_memory(meta['symbols']['framebuffer32' if rgb32 else 'framebuffer'],352*256*(4 if rgb32 else 2))
        pixels=struct.unpack('<'+str(352*256)+('I' if rgb32 else 'H'),raw);cases=[]
        for mode in (0,1,2):
            c.write_memory(meta['symbols']['display_mode'],struct.pack('<I',(mode+2)%3))
            c.press(['G'],ticks=80)
            try:time.sleep(.3)
            finally:c.release()
            time.sleep(.2)
            screen=c.screenshot(out/('final-pixel-mode'+str(mode)+'.png'))
            actual=Image.open(out/('final-pixel-mode'+str(mode)+'.png')).convert('RGB')
            width=362 if mode==0 else 320 if mode==1 else 480;height=224 if mode==1 else 272
            expected=Image.new('RGB',(480,272));dst=expected.load()
            for y in range(height):
                for x in range(width):
                    p=pixels[(y*224//height+16)*352+x*320//width+16]
                    dst[x+(480-width)//2,y+(272-height)//2]=((p>>16)&255,(p>>8)&255,p&255) if rgb32 else ((p>>11)*8,((p>>5)&63)*4,(p&31)*8)
            actual_bytes=actual.tobytes();expected_bytes=expected.tobytes()
            different=sum(actual_bytes[i:i+3]!=expected_bytes[i:i+3] for i in range(0,len(actual_bytes),3))
            cases.append({'mode':mode,'different_pixels':different,'screen':screen})
            assert different==0,cases[-1]
        c.press(['G'],ticks=80)
        try:time.sleep(.3)
        finally:c.release()
        time.sleep(.2)
        end=stats();assert end['frames']==b['frames'] and not end['error']
        assert struct.unpack('<I',c.read_memory(meta['symbols']['display_mode'],4))[0]==0
        report={'ok':True,'environment':'physical H1, paused KOF97, full 480x272 LCD comparison',
                'source_format':'RGB32' if rgb32 else 'RGB565',
                'bda_sha256':meta['sha256'],'framebuffer_sha256':hashlib.sha256(raw).hexdigest(),
                'cases':cases,'frames_held':end['frames'],'restored_display_mode':0,'status':c.status()}
        (out/'final-pixels.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,indent=2))
if __name__=='__main__':main()
