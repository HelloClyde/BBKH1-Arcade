"""Convert supported original Neo Geo ZIP sets to H1's paged .h1neo format.

No game or BIOS is downloaded. Driver sizes and CRCs come from pinned GnGeo.
"""
from pathlib import Path
from functools import lru_cache
import argparse,ctypes,hashlib,json,os,shutil,struct,subprocess,zipfile,zlib
ROOT=Path(__file__).resolve().parents[1]
BASELINE_SETS={'kof94','kof95','kof96','kof97','mslug','mslug2','samsho','samsho2','samsho3','samsho4','lastblad'}
SPECIAL_HARDWARE={'fatfury2','irrmaze','kof98','kof98k','kof98n','kog','lans2004','popbounc'}
MAGIC=b'H1NEO1\0\0'
PAGE=4096

def crc(data):return zlib.crc32(data)&0xffffffff

def write_package(path,name,regions):
    if not name.isascii() or len(name)>15:raise ValueError('invalid game name')
    sprites=regions[9]
    if len(sprites)%PAGE:raise ValueError('sprite length must be page aligned')
    regions=dict(regions)
    regions[14]=b''.join(struct.pack('<I',crc(sprites[i:i+PAGE])) for i in range(0,len(sprites),PAGE))
    offset=64+len(regions)*16;table=bytearray();parts=[]
    for id,data in sorted(regions.items()):
        if not data:continue
        table.extend(struct.pack('<4I',id,offset,len(data),crc(data)));parts.append(data);offset+=len(data)
    if len(table)!=len(regions)*16:raise ValueError('empty region')
    header=struct.pack('<8s4I16s20s',MAGIC,1,len(regions),offset,0,name.encode(),bytes(20))
    header+=struct.pack('<I',crc(header))
    data=header+table+b''.join(parts)
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    if path.exists():raise FileExistsError(f'will not overwrite existing {path}')
    temp=path.with_name(path.name+'.tmp')
    with temp.open('xb') as f:f.write(data)
    temp.rename(path)
    return {'path':str(path),'game':name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest(),
            'regions':{str(k):len(v) for k,v in regions.items()}}

