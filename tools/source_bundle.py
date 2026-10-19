"""Build complete corresponding source snapshots from the recorded git objects."""
from pathlib import Path, PurePosixPath
import hashlib
import io
import json
import posixpath
import subprocess
import zipfile
from dependencies import ROOT, verify_dependencies

def sha(data):return hashlib.sha256(data).hexdigest()

def git(repo,*args):
    return subprocess.check_output(['git','-c',f'safe.directory={repo.resolve().as_posix()}',
                                    '-C',str(repo),*args])

def tracked_sources(repo,ref,upstream=False):
    rows=[];excluded=[];modes={};symlinks={}
    for row in git(repo,'ls-tree','-r','-z',ref).split(b'\0'):
        if not row:continue
        head,name=row.split(b'\t',1);mode,kind,blob=head.decode().split();name=name.decode('utf-8')
        if mode=='160000':continue
        assert kind=='blob' and mode in ('100644','100755','120000'),name
        modes[name]=mode
        assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts,name
        suffix=PurePosixPath(name).suffix.lower()
        if suffix in ('.bin','.exe','.dll','.o','.a','.lib','.elf','.bda','.h1neo','.rom','.nand','.raw','.tgz'):
            if upstream:excluded.append(name);continue
            raise AssertionError('Unexpected generated or game data in project sources: '+name)
        rows.append((name,blob))
    data=subprocess.check_output(['git','-c',f'safe.directory={repo.resolve().as_posix()}',
        '-C',str(repo),'cat-file','--batch'],input=('\n'.join(blob for _,blob in rows)+'\n').encode())
    stream=io.BytesIO(data);entries={}
    for name,blob in rows:
        actual,kind,size=stream.readline().decode().split()
        assert actual==blob and kind=='blob'
        entries[name]=stream.read(int(size));assert stream.read(1)==b'\n'
    for name in list(entries):
        if modes[name]!='120000':continue
        target=name;body=entries[name]
        symlinks[name]=body.decode('utf-8')
        for _ in range(16):
            target=posixpath.normpath(posixpath.join(posixpath.dirname(target),body.decode('utf-8')))
            assert not target.startswith('../') and not target.startswith('/') and target in modes,target
            body=git(repo,'show',ref+':'+target)
            if modes[target]!='120000':entries[name]=body;break
        else:raise AssertionError('Symbolic link loop: '+name)
    return entries,excluded,symlinks

def write_zip(path,entries,prefix=''):
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in sorted(entries.items()):
            info=zipfile.ZipInfo(prefix+name,date_time=(2026,10,20,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16
            z.writestr(info,data,compresslevel=9)
    with zipfile.ZipFile(path) as z:assert not z.testzip()

def selected_driver_archive(data):
    from convert import SUPPORTED
    stream=io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(data)) as source,zipfile.ZipFile(stream,'w') as output:
        for game in sorted(SUPPORTED):
            name='rom/'+game+'.drv'
            info=zipfile.ZipInfo(name,date_time=(2026,10,20,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            output.writestr(info,source.read(name),compresslevel=9)
    return stream.getvalue()

def complete_source(path,commit,tag):
    deps=verify_dependencies();entries,_,_=tracked_sources(ROOT,commit)
    snapshots={}
    for key,info in deps.items():
        files,excluded,symlinks=tracked_sources(ROOT/info['path'],info['commit'],upstream=True)
        transformations={}
        if key=='gngeo':
            name='gngeo.dat/gngeo_data.zip';original=files[name]
            files[name]=selected_driver_archive(original)
            transformations[name]={'original_sha256':sha(original),
                'change':'Only the 11 supported .drv metadata files are included. Unused UI and XOR data are omitted.'}
        files={info['path']+'/'+name:data for name,data in files.items()}
        entries.update(files)
        snapshots[key]={'url':info['url'],'commit':info['commit'],'path':info['path'],
                        'files':{name:sha(data) for name,data in files.items()},
                        'excluded_unlinked_binaries_or_optional_archives':excluded,
                        'asset_transformations':transformations,'dereferenced_symlinks':symlinks}
    entries['UPSTREAM-SOURCE-MANIFEST.json']=(json.dumps({'dependencies':snapshots},indent=2)+'\n').encode()
    manifest={'commit':commit,'tag':tag,'version':(ROOT/'VERSION').read_text().strip(),
              'files':{name:sha(data) for name,data in entries.items()},
              'dependencies':deps,'submodules_included':True,
              'bootstrap':'Upstream snapshots are included. Run python -X utf8 tools/fetch.py to verify them, then build normally.'}
    entries['SOURCE-MANIFEST.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    write_zip(path,entries,f'BBKH1-Arcade-v{manifest["version"]}/')
    return {'filename':path.name,'sha256':sha(path.read_bytes()),'files':len(manifest['files']),
            'complete_upstream_snapshots':True}
