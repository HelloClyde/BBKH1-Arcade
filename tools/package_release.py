"""Package the checked BDA, Windows converter, installer and complete sources."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
from convert import gfx_library
from source_bundle import ROOT, sha, git, write_zip, complete_source, selected_driver_archive

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--tag');a=ap.parse_args()
    version=(ROOT/'VERSION').read_text().strip();commit=git(ROOT,'rev-parse','HEAD').decode().strip()
    assert not git(ROOT,'status','--porcelain','--untracked-files=normal').strip(),'Commit all package inputs first'
    if a.tag:
        assert a.tag=='v'+version
        assert git(ROOT,'rev-parse',a.tag+'^{commit}').decode().strip()==commit
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tools/verify_release.py')],check=True)
    gfx_library()
    data=(ROOT/'dist/H1Arcade.bda').read_bytes()
    check=json.loads((ROOT/'build/verification/release-verification.json').read_text())
    assert check['ok'] and sha(data)==check['sha256'] and check['exact_physical_bda_match']
    deps=json.loads((ROOT/'UPSTREAM.json').read_text())
    out=ROOT/'dist/release';out.mkdir(parents=True,exist_ok=True)
    bda=out/'H1Arcade.bda';bda.write_bytes(data)
    entries={
        '应用/程序/H1Arcade.bda':data,
        'ARCADE/放置游戏文件.txt':'把自行转换的 .h1neo 游戏放在此目录。ROM、商业 BIOS 和个人存档不随安装包提供。\n'.encode(),
        '使用说明.md':(ROOT/'README.md').read_bytes(),
        'NOTICE.md':(ROOT/'NOTICE.md').read_bytes(),
        '许可/H1-GPL-2.0.txt':(ROOT/'LICENSE').read_bytes(),
        '许可/H1-linking-exception.txt':(ROOT/'LICENSE-EXCEPTION').read_bytes(),
        'PC工具/convert.py':(ROOT/'tools/convert.py').read_bytes(),
        'PC工具/gfx.dll':(ROOT/'build/convert/gfx.dll').read_bytes(),
        'PC工具/gfx-source.c':(ROOT/'build/convert/gfx.c').read_bytes(),
        'PC工具/upstream-roms.c':(ROOT/'third_party/gngeo/src/roms.c').read_bytes(),
        'PC工具/gngeo_data.zip':selected_driver_archive((ROOT/'third_party/gngeo/gngeo.dat/gngeo_data.zip').read_bytes()),
        'PC工具/使用说明.txt':('需要 Windows x64 和 Python 3.12+；本目录已带图形解码 DLL，不需要安装 GCC。\n'
            'python -X utf8 convert.py "你的/kof97.zip" --set kof97 --bios "你的/neogeo.zip" --output "你的/kof97.h1neo"\n'
            '图形解码器来自固定版本 GnGeo src/roms.c，保留原版权声明和完整对应源码。\n').encode(),
    }
    for p in sorted((ROOT/'assets/licenses').glob('*')):
        if p.is_file():entries['许可/'+p.name]=p.read_bytes()
    entries['许可/来源与许可说明.md']=(ROOT/'docs/licensing.md').read_bytes()
    manifest={name:sha(blob) for name,blob in entries.items()}
    entries['INSTALL-MANIFEST.json']=(json.dumps({'version':version,'commit':commit,'files':manifest},indent=2)+'\n').encode()
    installer=out/f'H1Arcade-v{version}-install.zip';write_zip(installer,entries)
    source=out/f'BBKH1-Arcade-v{version}-source.zip';source_info=complete_source(source,commit,a.tag)
    info={'version':version,'tag':a.tag,'application_commit':commit,'dependencies':deps,
          'bda':{'filename':bda.name,'sha256':sha(data),'size':len(data),
                 'exact_physical_bda_match':True,'evidence':'docs/evidence/hardware-verification.json'},
          'installer':{'filename':installer.name,'sha256':sha(installer.read_bytes()),'files':len(manifest)},
          'source':source_info,'ROM_or_commercial_BIOS_distributed':False,
          'verification':check,'runtime_preset':{'cpu_mhz':408,'display':'4:3','skip':1,'sound_hz':8000}}
    (out/'BUILD-INFO.json').write_text(json.dumps(info,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    files=[bda,installer,source,out/'BUILD-INFO.json']
    (out/'SHA256SUMS.txt').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in files))
    (out/'RELEASE-NOTES.md').write_text(f'''# 街机游戏 v{version} — H1 / Y100 开发预览

本次提供原生 BDA、安装包、完整对应源码和 SHA256 校验文件。
默认 408 MHz、4:3、固定跳1帧、8 kHz 声音，带触屏暂停菜单、按键映射、显示与音量设置。

下载 H1Arcade-v{version}-install.zip 后，把“应用”和“ARCADE”目录复制到设备 A 盘根目录。
单独 BDA 放到 A:\\应用\\程序\\H1Arcade.bda。PC 转换工具在安装包 PC工具 目录，需要 Windows x64 和 Python。
源码 ZIP 包含固定 SDK、GnGeo、Musashi 和 A68K 源码快照，无需另取子模块即可重建。
上游版权与各自条款保留在 NOTICE.md、docs/licensing.md 和安装包“许可”目录；完整组合不统一重标为 GPL。
游戏 ROM、商业 BIOS、固件和个人存档不分发。

本次发布只完善构建与打包，BDA 与已有真机验证版本逐字节相同：{sha(data)}。
CI 做实际 C 函数测试和重建校验，没有运行真机或游戏 CPU 模拟器。
历史实测窗口及限制见 README 和完整源码中的真机记录。

应用提交：{commit}
固定 SDK：{deps['h1_sdk']['commit']}
''',encoding='utf-8')
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tools/verify_packages.py'),
                    '--directory',str(out),*(['--tag',a.tag] if a.tag else []),'--commit',commit],check=True)
    print(json.dumps({'ok':True,'files':[p.name for p in files],'bda_sha256':sha(data)},indent=2))

if __name__=='__main__':main()
