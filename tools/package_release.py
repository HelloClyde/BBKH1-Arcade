"""Package the verified Neo Geo/PGM BDA, installer and complete sources."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
from convert import gfx_library
from source_bundle import ROOT, sha, git, write_zip, complete_source, selected_driver_archive
from release_bios import bios_install_entries

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
    assert check['ok'] and sha(data)==check['sha256'] and check['exact_device_file_match']
    assert check['exact_physical_bda_match'] is False and check['runtime_hardware_verified'] is False
    assert check['user_runtime_validation_pending'] is True
    bios_entries,bios_manifest=bios_install_entries()
    deps=json.loads((ROOT/'UPSTREAM.json').read_text())
    out=ROOT/'dist/release';out.mkdir(parents=True,exist_ok=True)
    expected={'H1Arcade.bda',f'H1Arcade-v{version}-install.zip',
              f'BBKH1-Arcade-v{version}-source.zip','BUILD-INFO.json','SHA256SUMS.txt','RELEASE-NOTES.md'}
    assert {p.name for p in out.iterdir()}<=expected,'Move previous release outputs aside before packaging a new version'
    bda=out/'H1Arcade.bda';bda.write_bytes(data)
    entries={
        '应用/程序/H1Arcade.bda':data,
        'ARCADE/放置游戏文件.txt':'本安装包已包含 neogeo.zip 和 pgm.zip，无需解压这两个 BIOS ZIP。把自己的 kof97.zip 或三国战纪 V117 kov.zip 放在此目录，H1 首次打开会自动转换；也可放已有 .h1neo / .h1pgm。游戏 ROM 和个人存档不随安装包提供。\n'.encode(),
        '设备端转换说明.md':(ROOT/'docs/device-import.md').read_bytes(),
        'PGM三国战纪说明.md':(ROOT/'docs/pgm.md').read_bytes(),
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
    assert not set(entries)&set(bios_entries)
    entries.update(bios_entries)
    for p in sorted((ROOT/'assets/licenses').glob('*')):
        if p.is_file():entries['许可/'+p.name]=p.read_bytes()
    entries['许可/来源与许可说明.md']=(ROOT/'docs/licensing.md').read_bytes()
    manifest={name:sha(blob) for name,blob in entries.items()}
    entries['INSTALL-MANIFEST.json']=(json.dumps({'version':version,'commit':commit,'files':manifest},indent=2)+'\n').encode()
    installer=out/f'H1Arcade-v{version}-install.zip';write_zip(installer,entries)
    source=out/f'BBKH1-Arcade-v{version}-source.zip';source_info=complete_source(source,commit,a.tag)
    info={'version':version,'tag':a.tag,'application_commit':commit,'dependencies':deps,
          'bda':{'filename':bda.name,'sha256':sha(data),'size':len(data),
                 'exact_physical_bda_match':False,'exact_device_file_match':True,
                 'evidence':check['release_evidence'],
                 'installation_evidence':check['installation_evidence']},
          'installer':{'filename':installer.name,'sha256':sha(installer.read_bytes()),'files':len(manifest)},
          'source':source_info,'BIOS_distributed':True,'game_ROM_distributed':False,'bios':bios_manifest,
          'verification':check,'runtime_preset':{'cpu_mhz':408,'display':'4:3','skip':1,'sound_hz':8000}}
    (out/'BUILD-INFO.json').write_text(json.dumps(info,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    files=[bda,installer,source,out/'BUILD-INFO.json']
    (out/'SHA256SUMS.txt').write_text(''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in files))
    (out/'RELEASE-NOTES.md').write_text(f'''# 街机游戏 v{version} — H1 / Y100 开发预览

本次提供原生 BDA、安装包、完整对应源码和 SHA256 校验文件。
默认 408 MHz、4:3、固定跳1帧、8 kHz 声音，带触屏暂停菜单、按键映射、显示与音量设置。

下载 H1Arcade-v{version}-install.zip 后，把“应用”和“ARCADE”目录复制到设备 A 盘根目录。
单独 BDA 放到 A:\\应用\\程序\\H1Arcade.bda。
v0.2.2 修复普通启动会自动执行整套原生诊断、长时间停在文件选择器之前的问题。正常启动直接进入游戏选择器；CPU/MXU 等诊断代码保留，通过单独的 --startup-selftest 诊断构建选项开启，不在普通入口执行。PGM 与 Neo Geo 共用同一应用，支持《三国战纪》一代 kov V117。
安装包 ARCADE 内已包含 pgm.zip 和 neogeo.zip。把自己的 kof97.zip 或 kov.zip 复制到 ARCADE，直接选择游戏 ZIP，H1 首次转换后进入游戏；之后复用缓存。两个 BIOS ZIP 保持原样，不要解压。
转换有进度和取消按钮，保留原始 ZIP，兼容已有 .h1neo / .h1pgm。PGM 缓存约64.2 MiB，A盘写入失败并清理临时文件后可回退 B:\\ARCADE；下次仍选原ZIP自动复用。
PC工具仅转换Neo Geo，作为可选工具附带；PGM直接使用设备端转换。
源码 ZIP 包含固定 SDK、GnGeo、Musashi 和 A68K 源码快照，无需另取子模块即可重建。
上游版权与各自条款保留在 NOTICE.md、docs/licensing.md 和安装包“许可”目录；完整组合不统一重标为 GPL。
本版依用户明确发布要求原样附带其提供的 pgm.zip 和 neogeo.zip；后续 CI 同样从固定仓库输入打包并逐项校验。安装包 BIOS-MANIFEST.json 及完整源码 assets/bios/manifest.json 记录档案和成员校验值。
BIOS 保留原版权与适用条款，不重新许可为 GPL；该清单不代表已独立确认 BIOS 的再分发许可。游戏 ROM、固件和个人存档不分发。

本次仅安装新 BDA 到用户 H1，没有启动应用；上传 CRC 回执与发布 BDA 对应，SHA256：{sha(data)}。
按用户要求，文件选择器、两款游戏、暂停菜单与退出的真机验收由用户自行完成，目前仍待验证。本版为开发预览版，不能把安装成功视为运行通过。
PGM逐行合成、MIPS/MXU精灵合成、CPU等待循环HLE和8 kHz音频优化保留。
历史 v0.2.0 真机数据：同一冻结画面完整绘图与输出约22.14 ms降至14.32 ms；408 MHz、4:3、跳1帧、8 kHz下，20.3秒战斗窗口达到60逻辑/30显示FPS。此数据及旧 CPU/MXU 原生检查仅属于旧 BDA，不是本次新构建的运行验收或满速保证；首次加载新资源仍可能短暂掉速。
CI 做实际 C 函数测试和重建校验，没有运行真机或游戏 CPU 模拟器。
历史实测窗口及限制见 README 和完整源码中的真机记录。

应用提交：{commit}
固定 SDK：{deps['h1_sdk']['commit']}
''',encoding='utf-8')
    subprocess.run([sys.executable,'-X','utf8',str(ROOT/'tools/verify_packages.py'),
                    '--directory',str(out),*(['--tag',a.tag] if a.tag else []),'--commit',commit],check=True)
    print(json.dumps({'ok':True,'files':[p.name for p in files],'bda_sha256':sha(data)},indent=2))

if __name__=='__main__':main()
