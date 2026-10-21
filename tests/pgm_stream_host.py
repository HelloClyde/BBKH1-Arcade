"""Compare generated whole-frame graphics with row-streaming LCD output.

Only graphics fixtures execute. Guest CPUs, devices and MXU instructions are
not simulated; the native MXU leaves are validated separately on physical H1.
"""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
root=Path(__file__).resolve().parents[1]
out=root/'build/pgm-stream-host';out.mkdir(exist_ok=True)
cc=os.environ.get('HOST_CC',shutil.which('gcc') or 'gcc')
env=dict(os.environ);env['PATH']=str(Path(cc).parent)+os.pathsep+env['PATH']
exe=out/'stream.exe'
command=[cc,'-O2','-flto','-fwhole-program','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-Isrc','-Isdk/sdk/include','tests/pgm_stream_host.c','-o',str(exe)]
subprocess.run(command,cwd=root,env=env,check=True)
result=subprocess.run([str(exe)],cwd=root,env=env,check=True,capture_output=True,text=True)
counts=json.loads(next(line.removeprefix('PGM_STREAM_RESULT ') for line in result.stdout.splitlines() if line.startswith('PGM_STREAM_RESULT ')))
sources=('src/pgm.c','src/pgm_draw_reference.h','src/pgm_layers.h','src/pgm_clear.h','src/pgm_present_fast.h','src/pgm_stream.h','src/pgm_stream_layers.h','src/pgm_stream_sprites.h','tests/pgm_draw_host.c','tests/pgm_stream_host.c','tests/pgm_stream_host.py')
report={'ok':True,**counts,'frame_size':[448,224],'lcd_size':[480,272],
        'scope':'Generated graphics only: zoom/flip/priority, rowscroll, partial tiles, dense and sparse text plans, conflicting font-cache tiles with copied indices, text allocation failure retaining row fallback, decoded-cache conflict/capacity fallback, pool wrap, exact ordered sprite reservation with warm-hit preservation and pre-reset pin safety, virtual-tag allocation failure without cache side effects and retry, prepared sprite snapshot, stable/mode/target/UI-reset output and guards. No guest CPU or device execution model. MXU uses C fallback on host.',
        'stdout':result.stdout,'source_sha256':{s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources}}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(result.stdout,end='')