def gfx_library():
    """Build the pinned upstream decoders as a small local native helper."""
    bundled=Path(__file__).with_name('gfx.dll')
    if bundled.exists():
        lib=ctypes.CDLL(str(bundled));lib.sprites.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
        lib.chars.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p];return lib
    build=ROOT/'build/convert';build.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'third_party/gngeo/src/roms.c').read_text()
    tile=source[source.index('static int convert_roms_tile('):source.index('void convert_all_tile(')]
    char=source[source.index('void convert_all_char('):source.index('static int init_roms(')]
    code='#include <stdint.h>\n#include <string.h>\n#include <stdlib.h>\n#include <stdio.h>\ntypedef uint8_t Uint8;typedef uint32_t Uint32;\n#define TILE_INVISIBLE 1\n'+tile+char
    code+='\n__declspec(dllexport) void sprites(uint8_t *data,unsigned n,uint32_t *usage){for(unsigned i=0;i<n/128;i++)usage[i/16]|=convert_roms_tile(data,i);}\n'
    code+='__declspec(dllexport) void chars(uint8_t *data,unsigned n,uint8_t *usage){convert_all_char(data,n,usage);}\n'
    src=build/'gfx.c';dll=build/'gfx.dll';stamp=build/'gfx.sha256'
    digest=hashlib.sha256(b'-O2 -shared -static-libgcc -Wl,--no-insert-timestamp\0'+code.encode()).hexdigest()
    if not dll.exists() or not stamp.exists() or stamp.read_text()!=digest:
        src.write_text(code)
        cc=Path(os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc'))
        env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
        subprocess.run([str(cc),'-O2','-shared','-static-libgcc','-Wl,--no-insert-timestamp',str(src),'-o',str(dll)],env=env,check=True)
        stamp.write_text(digest)
    lib=ctypes.CDLL(str(dll));lib.sprites.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
    lib.chars.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
    return lib

def decode_graphics(regions):
    lib=gfx_library()
    for id,usage_id in ((5,13),(6,11)):
        data=regions[id];usage=bytearray(len(data)//32)
        lib.chars((ctypes.c_ubyte*len(data)).from_buffer(data),len(data),(ctypes.c_ubyte*len(usage)).from_buffer(usage))
        regions[usage_id]=usage
    data=regions[9];usage=bytearray(len(data)//512)
    lib.sprites((ctypes.c_ubyte*len(data)).from_buffer(data),len(data),(ctypes.c_ubyte*len(usage)).from_buffer(usage))
    regions[10]=usage

def native_words(data):
    """Recognize a 68000 reset vector in either common word byte order."""
    candidates=[]
    for swap in (False,True):
        candidate=bytearray(data)
        if swap:candidate[0::2],candidate[1::2]=candidate[1::2],candidate[0::2]
        w=struct.unpack_from('<4H',candidate)
        sp=(w[0]<<16)|w[1];pc=(w[2]<<16)|w[3]
        if 0x100000<=sp<=0x110000 and pc%2==0 and (0x80<=pc<len(data) or 0xc00000<=pc<0xc20000):candidates.append(candidate)
    if len(candidates)!=1:raise ValueError('unrecognized or ambiguous 68000 reset vectors; check original ROM/BIOS byte order')
    return candidates[0]

def driver_archive():
    datafile=Path(__file__).with_name('gngeo_data.zip')
    return datafile if datafile.exists() else ROOT/'third_party/gngeo/gngeo.dat/gngeo_data.zip'

def parse_driver(data):
    sizes=struct.unpack_from('<10I',data,196);count=struct.unpack_from('<I',data,236)[0]
    if count>32 or len(data)!=240+49*count:raise ValueError('invalid upstream driver')
    entries=[]
    for i in range(count):
        p=240+i*49;filename=data[p:p+32].split(b'\0')[0].decode('ascii')
        entries.append((filename,*struct.unpack_from('<B4I',data,p+32)))
    return sizes,entries

@lru_cache(maxsize=None)
def driver(name):
    with zipfile.ZipFile(driver_archive()) as z:return parse_driver(z.read('rom/'+name+'.drv'))

def members_required(entries):
    lengths={}
    for filename,region,source,_,size,expected in entries:
        key=(filename.lower(),expected)
        lengths[key]=max(lengths.get(key,0),(source//2 if region==9 else source)+size)
    return lengths

def compatible_layout(sizes,entries):
    allowed={1,3,4,6,8,9}
    if any(size and i not in allowed for i,size in enumerate(sizes)):return False
    if not (0<sizes[1]<=0x80000 and 0<sizes[3]<=0x1000000 and sizes[4]<=0x1000000 and
            sizes[6]==0x20000 and 0<sizes[8]<=0x800000 and 0<sizes[9]<=0x4000000 and sizes[9]%4096==0):return False
    by_region={i:{} for i in allowed}
    for filename,region,source,dest,size,expected in entries:
        if not filename or not expected or not size or region not in allowed:return False
        if dest in by_region[region]:return False
        by_region[region][dest]=size
        if region==9:
            if source%2 or (dest&~1)%4096 or size%2048 or (dest&~1)+size*2>sizes[9]:return False
        elif dest+size>sizes[region] or (region==8 and (dest|size)%2) or (region==6 and (dest|size)%32):return False
    for region,parts in by_region.items():
        if bool(parts)!=bool(sizes[region]):return False
        if region==9:
            if any(parts.get(dest^1)!=size for dest,size in parts.items()):return False
            intervals=sorted((dest,dest+size*2) for dest,size in parts.items() if not dest%2)
        else:
            intervals=sorted((dest,dest+size) for dest,size in parts.items())
            if region==8 and intervals[0][0]!=0:return False
        if any(end>start for (_,end),(start,_) in zip(intervals,intervals[1:])):return False
    return True

@lru_cache(maxsize=1)
def supported_drivers():
    candidates={}
    with zipfile.ZipFile(driver_archive()) as archive:
        for entry in archive.namelist():
            if not entry.startswith('rom/') or not entry.endswith('.drv'):continue
            name=entry[4:-4]
            data=archive.read(entry)
            internal,parent=(data[start:start+32].split(b'\0',1)[0].decode('ascii') for start in (0,32))
            year=struct.unpack_from('<I',data,192)[0]
            if not (1990<=year<=1998) or {name,internal,parent}&SPECIAL_HARDWARE:continue
            if not name.isalnum() or len(name)>15:continue
            sizes,entries=parse_driver(data)
            if compatible_layout(sizes,entries):candidates[name]=(sizes,entries,parent)
    by_signature={}
    for name,(sizes,entries,parent) in candidates.items():
        signature=frozenset((crc,length) for (_,crc),length in members_required(entries).items())
        by_signature.setdefault(signature,[]).append(name)
    result=[]
    for names in by_signature.values():
        layouts={ (candidates[name][0],tuple(sorted(entry[1:] for entry in candidates[name][1]))) for name in names }
        if len(layouts)==1:
            result.append(min(names,key=lambda name:(name not in BASELINE_SETS,candidates[name][2]!='neogeo',name)))
    return tuple(sorted(result))

def read_zip(path):
    with zipfile.ZipFile(path) as z:
        entries={}
        for entry in z.infolist():
            if entry.is_dir():continue
            if entry.file_size>64*1024*1024:raise ValueError('oversized ZIP member')
            key=entry.filename.replace('\\','/').rsplit('/',1)[-1].lower()
            if key in entries:raise ValueError('duplicate ZIP member: '+key)
            entries[key]=(entry.filename,entry.CRC,entry.file_size)
        return entries

def match_member(members,filename,expected,length):
    match=members.get(filename.lower())
    if match and match[1:]==(expected,length):return match
    matches=[member for member in members.values() if member[1:]==(expected,length)]
    return matches[0] if len(matches)==1 else None

def identify_driver(members):
    matches=[]
    for name in supported_drivers():
        _,entries=driver(name)
        if all(match_member(members,filename,crc,length) for (filename,crc),length in members_required(entries).items()):
            matches.append(name)
    if len(matches)!=1:
        raise ValueError('ZIP matches multiple Neo Geo sets' if matches else 'Unknown or incomplete Neo Geo ROM set')
    return matches[0]

def convert(game,bios,output,name=None,bios_name=None):
    members=read_zip(game)
    if name is None:name=identify_driver(members)
    elif name not in supported_drivers():raise ValueError('unsupported Neo Geo set: '+name)
    sizes,entries=driver(name)
    lengths=members_required(entries)
    regions={i:bytearray(size) for i,size in enumerate(sizes) if size}
    if any(i not in (1,3,4,6,8,9) for i in regions):raise ValueError('driver needs an unsupported hardware feature')
    with zipfile.ZipFile(game) as z:
        for filename,id,src,dest,size,expected in entries:
            match=match_member(members,filename,expected,lengths[filename.lower(),expected])
            if not match:raise ValueError(f'missing or incorrect ROM: {filename}, CRC {expected:08x}')
            data=z.read(match[0]);source=src//2 if id==9 else src
            if source+size>len(data):raise ValueError('short ROM: '+filename)
            chunk=data[source:source+size]
            if id==9:
                if (dest&~1)+size*2>len(regions[id]):raise ValueError('sprite exceeds region')
                regions[id][dest:dest+size*2:2]=chunk
            else:
                if dest+size>len(regions[id]):raise ValueError('ROM exceeds region')
                regions[id][dest:dest+size]=chunk
    with zipfile.ZipFile(bios) as z:
        available={Path(n).name.lower():n for n in z.namelist()}
        def get(names,size):
            for n in names:
                if n.lower() in available:
                    data=z.read(available[n.lower()])
                    if len(data)!=size:raise ValueError('incorrect BIOS size: '+n)
                    return bytearray(data)
            raise ValueError('BIOS archive is missing '+', '.join(names))
        names=[bios_name] if bios_name else ['vs-bios.rom','sp-s2.sp1','usa_2slt.bin','asia-s3.rom']
        regions[7]=get(names,0x20000);regions[5]=get(['sfix.sfix','sfix.sfx'],0x20000)
        regions[12]=get(['000-lo.lo'],0x20000)
    if len(regions.get(6,b''))!=0x20000:raise ValueError('this version requires 128 KiB S1 fixed graphics')
    # GnGeo's native bus stores each 68000 word in host little-endian order.
    for id in (7,8):
        regions[id]=native_words(regions[id])
    # Pad the main ROM to complete 1 MiB banks, including small early games.
    regions[8].extend(bytes((-len(regions[8]))%0x100000))
    decode_graphics(regions)
    result=write_package(output,name,regions)
    result['input_sha256']={str(p):hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in (game,bios)}
    Path(str(output)+'.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('game',type=Path);ap.add_argument('--bios',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);ap.add_argument('--set',dest='name')
    ap.add_argument('--bios-name',help='exact 128 KiB MVS BIOS member name')
    args=ap.parse_args()
    try:print(json.dumps(convert(args.game,args.bios,args.output,args.name,args.bios_name),indent=2))
    except (ValueError,FileNotFoundError,FileExistsError,zipfile.BadZipFile) as e:ap.exit(1,str(e)+'\n')
if __name__=='__main__':main()
