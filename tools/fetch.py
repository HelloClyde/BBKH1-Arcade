"""Initialize pinned public upstream repositories without changing their revisions."""
import json
import subprocess
from pathlib import Path
from dependencies import verify_dependencies

ROOT=Path(__file__).resolve().parents[1]
def main():
    if (ROOT/'UPSTREAM-SOURCE-MANIFEST.json').exists():
        verify_dependencies()
        print('Bundled upstream source snapshots match the fixed commits and manifest')
        return
    deps=json.loads((ROOT/'UPSTREAM.json').read_text())
    if (ROOT/'.git').exists():
        subprocess.run(['git','submodule','update','--init','--',*(d['path'] for d in deps.values())],cwd=ROOT,check=True)
    else:
        # A source ZIP has no gitlink entries; obtain exactly its documented pins.
        for d in deps.values():
            target=ROOT/d['path']
            if target.exists():
                if not (target/'.git').exists():raise RuntimeError('Preserving unexpected existing directory: '+str(target))
            else:
                target.parent.mkdir(parents=True,exist_ok=True)
                subprocess.run(['git','clone','--no-checkout',d['url'],str(target)],check=True)
                subprocess.run(['git','-C',str(target),'checkout','--detach',d['commit']],check=True)
    for name,d in deps.items():
        p=(ROOT/d['path']).resolve()
        actual=subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD'],text=True).strip()
        assert actual==d['commit'],name
    print('All four dependencies match UPSTREAM.json')
if __name__=='__main__':main()
