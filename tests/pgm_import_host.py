"""Verify PGM conversion with generated data, without running a guest CPU."""
from pathlib import Path
import hashlib,json,os,re,shutil,struct,subprocess,sys,zipfile,zlib
sys.path.insert(0,str(Path(__file__).parent))
from import_host import forge_crc,write_zip
ROOT=Path(__file__).resolve().parents[1]
out=ROOT/'build/pgm-import-host';out.mkdir(exist_ok=True)
cc=os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc')
env=dict(os.environ);env['PATH']=str(Path(cc).parent)+os.pathsep+env['PATH']
exe=out/'convert.exe'
subprocess.run([cc,'-O2','-Isrc','tests/pgm_import_cli.c','src/pgm_import.c','src/import_zip.c','-o',str(exe)],cwd=ROOT,env=env,check=True)
region_names=['BIOS','PROGRAM','TILES','COLOR','MASK','SOUND']
sizes=[0x20000,0x400000,0xc00000,0x1c00000,0xc00000,0x800000]
regions=[bytearray(n) for n in sizes];members=[{},{}]
for name,n,crc,region,dest,bios in re.findall(r'\{"([^"]+)",(0x[0-9a-f]+),(0x[0-9a-f]+),PGM_(\w+),(0x[0-9a-f]+|0),(0|1)\}',(ROOT/'src/pgm_import.c').read_text()):
    n,crc,dest,bios=int(n,0),int(crc,0),int(dest,0),int(bios)
    pattern=hashlib.shake_256(name.encode()).digest(1024)
    data=bytearray((pattern*((n+1023)//1024))[:n]);forge_crc(data,crc)
    members[bios][name]=data;regions[region_names.index(region)][dest:dest+n]=data
game,bios=out/'kov.zip',out/'pgm.zip';write_zip(game,members[0]);write_zip(bios,members[1])
cache=out/'kov.h1pgm';cache.unlink(missing_ok=True)
def run(g,b,c,expected=0,cancel=None):
    r=subprocess.run([str(exe),str(g),str(b),str(c)]+([str(cancel)] if cancel else []),env=env,capture_output=True,text=True)
    assert r.returncode==expected,(r.returncode,r.stdout,r.stderr)
    assert not Path(str(c)+'.tmp').exists()
run(game,bios,cache)
data=cache.read_bytes();h=data[:256]
assert h[:8]==b'H1PGM1\0\0' and struct.unpack_from('<I',h,12)[0]==len(data)
assert zlib.crc32(h[:252])==struct.unpack_from('<I',h,252)[0]
for i,expected in enumerate(regions):
    offset,size,crc,pages=struct.unpack_from('<4I',h,64+i*16)
    assert size==len(expected) and data[offset:offset+size]==expected
    assert crc==zlib.crc32(expected)
    checks=struct.unpack_from('<'+'I'*(size//4096),data,pages)
    assert checks==tuple(zlib.crc32(expected[j:j+4096]) for j in range(0,size,4096))
before=hashlib.sha256(data).hexdigest();run(game,bios,cache,1);assert hashlib.sha256(cache.read_bytes()).hexdigest()==before
cancel=out/'cancel.h1pgm';cancel.unlink(missing_ok=True);run(game,bios,cancel,3,1048576);assert not cancel.exists()
missing=out/'missing.zip';write_zip(missing,{k:v for k,v in members[0].items() if k!='b0600.rom'})
bad=out/'bad.h1pgm';bad.unlink(missing_ok=True);run(missing,bios,bad,1);assert not bad.exists()
renamed=out/'renamed.zip';write_zip(renamed,{f'{i}.rom':v for i,v in enumerate(members[0].values())})
renamed_cache=out/'renamed.h1pgm';renamed_cache.unlink(missing_ok=True);run(renamed,bios,renamed_cache);assert renamed_cache.read_bytes()==data
corrupt=out/'corrupt.zip';write_zip(corrupt,members[0],method=zipfile.ZIP_STORED)
with zipfile.ZipFile(corrupt) as z:info=z.getinfo('a0600.rom')
raw=bytearray(corrupt.read_bytes());n,e=struct.unpack_from('<2H',raw,info.header_offset+26)
raw[info.header_offset+30+n+e+1023]^=128;corrupt.write_bytes(raw)
corrupt_cache=out/'corrupt.h1pgm';corrupt_cache.unlink(missing_ok=True);run(corrupt,bios,corrupt_cache,1);assert not corrupt_cache.exists()
report={'ok':True,'size':len(data),'sha256':before,'checked':'six region bytes, section CRCs, every page CRC, cancellation, preserve output, missing ROM, CRC filename fallback, one-bit corrupt graphics member rejected and temporary removed','guest_cpu_executed':False}
(out/'report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
