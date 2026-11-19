"""Exercise the actual device importer C on generated, non-game ROM fixtures."""
from pathlib import Path
import argparse, ctypes, hashlib, io, json, os, shutil, struct, subprocess, sys, tempfile, zipfile, zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import convert

def forge_crc(data,expected):
    prefix=zlib.crc32(data[:-4]);base=zlib.crc32(bytes(4),prefix);basis={}
    for bit in range(32):
        effect=zlib.crc32((1<<bit).to_bytes(4,'little'),prefix)^base;mask=1<<bit
        while effect:
            top=effect.bit_length()-1
            if top not in basis:basis[top]=(effect,mask);break
            v,m=basis[top];effect^=v;mask^=m
    effect=base^expected;mask=0
    while effect:
        v,m=basis[effect.bit_length()-1];effect^=v;mask^=m
    data[-4:]=mask.to_bytes(4,'little');assert zlib.crc32(data)==expected

def fixture(name):
    _,entries=convert.driver(name);sizes={};checks={}
    for filename,region,source,dest,size,crc in entries:
        start=source//2 if region==9 else source
        sizes[filename]=max(sizes.get(filename,0),start+size);checks[filename]=crc
    files={}
    for filename,size in sizes.items():
        pattern=hashlib.shake_256(filename.encode()).digest(1024)
        files[filename]=bytearray((pattern*((size+1023)//1024))[:size])
    for filename,region,source,dest,size,crc in entries:
        if region==8 and dest==0:files[filename][source:source+8]=struct.pack('<4H',0x10,0xf300,0,0x100)
    for filename,data in files.items():forge_crc(data,checks[filename])
    return files

def write_zip(path,files,method=zipfile.ZIP_DEFLATED,descriptor=False):
    class Streaming(io.BytesIO):
        def seek(self,*a):raise OSError('stream only')
    stream=Streaming() if descriptor else io.BytesIO()
    with zipfile.ZipFile(stream,'w',method,compresslevel=6 if method==8 else None) as z:
        for name,data in files.items():z.writestr(name,data)
    path.write_bytes(stream.getvalue())

def metadata_zip(path,pairs):
    pairs=sorted(set(pairs))
    write_zip(path,{f'renamed-{index}.rom':b'x' for index in range(len(pairs))})
    data=bytearray(path.read_bytes());central=struct.unpack_from('<I',data,len(data)-6)[0]
    for expected,size in pairs:
        local=struct.unpack_from('<I',data,central+42)[0]
        struct.pack_into('<I',data,local+14,expected)
        struct.pack_into('<I',data,local+22,size)
        struct.pack_into('<I',data,central+16,expected)
        struct.pack_into('<I',data,central+24,size)
        central+=46+sum(struct.unpack_from('<3H',data,central+28))
    path.write_bytes(data)

def main():
    samples=convert.BASELINE_SETS|{'aof','maglord','pbobblen','pulstar','shocktro'}
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--sets',nargs='+',default=sorted(samples));a=ap.parse_args()
    build=ROOT/'build/import-host';build.mkdir(parents=True,exist_ok=True)
    cc=Path(os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc'))
    env=dict(os.environ);env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    exe=build/'import.exe'
    subprocess.run([str(cc),'-O3','-Isrc','tests/import_cli.c','src/import_game.c','src/import_zip.c','-o',str(exe)],cwd=ROOT,env=env,check=True)
    paths_dll=build/'import-paths.dll'
    subprocess.run([str(cc),'-O3','-shared','-Isrc','src/import_game.c','src/pgm_import.c','src/import_zip.c','-o',str(paths_dll)],cwd=ROOT,env=env,check=True)
    lib=ctypes.CDLL(str(paths_dll))
    neo=lib.h1_import_paths;neo.argtypes=[ctypes.c_char_p]*3
    dest=ctypes.create_string_buffer(240);bios_path=ctypes.create_string_buffer(240)
    for filename in ('拳皇97.zip','一 大包（改名）.ZIP','a title much longer than 15 bytes.zip'):
        folder=b'B:\\\x81\x5c\\';filename=filename.encode('gbk')
        assert neo(folder+filename,dest,bios_path)==1
        assert dest.value==folder+filename[:-4]+b'.h1neo' and bios_path.value==folder+b'neogeo.zip'
    for path in (b'game.zip',b'A:\\x.txt',b'A:\\\x81.zip',b'A:\\'+b'x'*228+b'.zip'):
        before=(dest.value,bios_path.value)
        assert neo(path,dest,bios_path)==0 and (dest.value,bios_path.value)==before
    cases=((lib.h1_pgm_paths,b'kov',b'pgm.zip',b'.h1pgm','三国战记',b'kovplus'),)
    for match,short,bios_name,extension,title,clone in cases:
        match.argtypes=[ctypes.c_char_p]*4
        renamed=short.upper()+title.encode('gbk')+b'.Zip'
        long_name=short+'国'.encode('gbk')*18+b'.zip'
        accepted=((b'A:\\ARCADE\\',short+b'.zip'),(b'A:\\',short.upper()+b'.ZIP'),
                  (b'A:\\\x81\x5c\\',renamed),(b'B:/games/',renamed),
                  (b'A:\\ARCADE\\',short+b'\x81\x5c.zip'),
                  (b'A:\\'+b'x'*(229-len(long_name))+b'\\',long_name))
        for folder,filename in accepted:
            dest=ctypes.create_string_buffer(240);bios_path=ctypes.create_string_buffer(240);game_set=ctypes.create_string_buffer(16)
            assert match(folder+filename,dest,bios_path,game_set)==1
            assert game_set.value==short and dest.value==folder+filename[:-4]+extension
            assert bios_path.value==folder+bios_name
        for path in (accepted[-1][0]+b'x'+long_name,b'A:\\foo.zip',b'A:\\kof98.zip',b'A:\\\x81',short+b'.zip',
                     b'A:\\'+short+b'\x81.zip',b'A:\\'+clone+title.encode('gbk')+b'.zip',
                     b'A:\\PGM.ZIP',b'A:\\NEOGEO.ZIP'):
            guard=(ctypes.c_ubyte*256)(*([0xa5]*256));p=ctypes.cast(ctypes.byref(guard,8),ctypes.c_char_p)
            assert match(path,p,p,ctypes.create_string_buffer(16))==0
            assert bytes(guard)==b'\xa5'*256
    lib.h1_import_find_bios.argtypes=[ctypes.c_char_p,ctypes.c_int]
    lib.h1_import_find_bios.restype=None
    with tempfile.TemporaryDirectory(dir=build) as temp:
        folder=Path(temp); sub=folder/'BIOS'; sub.mkdir()
        for pgm,bios_name in enumerate(('neogeo.zip','pgm.zip')):
            old=folder/bios_name; new=sub/bios_name
            old.write_bytes(b'old')
            path=ctypes.create_string_buffer(old.as_posix().encode(),240)
            lib.h1_import_find_bios(path,pgm)
            assert path.value==old.as_posix().encode()
            new.write_bytes(b'new')
            lib.h1_import_find_bios(path,pgm)
            assert path.value==new.as_posix().encode()
    def invoke(game,bios,out,cancel=None,expect=0):
        cmd=[str(exe),str(game),str(bios),str(out)]+([cancel] if cancel else [])
        p=subprocess.run(cmd,capture_output=True,text=True,env=env)
        assert p.returncode==expect,(cmd,p.returncode,p.stdout,p.stderr)
        assert not Path(str(out)+'.tmp').exists(),p.stdout
        if expect:assert not out.exists(),p.stdout
        return p.stdout
    bios=build/'neogeo.zip';bios_files={n:bytes(range(256))*512 for n in ('vs-bios.rom','sfix.sfix','000-lo.lo')}
    bios_files['vs-bios.rom']=struct.pack('<4H',0x10,0xf300,0xc0,0x100)+bios_files['vs-bios.rom'][8:]
    write_zip(bios,bios_files)
    cases=[]
    for name in a.sets:
        game=build/(name+'.zip');files=fixture(name);write_zip(game,files)
        native=build/(name+'-native.h1neo');pc=build/(name+'-pc.h1neo')
        for path in (native,pc):path.unlink(missing_ok=True)
        invoke(game,bios,native);convert.convert(game,bios,pc)
        assert native.read_bytes()==pc.read_bytes(),name
        cases.append({'set':name,'size':native.stat().st_size,'sha256':hashlib.sha256(native.read_bytes()).hexdigest()})
    name=a.sets[0];files=fixture(name);game=build/(name+'.zip');native=build/(name+'-native.h1neo')
    for method,descriptor in ((0,False),(8,True)):
        variant=build/f'variant-{method}-{descriptor}.zip';write_zip(variant,files,method,descriptor)
        out=build/f'variant-{method}-{descriptor}.h1neo';out.unlink(missing_ok=True);invoke(variant,bios,out)
        assert out.read_bytes()==native.read_bytes()
    renamed=build/'wrong-set-name.zip';write_zip(renamed,{'folder/'+str(i)+'.rom':data for i,data in enumerate(files.values())})
    out=build/'renamed.h1neo';out.unlink(missing_ok=True);invoke(renamed,bios,out)
    assert out.read_bytes()==native.read_bytes()
    assert convert.identify_driver(convert.read_zip(renamed))==name
    assert 'result=2' in invoke(renamed,build/'no-bios.zip',out)
    # Other common 68000 byte order, with independently recomputed fixture CRCs.
    swapped={key:bytearray(value) for key,value in files.items()}
    _,entries=convert.driver(name)
    main={entry[0]:entry[-1] for entry in entries if entry[1]==8}
    for key,crc in main.items():
        data=swapped[key];data[0::2],data[1::2]=data[1::2],data[0::2];forge_crc(data,crc)
    swap_zip=build/'swap.zip';write_zip(swap_zip,swapped)
    swap_bios=build/'swap-bios.zip';swap_files=dict(bios_files);data=bytearray(swap_files['vs-bios.rom']);data[0::2],data[1::2]=data[1::2],data[0::2];swap_files['vs-bios.rom']=data;write_zip(swap_bios,swap_files)
    out=build/'swap.h1neo';pc_swap=build/'swap-pc.h1neo'
    out.unlink(missing_ok=True);pc_swap.unlink(missing_ok=True);invoke(swap_zip,swap_bios,out)
    convert.convert(swap_zip,swap_bios,pc_swap,name);assert out.read_bytes()==pc_swap.read_bytes()
    for stage in ('padding','program','sprites','verify'):
        out=build/('cancel-'+stage+'.h1neo');out.unlink(missing_ok=True);invoke(game,bios,out,stage,3)
    out=build/'rejected.h1neo';out.unlink(missing_ok=True)
    missing=build/'missing.zip';write_zip(missing,{})
    invoke(game,missing,out,expect=1)
    incomplete=build/'incomplete.zip';write_zip(incomplete,dict(list(files.items())[1:]))
    assert 'Unknown or incomplete' in invoke(incomplete,bios,out,expect=1)
    duplicate=build/'duplicate.zip';write_zip(duplicate,{'ROM.bin':b'1','rom.BIN':b'2'})
    invoke(duplicate,bios,out,expect=1)
    truncated=build/'truncated.zip';truncated.write_bytes(game.read_bytes()[:-12]);invoke(truncated,bios,out,expect=1)
    corrupt=build/'corrupt.zip';data=bytearray(game.read_bytes());fn,extra=struct.unpack_from('<HH',data,26);data[30+fn+extra+20]^=0x55;corrupt.write_bytes(data)
    invoke(corrupt,bios,out,expect=1)
    signatures={game:{(crc,size) for (_,crc),size in convert.members_required(convert.driver(game)[1]).items()}
                for game in convert.supported_drivers()}
    assert convert.BASELINE_SETS<=signatures.keys()
    meta=build/'rom-signature-test.zip'
    for game_name,signature in signatures.items():
        metadata_zip(meta,signature)
        assert convert.identify_driver(convert.read_zip(meta))==game_name
        assert 'Cannot read neogeo.zip BIOS' in invoke(meta,missing,out,expect=1),game_name
    metadata_zip(meta,signatures['kof97']|signatures['mslug'])
    assert 'multiple Neo Geo sets' in invoke(meta,bios,out,expect=1)
    try:convert.identify_driver(convert.read_zip(meta))
    except ValueError as error:assert 'multiple Neo Geo sets' in str(error)
    else:raise AssertionError('PC tool accepted an ambiguous archive')
    # Existing output is immutable, including an invalid previous cache.
    out.write_bytes(b'keep existing');p=subprocess.run([str(exe),str(game),str(bios),str(out)],capture_output=True,env=env)
    assert p.returncode==1 and out.read_bytes()==b'keep existing';out.unlink()
    fault_exe=build/'import-io-faults.exe'
    subprocess.run([str(cc),'-O3','-Isrc','tests/import_cli.c','tests/import_io_faults.c','src/import_zip.c','-o',str(fault_exe)],cwd=ROOT,env=env,check=True)
    for kind in ('write','close','rename'):
        fault_env=dict(env,IMPORT_IO_FAULT=kind)
        p=subprocess.run([str(fault_exe),str(game),str(bios),str(out)],capture_output=True,text=True,env=fault_env)
        assert p.returncode==1 and not out.exists() and not Path(str(out)+'.tmp').exists(),(kind,p.stdout)
    report={'ok':True,'environment':'actual device import C on host, no game CPU emulation','synthetic_sets':cases,
            'catalog_signatures_checked':len(signatures),'ambiguous_ZIP_rejected':True,'cache_reused_without_BIOS':True,
            'stored_deflate_and_data_descriptors':True,'cancellation_cleanup':True,'damaged_missing_duplicate_rejected':True,
            'existing_output_preserved':True,'write_close_rename_failure_cleanup':True,'GBK_and_path_length_bounds':True,
            'renamed_ROM_CRC_fallback':True,'both_68000_byte_orders':True,'byte_identical_to_PC_converter':True}
    (build/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
