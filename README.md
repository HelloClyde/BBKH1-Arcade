# 街机游戏 · BBKH1-Arcade

作者：**HelloClyde**。面向步步高 **H1 / Y100 V1.41、JZ4740、480×272 屏幕**的 Neo Geo MVS 模拟器，版本 **0.1.2**，开发预览。

公开 H1 移植层、A68K/MXU/HLE 优化、触屏菜单和 PC 转换工具，固定所有上游提交。
默认采用 **408 MHz、4:3 放大、固定跳1帧（目标约30显示帧/秒）、8 kHz 声音合成**。
完整组合保留第三方各自条款与非商业限制，见[许可说明](docs/licensing.md)。
[GitHub Releases 下载](https://github.com/HelloClyde/BBKH1-Arcade/releases)提供 BDA、安装包、完整对应源码及 SHA256 校验文件。

## 快速开始

下载 Release 的 `H1Arcade-v0.1.2-install.zip`，把“应用”和“ARCADE”目录复制到设备 A 盘根目录。
也可以只下载 `H1Arcade.bda`；本地构建输出为 `dist/H1Arcade.bda`。
通过 USB U 盘把它复制到设备 **`A:\应用\程序\H1Arcade.bda`**，刷新桌面后打开“**街机游戏**”。
游戏文件放在 **`A:\ARCADE\`**。可直接选择原始游戏 ZIP：将自己的 `kof97.zip` 和 `neogeo.zip`
放在同一目录，首次在 H1 上分块转换、显示进度，之后复用生成的 `.h1neo` 缓存。
也可直接打开已有 `.h1neo`，继续使用原来的存档与设置。详见[设备端转换说明](docs/device-import.md)。
商业游戏、商业 BIOS、原机固件和个人存档均不随仓库或源码包分发。

若希望在电脑上预先转换，安装包仍附带可选“PC工具”，包含 Windows x64 图形解码 DLL，只需 Python 3.12+，无需 GCC；
在该目录运行下列命令时，将 `tools/convert.py` 换成 `convert.py`。工具只处理本地提供的原始 ZIP：

```powershell
python -X utf8 tools/convert.py "你的/kof97.zip" --set kof97 --bios "你的/neogeo.zip" --output "你的/kof97.h1neo"
```

转换输出不会覆盖同名文件。完整参数以 `python -X utf8 tools/convert.py --help` 为准。
支持驱动：`kof94`、`kof95`、`kof96`、`kof97`、`mslug`、`mslug2`、`samsho`、`samsho2`、`samsho3`、`samsho4`、`lastblad`。
目前只有用户自备的《拳皇97》完成真机功能验证，列出驱动不等于所有游戏已测通过。

PGM 实验版已用 H1Debug 真机运行《三国战纪》V117 并进入第一关；当前约 11.3 逻辑帧/秒、5.7 显示帧/秒，仍明显慢于正常速度。通过 `tools/build_pgm.py` 构建；不在上述 v0.1.2 Release 中，见 [PGM 说明与真机记录](docs/pgm.md)。

| 操作 | 默认按键 |
| --- | --- |
| 方向 | 方向键 |
| Neo Geo A / B / C / D | Z / X / C / V |
| 投币 / 开始 | 空格 / Enter |
| 打开暂停菜单 | 触摸屏幕 / P |
| 菜单操作 | 触摸，或方向键、确认、返回 |

暂停菜单提供继续游戏、10项按键映射、4:3／原尺寸／全屏显示、跳帧、音量、退出及关于。
设置按游戏保存在 ROM 旁的 `.cfg0` / `.cfg1`；NVRAM 为 `.nv0` / `.nv1`，均采用双槽与 CRC 校验。
408 MHz 在进入游戏后启用，正常退出时先停止声音并恢复原始时钟，再保存退出。
游戏中 ESC／返回也会直接退出；改映射时这些前端功能键保留。

### 开发者构建（Windows x64）

以下命令在仅含项目与固定子模块的检出中验证。需要 Python 3.12+、Git、x64 MinGW GCC；
MIPS 工具链由脚本安装，普通构建使用已生成的菜单字体，不需要下载字库。

```powershell
git clone --recurse-submodules https://github.com/HelloClyde/BBKH1-Arcade.git
cd BBKH1-Arcade
python -X utf8 -m pip install -r requirements.txt
python -X utf8 tools/install_toolchain.py
# 把你的 MSYS2 UCRT64 bin 加入 PATH，让 gcc 可用；也可通过 --host-cc 指定。
python -X utf8 tools/build_release.py
python -X utf8 tools/verify_release.py
```

Git 检出运行 `python -X utf8 tools/fetch.py` 初始化固定依赖。
完整源码 ZIP 已包含全部构建所需的固定源码快照，该命令仅验证快照，不下载上游。
若已安装工具链，可用 `--toolchain "你的工具链/bin" --host-cc "你的/gcc.exe"`，
或设置 `H1_GNU_BIN` 与 `HOST_CC`。脚本不依赖开发者用户名或本机 SDK 路径。
打包命令为 `python -X utf8 tools/package_release.py`，输出在 `dist/release/`；
正式版本标签由 CI 构建后生成同一套 BDA、安装包、完整源码、构建信息和校验文件。

## 截图

以下均为 **H1Debug 采集的物理 H1、480×272 原尺寸截图**。
第一行来自本版确切 BDA；后续桌面、选择器与菜单是加入设备端转换前的 v0.1.1 界面记录。
对战图来自菜单加入前的 408 MHz 版本，单独标明，不能据此宣称本版所有场景满速。

| v0.1.2 设备端转换 | v0.1.2 游戏画面 |
| --- | --- |
| ![首次转换进度](docs/evidence/h1-arcade-zip-import-progress-20261020.png) | ![设备生成缓存后运行拳皇97](docs/evidence/h1-arcade-zip-import-game-20261020.png) |

| v0.1.1 桌面 | v0.1.1 游戏选择 |
| --- | --- |
| ![街机游戏桌面图标](docs/evidence/h1-arcade-icon-hardware-20261020.png) | ![原生游戏选择](docs/evidence/h1-arcade-icon-picker-20261020.png) |

| 触屏暂停菜单 | 按键映射 |
| --- | --- |
| ![暂停菜单](docs/evidence/h1-arcade-menu-hardware-20261019.png) | ![按键映射](docs/evidence/h1-arcade-menu-mapping-20261019.png) |

| 音量设置 | 先前 408 MHz 版本的真机对战 |
| --- | --- |
| ![音量](docs/evidence/h1-arcade-menu-volume-20261019.png) | ![先前版本真机对战](docs/evidence/h1-arcade-scale408-hardware-20261019.png) |

## 验证范围

本版确切 BDA SHA256：`1aab71d3d1537c5b3261ee9545e20440f8e919674cee01d3ba4bcf1ba2717692`。
已通过 H1Debug 在物理 H1 上从原始《拳皇97》ZIP 与 BIOS 完成首次转换，单独转换用时 **95.825秒**（3833个40 Hz原生tick），
不包含 USB 复制、启动自检、选择文件及后续载入。缓存大小60,555,520字节；全部区段CRC、精灵页CRC与使用表、176,384字节抽样均匹配PC参考。
已验证写入超过1 MiB后触屏及ESC取消、清理临时文件、保留原ZIP与旧缓存备份，重新选择ZIP复用缓存、沿用原设置和存档，
以及实际游戏、暂停菜单和正常退出恢复时钟。音量10%，4:3、跳1、8 kHz、4 MiB图形缓存均已核对。
本版约30秒混合游戏流程窗口为58.98逻辑FPS、29.48显示FPS；恢复运行后的音频日志累计1次欠载、0输出失败。
该窗口不是确定性对战回放，也不证明所有场景满速。详情见[本版真机记录](docs/evidence/hardware-verification.json)。

此前 v0.1.1 BDA SHA256：`3bf61408351efd4d9907490f1cba1ee8103bddf35ed7d0849ba51f3b5be55b35`。
该版本的独立仓库重建与真机 BDA 逐字节相同。
旧版真机验证包括桌面标题与图标、点击启动、**13,056 组启动差分自检**、原生文件选择和正常退出，
以及《拳皇97》的组合输入、触屏/键盘菜单、改键、三种显示、音量拖动、暂停冻结、继续、退出、时钟恢复、设置和 NVRAM 保存重开。
旧版三种显示模式在菜单恢复后与参考图比较均为0像素差异。

菜单版两段约30秒窗口为 **57.93 / 58.94 逻辑 FPS**、音频欠载5 / 1次；先前无菜单的 408 MHz 版本在另外的窗口达到约59.18逻辑 FPS。
这些是不同游戏时刻，不是确定性回放对照，也不证明所有场景稳定满速或长时超频稳定。
[旧版原始数值与构建关联](docs/evidence/v0.1.1-hardware-verification.json)保留历史构建的不同散列。

CI 只做实际 C 函数测试、构建、BDA校验及确切散列比对，**不运行真机或游戏 CPU／固件模拟器**。
可在本机运行：

```powershell
python -X utf8 tests/menu_host.py
python -X utf8 tests/import_host.py
python -X utf8 tests/present_host.py --optimize 3 --mxu
python -X utf8 tests/render_host.py --mxu-render
python -X utf8 tests/audio_fast_host.py --build release-a68k-idle-dirty-selftest-o3-lto-bus-reads-tiles-mxu-spritefix-drawmxu4-audio16-visible-speed-scale-cpu408-menu
```

MXU 指令本身由真机启动自检验证；主机测试中的标量替身仅验证 C 调度与像素运算。
手工 H1Debug 测试说明见 [docs/hardware-testing.md](docs/hardware-testing.md)。

## 依赖

| 依赖 | 固定提交 / 版本 | 用途 |
| --- | --- | --- |
| [H1 BDA SDK](https://github.com/MrDefinition1999/bbk-h1-bda-sdk) | `067fe072477861dfc8949d7b1a55279fb92d2548` | 原生 ABI、BDA 头与图标打包；`sdk/` 子模块 |
| [GnGeo / Apaczer](https://github.com/Apaczer/gngeo) | `6fa682e06b60df0ec42e99f00ffc196b52cfa780` | Neo Geo 图形、总线、Z80、YM2610 与驱动信息 |
| [fba-a320 / A68K](https://github.com/dmitrysmagin/fba-a320) | `68af7cc0065757c688595adc409f5d47977793ae` | MIPS32r1 68000 代码生成器；原始作者与上游声明见许可说明 |
| [Musashi](https://github.com/kstenerud/Musashi) | `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd` | 可选 C 68000 核心，构建生成器 |
| miniz 流式解压器 | Gnuboy `c367bb4ba96fb07cd62f72f5ecb43aeff7012564` 中的两份原文件 | 已随 `src/import_vendor/` 附带，MIT，供设备端读取 ZIP |
| GNU MIPS bare-metal | 15.2.0，安装包 SHA 固定在脚本 | 仅开发构建需要 |
| 原生 MinGW GCC、Git、Python | x64 GCC；Python 3.12+ | 仅开发构建与 PC 转换需要 |
| Pillow | 12.3.0 | 图标打包、测试和可选字体再生 |
| Noto Sans CJK SC | 固定来源和 SHA 见 [assets/README.md](assets/README.md) | 已生成中文字体子集，OFL-1.1 |

设备运行不需要 Python、GCC、SDL 或桌面版 GnGeo。上游只在 build 目录生成补丁副本，不修改子模块。
本版图标由内置 GPT 图像工具重新生成，透明 PNG、提示词与来源均在 `assets/`。

## 许可与感谢

H1 原创代码采用 **GPL-2.0-or-later 加独立模块链接例外**，见 [LICENSE](LICENSE)、[LICENSE-EXCEPTION](LICENSE-EXCEPTION)。
上游核心、SDK 和字体保留各自许可，完整组合不是统一 GPL，历史 MAME 组件包含非商业限制。
A68K 的原始作者声明与来源完整保留；H1 移植层的 GPL 不替代上游组件自己的条款，见 [docs/licensing.md](docs/licensing.md)。

感谢 Peponas Mathieu、Steward Fu、GnGeo / Apaczer 贡献者；A68K 的 Mike Coates、Darren Olafson、Manuel Geran、Dmitry Smagin；
Z80 的 Juergen Buchmueller，YM2610 的 Tatsuyuki Satoh、Jarek Burczynski 及 MAME 贡献者；
Musashi 的 Karl Stenerud；H1 SDK 的 MrDefinition1999 及贡献者；Noto 字体贡献者。
感谢实际 H1 真机测试与问题反馈。完整来源声明见 [NOTICE.md](NOTICE.md)。
