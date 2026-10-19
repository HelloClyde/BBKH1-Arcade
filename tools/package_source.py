"""Package tracked project sources only; upstream dependencies remain pinned gitlinks."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--tag',required=True);a=ap.parse_args()
    version=(ROOT/'VERSION').read_text().strip();assert a.tag=='v'+version
    commit=git('rev-parse','HEAD').decode().strip()
    assert git('rev-parse',a.tag+'^{commit}').decode().strip()==commit
    assert not git('status','--porcelain','--untracked-files=normal').strip(),'Commit all release inputs first'
    out=ROOT/'dist/release';out.mkdir(parents=True,exist_ok=True)
    archive=out/f'BBKH1-Arcade-{a.tag}-source.zip'
    entries={}
    for row in git('ls-files','-s','-z').split(b'\0'):
        if not row:continue
        head,path=row.split(b'\t',1);mode,blob,stage=head.decode().split()
        if mode=='160000':continue
        name=path.decode('utf-8')
        assert not name.startswith(('.tools/','build/','dist/'))
        assert not name.endswith(('.bda','.dll','.exe','.h1neo','.nv0','.nv1','.cfg0','.cfg1','.raw','.rom','.nand','.bin','.elf','.o'))
        entries[name]=git('show',commit+':'+name)
    manifest={k:hashlib.sha256(v).hexdigest() for k,v in entries.items()}
    entries['SOURCE-MANIFEST.json']=(json.dumps({'commit':commit,'tag':a.tag,'files':manifest,
        'dependencies':json.loads((ROOT/'UPSTREAM.json').read_text()),
        'submodules_included':False,'bootstrap':'Run python -X utf8 tools/fetch.py in the extracted directory to obtain the exact upstream dependencies.'},indent=2)+'\n').encode()
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in sorted(entries.items()):
            info=zipfile.ZipInfo(f'BBKH1-Arcade-{a.tag}/'+name,date_time=(2026,10,20,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED;info.external_attr=0o100644<<16
            z.writestr(info,data)
    with zipfile.ZipFile(archive) as z:assert not z.testzip()
    sums=out/'SHA256SUMS.txt';sums.write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
    (out/'RELEASE-NOTES.md').write_text(f'''# 街机游戏 {a.tag} — 源码预览发布

适用于步步高 H1 / Y100 V1.41，作者 HelloClyde。
公开 H1 移植层、A68K/MXU/HLE 优化、触屏菜单、转换工具及固定依赖的构建流程。
默认构建采用 408 MHz、4:3、固定跳1帧和 8 kHz 声音。

此次发布提供项目源码 ZIP 与 SHA256SUMS.txt。完整依赖请使用仓库的递归克隆命令。
固定版本 A68K MIPS 生成器没有附带可核对的完整再分发许可，因此此次不发布 BDA、DLL 或包含它们的安装包。
详细缺项见 docs/licensing.md；第三方历史核心还包含非商业限制。

物理 H1 的确切 BDA SHA256：{json.loads((ROOT/'docs/evidence/hardware-verification.json').read_text())['current_bda_sha256']}。
CI 从源码重建并核对该散列，执行实际 C 函数测试；CI 本身没有运行真机或游戏 CPU 模拟器。
游戏 ROM、商业 BIOS、固件和个人存档均不提供。

应用提交：{commit}
固定 SDK：{json.loads((ROOT/'UPSTREAM.json').read_text())['h1_sdk']['commit']}
''',encoding='utf-8')
    print(json.dumps({'archive':str(archive),'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'commit':commit},indent=2))
if __name__=='__main__':main()
