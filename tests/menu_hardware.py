import os
"""Exercise native touch pause/configuration on physical H1 via H1Debug."""
from pathlib import Path
import argparse,json,struct,sys,time
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,os.environ.get('H1DEBUG_TOOLS',str(ROOT.parent/'BBKH1-debug/tools')))
from h1_debug import Client
from hardware import FIELDS

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build',required=True)
 ap.add_argument('--phase',choices=('suite','prepare-exit','collect','reload','defaults','keyboard','ready'),required=True);a=ap.parse_args()
 b=ROOT/'build'/a.build;m=json.loads((b/'H1Arcade.build.json').read_text());f=ROOT/'build/menu-hardware'/a.build;f.mkdir(parents=True,exist_ok=True)
 result={'hardware':True,'bda_sha256':m['sha256'],'phase':a.phase,'steps':[]}
 def save(): (f/(a.phase+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 with Client('COM4',timeout=15) as c:
  assert c.identity=='H1DEBUG/1 V1.41 RESIDENT';result['identity']=c.identity;result['status_start']=c.status()
  result['resident_image_id']=f"{struct.unpack('<20I',c.read_memory(result['status_start']['resident_base'],80))[19]:08x}"
  def words(name,n):return struct.unpack('<'+'I'*n,c.read_memory(m['symbols'][name],n*4))
  def stats():return dict(zip(FIELDS,words('h1_arcade_stats',20)))
  def menu():return dict(zip(('magic','version','active','page','cursor','capture','opened','closed','action','touches','edits','save_ok'),words('h1_menu_stats',12)))
  def settings():
   v=c.read_memory(m['symbols']['h1_arcade_settings'],64)
   return dict(zip(('mode','skip','volume','muted'),struct.unpack_from('<4I',v)))|{'mapping':list(v[16:61])}
  def wait(predicate,timeout=15):
   start=time.monotonic()
   while not predicate():
    if time.monotonic()-start>timeout:raise TimeoutError({'stats':stats(),'menu':menu(),'settings':settings()})
    time.sleep(.1)
  def checkpoint(name,screen=False):
   d={'name':name,'stats':stats(),'menu':menu(),'settings':settings(),
      'audio':{k:words(k,1)[0] for k in ('queued','playing','batches','nonzero')}}
   if screen:d['screen']=c.screenshot(f/(a.phase+'-'+name+'.png'))
   result['steps'].append(d);save();return d
  def tap(x,y,page=None):
   c.tap(x,y);time.sleep(.12)
   if page is not None:wait(lambda:menu()['active']==1 and menu()['page']==page)
  def press(key):
   c.press([key],ticks=80)
   try:time.sleep(.2)
   finally:c.release()
   time.sleep(.15)
  if a.phase in ('suite','reload'):
   wait(lambda:stats()['state']==3,180)
   if a.phase=='reload':
    s=settings();assert s['mode']==1 and s['volume']==5 and s['mapping'][2]==5 and s['mapping'][16]==0
    checkpoint('preferences-restored')
   tap(240,130,0);first=checkpoint('root',True);time.sleep(.4);second=checkpoint('frozen')
   assert first['stats']['frames']==second['stats']['frames'] and first['stats']['audio_samples']==second['stats']['audio_samples']
   assert first['audio']['batches']==second['audio']['batches'] and second['audio']['playing']==0 and second['audio']['queued']==0
   if a.phase=='suite':
    tap(350,78,1);tap(100,132);wait(lambda:menu()['capture']==4)
    press('W');wait(lambda:menu()['capture']==0xffffffff)
    assert settings()['mapping'][2]==5 and settings()['mapping'][16]==0;checkpoint('mapping-W',True)
    tap(350,241,0);tap(100,134,2)
    tap(240,73);assert settings()['mode']==1;checkpoint('display-native',True)
    tap(240,73);assert settings()['mode']==2;checkpoint('display-stretch',True)
    tap(240,153);assert settings()['mode']==0 and settings()['skip']==1
    tap(240,193,0);tap(350,134,3)
    tap(240,113);assert settings()['volume']==5;checkpoint('volume-5',True)
    tap(240,153);assert settings()['volume']==10
    c.touch(38,231,lease_ticks=80);time.sleep(.2);wait(lambda:settings()['volume']==0)
    c.touch(79,231,lease_ticks=80);time.sleep(.2);wait(lambda:settings()['volume']==10)
    c.release_touch();time.sleep(.2);checkpoint('volume-drag',True)
    tap(240,73);assert settings()['muted']==1;tap(240,73);assert settings()['muted']==0
    tap(240,193,0);tap(350,190,4);checkpoint('about',True);tap(390,254,0)
    before=stats()['frames'];tap(100,78);wait(lambda:menu()['active']==0 and stats()['frames']>before+10)
    checkpoint('resumed',True)
    c.press(['W'],ticks=80)
    try:time.sleep(.25);assert stats()['keys']==16;checkpoint('W-game-A')
    finally:c.release()
    c.press(['Z'],ticks=80)
    try:time.sleep(.25);assert stats()['keys']==0;checkpoint('Z-unmapped')
    finally:c.release()
    tap(240,130,0);checkpoint('paused-again',True)
  elif a.phase=='prepare-exit':
   assert menu()['active'] and menu()['page']==0
   tap(100,134,2);tap(240,73);assert settings()['mode']==1
   tap(240,193,0);tap(350,134,3);tap(240,113);assert settings()['volume']==5
   tap(240,193,0);checkpoint('exit-request-before',True)
   result['exit_touch_sent_once']=True;save()
   try:c.tap(100,190)
   except Exception as ex:result['exit_transport_exception']=str(ex);save()
  elif a.phase=='collect':
   wait(lambda:stats()['state']==5 and c.desktop_status()['menu_idle']==1,30)
   result['clock']=list(words('h1_clock_stats',52));assert result['clock'][2:4]==[2,0]
   result['usb_diag']=c.usb_diagnostics();result['desktop']=c.desktop_status()
   result['files']={}
   for n in ('kof97.h1neo.cfg0','kof97.h1neo.cfg1','h1arcade.log'):
    result['files'][n]=c.file_get('A:\\ARCADE\\'+n,f/n)
   checkpoint('normal-exit',True)
  elif a.phase=='defaults':
   assert menu()['active'] and menu()['page']==0
   tap(350,78,1);tap(100,241);assert settings()['mapping'][16]==5 and settings()['mapping'][2]==0
   tap(350,241,0);tap(100,134,2);tap(240,153);assert settings()['mode']==0 and settings()['skip']==1
   tap(240,193,0);tap(350,134,3);tap(240,153);assert settings()['volume']==10
   tap(240,193,0);before=stats()['frames'];tap(100,78);wait(lambda:stats()['frames']>before+10 and menu()['active']==0)
   checkpoint('defaults-resumed',True)
  elif a.phase=='keyboard':
   before=stats()['frames'];press('P');wait(lambda:menu()['active']==1 and menu()['page']==0)
   press('DOWN');assert menu()['cursor']==2;press('ENTER');wait(lambda:menu()['page']==2)
   press('ESCAPE');wait(lambda:menu()['page']==0)
   tap(350,78,1);tap(100,132);wait(lambda:menu()['capture']==4)
   press('F');assert menu()['capture']==4 and settings()['mapping'][16]==5
   checkpoint('reserved-key',True);press('P');wait(lambda:menu()['capture']==0xffffffff)
   press('ESCAPE');wait(lambda:menu()['page']==0);press('P')
   wait(lambda:menu()['active']==0 and stats()['frames']>before+10);checkpoint('keyboard-resumed',True)
  elif a.phase=='ready':
   tap(240,130,0);checkpoint('final-menu',True)
   result['files']={n:c.file_get('A:\\ARCADE\\'+n,f/('final-'+n)) for n in ('kof97.h1neo.cfg0','kof97.h1neo.cfg1')}
  result['settings_end']=settings();result['stats_end']=stats();result['menu_end']=menu();result['status_end']=c.status()
  assert not result['status_end']['usb_fault'] and not result['stats_end']['error']
  result['ok']=True;save();print(json.dumps({'ok':True,'phase':a.phase,'state':result['stats_end']['state'],'menu':result['menu_end'],'settings':result['settings_end']},indent=2))
if __name__=='__main__':main()
