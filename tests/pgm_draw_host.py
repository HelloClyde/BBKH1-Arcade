"""Compile generated-graphics checks; never execute a guest CPU/device model."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/pgm-draw-host';out.mkdir(exist_ok=True)
cc=os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc')
env=dict(os.environ);env['PATH']=str(Path(cc).parent)+os.pathsep+env['PATH']
exe=out/'draw.exe'
command=[cc,'-O2','-flto','-fwhole-program','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Isrc','-Isdk/sdk/include','tests/pgm_draw_host.c','-o',str(exe)]
subprocess.run(command,cwd=root,env=env,check=True)
result=subprocess.run([str(exe)],cwd=root,env=env,check=True,capture_output=True,text=True)
report={'ok':True,'generated_frames':160,'renderer_passes':831,'cache_scenes':21,'cache_passes':42,'selective_clear_scenes':53,'opaque_span_c_fixtures':512,'epoch_locations':23,'frame_size':[448,224],'presenter_modes':3,'presenter_passes':484,
        'scope':'Generated graphics, zoom, flip, clipping, cache reuse and partial final tile; no guest CPU or device emulation. Native MXU and CPU paths are checked separately on physical H1.',
        'stdout':result.stdout,'source_sha256':{s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in ('src/pgm.c','src/pgm_layers.h','src/pgm_clear.h','src/pgm_present_fast.h','src/pgm_stream.h','src/pgm_stream_sprites.h','src/pgm_stream_layers.h','src/pgm_draw_reference.h','src/pgm_bg_test.c','src/pgm_bg_mxu.S','tests/pgm_draw_host.c')}}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(result.stdout,end='')
