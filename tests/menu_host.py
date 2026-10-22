"""Execute actual C preference validation and menu drawing; no CPU simulation."""
from pathlib import Path
import ctypes,hashlib,json,os,struct,subprocess,zlib
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
class Settings(ctypes.Structure):
 _fields_=[('mode',ctypes.c_uint),('skip',ctypes.c_uint),('volume',ctypes.c_uint),('muted',ctypes.c_uint),('mapping',ctypes.c_ubyte*45),('show_fps',ctypes.c_uint)]
def main():
 out=ROOT/'build/menu-host';out.mkdir(parents=True,exist_ok=True)
 wrapper=out/'draw.c'
 wrapper.write_text('#include "'+(ROOT/'src/arcade_menu.c').as_posix()+'"\n'+'''
volatile h1_arcade_stats_t h1_arcade_stats;
void h1_arcade_log(const char *fmt,...){(void)fmt;}
void gba_audio_set_volume(unsigned v){(void)v;}
static uint32_t *host_pixels;
static uint32_t *host_target(void){return host_pixels;}
void menu_preview(uint32_t *p,arcade_settings *s,unsigned page,int capture){host_pixels=p;draw(page,0,capture,"",s,host_target);}
int menu_items(unsigned page){return count(page);}
int menu_hit(unsigned page,int x,int y){return hit(page,x,y);}
int menu_display_action(arcade_settings *s,unsigned item){return display_action(s,item);}
''',encoding='utf-8')
 cc=Path(os.environ.get('HOST_CC','gcc'));env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
 dllpath=out/'menu.dll'
 subprocess.run([str(cc),'-O3','-shared','-static-libgcc','-Wno-int-to-pointer-cast','-I'+str(ROOT/'src'),
  '-I'+str(ROOT/'sdk/sdk/include'),str(wrapper),str(ROOT/'src/arcade_config.c'),'-o',str(dllpath)],check=True,env=env)
 d=ctypes.CDLL(str(dllpath));u=ctypes.c_uint
 d.arcade_config_load.argtypes=[ctypes.POINTER(Settings),ctypes.c_char_p,u]
 d.arcade_config_save.argtypes=[ctypes.POINTER(Settings)]
 d.arcade_mapping_defaults.argtypes=[ctypes.POINTER(Settings)]
 d.menu_items.argtypes=[u];d.menu_hit.argtypes=[u,ctypes.c_int,ctypes.c_int]
 d.menu_display_action.argtypes=[ctypes.POINTER(Settings),u]
 d.arcade_map_button.argtypes=[ctypes.POINTER(Settings),u,u]
 d.menu_preview.argtypes=[ctypes.POINTER(u),ctypes.POINTER(Settings),u,ctypes.c_int]
 rom=str(out/'fixture.h1neo').encode();paths=[Path(rom.decode()+'.cfg'+str(i)) for i in (0,1)]
 for p in paths:
  if p.exists():p.unlink()
 s=Settings();d.arcade_config_load(ctypes.byref(s),rom,0)
 assert (s.mode,s.skip,s.volume,s.muted,s.show_fps)==(0,1,10,0,0)
 assert s.mapping[16]==5 and s.mapping[19]==8 and s.mapping[23]==9 and s.mapping[25]==10
 original=bytes(s);assert not d.arcade_map_button(ctypes.byref(s),4,31) and bytes(s)==original
 assert d.arcade_map_button(ctypes.byref(s),4,2) and s.mapping[2]==5 and not s.mapping[16]
 assert d.arcade_map_button(ctypes.byref(s),5,2) and s.mapping[2]==6 and not s.mapping[17]
 assert d.arcade_map_button(ctypes.byref(s),4,39) and not s.mapping[25] and s.mapping[39]==5
 d.arcade_mapping_defaults(ctypes.byref(s));s.mode=2;s.volume=15;s.muted=1;s.show_fps=1
 assert d.arcade_config_save(ctypes.byref(s));older=paths[1].read_bytes();assert len(older)==80 and struct.unpack_from('<I',older,4)[0]==2 and older[73:76]==bytes((1,0,0)) and zlib.crc32(older[:-4])&0xffffffff==struct.unpack_from('<I',older,76)[0]
 s.mode=1;s.volume=10;s.muted=0;s.show_fps=0;assert d.arcade_config_save(ctypes.byref(s));newer=paths[0].read_bytes()
 t=Settings();d.arcade_config_load(ctypes.byref(t),rom,0);assert (t.mode,t.volume,t.muted,t.show_fps)==(1,10,0,0)
 broken=bytearray(newer);broken[24]^=1;paths[0].write_bytes(broken)
 d.arcade_config_load(ctypes.byref(t),rom,0);assert (t.mode,t.volume,t.muted,t.show_fps)==(2,15,1,1)
 # Reject invalid FPS before narrowing it into a disk byte (256 must not become 0).
 before=[p.read_bytes() for p in paths]
 for invalid_fps in (2,256):
  s.show_fps=invalid_fps
  assert not d.arcade_config_save(ctypes.byref(s)) and [p.read_bytes() for p in paths]==before
 s.show_fps=0
 # Reject checksummed invalid ranges, duplicate native aliases and truncation.
 for field,value in ((4,0),(4,3),(12,3),(16,3),(20,101),(24,2)):
  invalid=bytearray(newer);struct.pack_into('<I',invalid,field,value);struct.pack_into('<I',invalid,76,zlib.crc32(invalid[:76])&0xffffffff)
  paths[0].write_bytes(invalid);d.arcade_config_load(ctypes.byref(t),rom,0);assert t.mode==2 and t.volume==15
 # V2 accepts exactly one Boolean byte; all other padding remains strict.
 for version,offset,value in ((2,73,2),(2,74,1),(2,75,1),(1,73,1)):
  invalid=bytearray(newer);struct.pack_into('<I',invalid,4,version);invalid[offset]=value
  struct.pack_into('<I',invalid,76,zlib.crc32(invalid[:76])&0xffffffff);paths[0].write_bytes(invalid)
  d.arcade_config_load(ctypes.byref(t),rom,0);assert (t.mode,t.volume,t.show_fps)==(2,15,1)
 invalid=bytearray(newer);invalid[28+26]=5 # ALT-left aliases default LEFT.
 struct.pack_into('<I',invalid,76,zlib.crc32(invalid[:76])&0xffffffff);paths[0].write_bytes(invalid)
 d.arcade_config_load(ctypes.byref(t),rom,0);assert t.mode==2 and t.volume==15
 # A legacy V1 slot loads unchanged settings with FPS off, then saves as V2.
 legacy=bytearray(older);struct.pack_into('<II',legacy,4,1,4);legacy[73:76]=bytes(3)
 struct.pack_into('<I',legacy,76,zlib.crc32(legacy[:76])&0xffffffff);paths[1].write_bytes(legacy)
 d.arcade_config_load(ctypes.byref(t),rom,0)
 assert (t.mode,t.skip,t.volume,t.muted,t.show_fps)==(2,1,15,1,0) and bytes(t.mapping)==bytes(s.mapping)
 t.show_fps=1;assert d.arcade_config_save(ctypes.byref(t))
 migrated=paths[1].read_bytes()
 assert len(migrated)==80 and struct.unpack_from('<II',migrated,4)==(2,5) and migrated[12:73]==legacy[12:73] and migrated[73:76]==bytes((1,0,0))
 d.arcade_config_load(ctypes.byref(t),rom,0);assert (t.mode,t.volume,t.muted,t.show_fps)==(2,15,1,1)
 # Settings remain per cartridge, including the FPS switch.
 other_rom=str(out/'another.h1pgm').encode();other_paths=[Path(other_rom.decode()+'.cfg'+str(i)) for i in (0,1)]
 for p in other_paths:
  if p.exists():p.unlink()
 d.arcade_config_load(ctypes.byref(t),other_rom,1);assert (t.mode,t.show_fps)==(1,0)
 assert d.arcade_config_save(ctypes.byref(t))
 d.arcade_config_load(ctypes.byref(t),rom,0);assert (t.mode,t.show_fps)==(2,1)
 for p in other_paths:
  if p.exists():p.unlink()
 paths[1].write_bytes(older[:40]);before=[p.read_bytes() for p in paths]
 d.arcade_config_load(ctypes.byref(t),rom,0);assert (t.mode,t.skip,t.volume)==(0,1,10) and not u.in_dll(d,'h1_config_writable').value
 assert not d.arcade_config_save(ctypes.byref(t)) and [p.read_bytes() for p in paths]==before
 # Actual shared display actions and hit targets: both touch and keyboard use these.
 assert d.menu_items(2)==5
 for item in range(5):assert d.menu_hit(2,240,73+item*40)==item
 assert d.menu_hit(2,240,251)==-1
 t.mode=2;t.skip=2;t.volume=15;t.muted=1
 mapping=bytes(t.mapping)
 assert not d.menu_display_action(ctypes.byref(t),2) and t.show_fps==1
 assert not d.menu_display_action(ctypes.byref(t),2) and t.show_fps==0
 assert not d.menu_display_action(ctypes.byref(t),0) and t.mode==0
 assert not d.menu_display_action(ctypes.byref(t),1) and t.skip==0
 t.mode=2;t.skip=2;t.show_fps=1
 assert not d.menu_display_action(ctypes.byref(t),3)
 assert (t.mode,t.skip,t.volume,t.muted,t.show_fps)==(0,1,15,1,0) and bytes(t.mapping)==mapping
 before=bytes(t);assert d.menu_display_action(ctypes.byref(t),4) and bytes(t)==before
 # Render the actual C widgets, with full-screen bounds canaries.
 canv=(u*(480*272+8))();ptr=ctypes.cast(ctypes.byref(canv,16),ctypes.POINTER(u));screens=[]
 for page in range(5):
  for i in range(len(canv)):canv[i]=0x1234abcd
  d.menu_preview(ptr,ctypes.byref(t),page,-1)
  assert all(canv[i]==0x1234abcd for i in list(range(4))+list(range(480*272+4,480*272+8)))
  image=Image.new('RGB',(480,272));image.putdata([((p>>16)&255,(p>>8)&255,p&255) for p in canv[4:-4]])
  dest=out/('page'+str(page)+'.png');image.save(dest);screens.append(str(dest))
 # FPS on/off changes only its own visible row and both variants stay in bounds.
 d.menu_preview(ptr,ctypes.byref(t),2,-1);fps_off=list(canv[4:-4]);t.show_fps=1
 d.menu_preview(ptr,ctypes.byref(t),2,-1);fps_on=list(canv[4:-4]);t.show_fps=0
 changed=[i for i,(a,b) in enumerate(zip(fps_off,fps_on)) if a!=b]
 assert changed and all(136<=i//480<170 and 38<=i%480<442 for i in changed)
 assert all(canv[i]==0x1234abcd for i in list(range(4))+list(range(480*272+4,480*272+8)))
 image=Image.new('RGB',(480,272));image.putdata([((p>>16)&255,(p>>8)&255,p&255) for p in fps_on]);dest=out/'page2-fps-on.png';image.save(dest);screens.append(str(dest))
 d.arcade_import_draw.argtypes=[ctypes.POINTER(u),ctypes.c_char_p,u,u]
 for stage in ('inspect','padding','program','sound','fixed','sprites','verify','done'):
  for i in range(len(canv)):canv[i]=0x1234abcd
  d.arcade_import_draw(ptr,stage.encode(),37,100)
  assert all(canv[i]==0x1234abcd for i in list(range(4))+list(range(480*272+4,480*272+8)))
  if stage=='sprites':
   image=Image.new('RGB',(480,272));image.putdata([((p>>16)&255,(p>>8)&255,p&255) for p in canv[4:-4]])
   dest=out/'import-progress.png';image.save(dest);screens.append(str(dest))
 report={'ok':True,'environment':'actual host C preferences and drawing only; no CPU/device emulation',
  'checks':['defaults','reserved key rejection','reassignment clears old key','native aliases cannot conflict',
   'V2 FPS two-slot roundtrip and independent CRC','bad newest slot falls back','checksummed invalid settings rejected',
   'V1 settings migrate with FPS off','FPS and unused padding are strictly validated','FPS is per cartridge','invalid FPS rejects before byte narrowing',
   'five display touch targets and shared display actions','display defaults preserve audio and mapping','FPS on/off drawing changes only its row',
   'duplicate native code rejected','two corrupt slots preserve files','five menu pages draw within bounds','eight import stages draw within bounds'],
  'source_sha256':{p:hashlib.sha256((ROOT/'src'/p).read_bytes()).hexdigest() for p in ('arcade_config.c','arcade_config.h','arcade_menu.c','arcade_menu.h','menu_font.h')},'screens':screens}
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
 for p in paths:p.unlink()
if __name__=='__main__':main()
