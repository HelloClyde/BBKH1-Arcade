# H1Debug 真机测试

本项目的发布准备没有运行游戏 CPU 或完整固件模拟器。
当前 v0.2.4 改为仅触屏暂停，并释放退出／返回和 P 的游戏中前端动作。本次按用户要求只发布，没有安装到 H1，也没有访问或运行设备。
`docs/evidence/hardware-verification.json` 等历史运行记录只对应各自的旧 BDA，
不能证明当前版本启动、FPS 显示、PGM / Neo Geo 游戏或性能已通过。
`python -X utf8 tools/verify_release.py` 检查当前构建与对应发布记录的散列和运行时代码；
安装或文件校验成功不等于已运行该 BDA。以下是供用户后续使用的测试步骤，本次没有执行。

手动操作设备时使用独立 [BBKH1-debug](https://github.com/HelloClyde/BBKH1-debug) 项目的 PC 客户端，
设置 `H1DEBUG_TOOLS` 指向其包含 `h1_debug.py` 的工具目录；不把客户端冒充本项目或重装正在运行的驻留服务。
以下脚本默认串口 COM4，需要先按实际 USB 枚举修改脚本中的端口。设备必须是 H1 / Y100 V1.41，
身份必须为 `H1DEBUG/1 V1.41 RESIDENT`；保留实际 resident image_id 和 BDA SHA256。

```powershell
$env:H1DEBUG_TOOLS="你的/BBKH1-debug/tools"
python -X utf8 tests/performance_hardware.py --build release-a68k-idle-dirty-selftest-o3-lto-bus-reads-tiles-mxu-spritefix-drawmxu4-audio16-visible-speed-scale-cpu408-menu --name read-only --screen
```

首次测试前核对正在运行的 BDA 与本地符号一致；不要用旧构建符号读写新程序。
测试音量设为10%。普通 BDA 部署须先正常退出前台到原生桌面；不要使用 EXEC 直接执行完整 BDA。
脚本 `menu_hardware.py` 覆盖触屏/键盘菜单、改键、显示、声音、退出重开及设置恢复。
`present_hardware.py` 在暂停游戏上比较三种显示的 LCD 像素。
所有脚本一次只占用一个串口客户端。

原生 tick 字段沿用旧命名，实际本轮固件频率为40 Hz；不能按80 Hz换算。
升降频可能触发 USB 软件重新枚举。旧句柄失效后先重新枚举、核对原驻留和保留状态，
不能自动重发触控、退出或写入动作。
真实测试需要用户自备设备、驻留器和游戏；安装包已附带两个 BIOS，只下载 BDA 时需另配 BIOS。CI 只执行实际 C 函数和静态检查。

## v0.2.4 输入行为

游戏中只通过触屏打开暂停菜单。退出／返回键和 P 不触发暂停或退出，按已有映射传给游戏；P 可在按键映射中录入。等待新按键时，返回键或触屏取消本次改键并保留原绑定；退出与返回共用底层键码，在改键等待时也用于取消。触屏取消会消费本次抬笔事件，防止误点下层菜单。结束模拟器需在暂停菜单选择“退出游戏”。
本轮只运行实际 C 菜单／配置主机检查，真机输入及游戏回归没有执行。

## v0.2.3 FPS 显示

游戏中触屏打开暂停菜单，进入“显示设置”，选择“FPS显示：开”，再继续游戏。
左上角应显示 `FPS 逻辑帧率/显示帧率`，例如 `FPS 60.0/30.0` 代表每秒约60个逻辑帧、30个实际显示帧；数值由实际计数计算，不是固定目标值或满速保证。
读数约每秒更新，进入游戏或暂停后重新采样时先显示 `FPS --.-/--.-`。

该开关默认关，并按游戏保存到原有 `.cfg0` / `.cfg1`；旧版设置加载后保留原显示、音量与按键选项，FPS 默认关。“恢复默认显示”也会关闭 FPS。
后续自行验证时，可分别在 PGM 与 Neo Geo 中核对：

1. 打开 FPS 后继续游戏，检查左上角读数更新；暂停期间不应继续累计游戏帧。
2. 切换原尺寸、4:3、全屏，再关闭 FPS，检查原画面恢复且没有文字残留。
3. 正常退出并重开同一游戏，检查开关保存；换到另一款没有此设置的游戏，检查其默认仍为关。
4. 记录所用 BDA 的 SHA256、游戏、显示模式、跳帧与采样时长；不要用旧版截图或60/30窗口代替新版测试。

这些步骤是待用户执行的检查清单，不是 v0.2.4 已完成的真机测试记录。

## 启动诊断构建

v0.2.2 已把普通启动与完整开发自检分开；v0.2.4 保留这一修复。普通发布组合沿用 `--cpu-selftest` 配置，但默认不执行启动和 PGM 载入后的完整自检。完整诊断通过下面的独立构建方式开启。
需要检查原生 CPU、MXU、绘图和音频路径时，给 `tools/build.py` 增加 `--startup-selftest`；该选项要求同时使用 `--cpu-selftest`。
例如，使用当前发布组合并显式启用诊断：

```powershell
python -X utf8 tools/build.py --core a68k --idle --dirty-present --cpu-selftest --startup-selftest --optimize 3 --lto --fast-bus --bus-reads-only --tile-direct --mxu --mxu-render --audio-fast --visible-render --speed-core --scale-output --cpu-mhz 408 --pgm --toolchain "你的工具链/bin" --host-cc "你的gcc.exe"
```

诊断构建目录名在 `-selftest` 后增加 `-startuptest`，与普通构建分别缓存；日志用 `STARTUP_SELFTEST enabled=0/1` 记录是否执行。
完整自检包含上千次全缓冲差分比较，在显示文件选择器前可能持续数十秒，只用于专门的真机验证。
诊断构建与普通构建的 BDA 散列、符号及测试证据分别记录；历史自检通过记录不自动证明新构建已完成真机验证。

## 设备端 ZIP 转换

把自备 `kof97.zip` 和 `neogeo.zip` 复制到 `ARCADE`，测试时暂时移动已有同名 `.h1neo`，
避免只命中旧缓存。保留原有 `.cfg0/1`、`.nv0/1` 和旧缓存备份。

1. 选择游戏 ZIP，在已经写入部分内容后按返回或触摸“取消”，核对本次 `.h1neo.tmp` 被清理，原 ZIP 与旧缓存备份保留。
2. 重新打开 ZIP，记录 `h1_import_stats` 的 start/end/result 和原尺寸进度截图；转换期间不打开 PCM。
3. 完成后检查容器头、全部区段 CRC、精灵页 CRC 与使用表，并与 PC 参考转换结果比较抽样数据。
4. 核对实际进入《拳皇97》、4 MiB 图形缓存、4:3、跳1、8 kHz、10%音量、暂停菜单及正常退出恢复时钟。
5. 再选同一个 ZIP，核对日志出现 `IMPORT_CACHE`、没有重新执行 `IMPORT_BEGIN`，原设置和存档继续使用。

转换回调按块处理原生事件，并消费 pen-up，防止原生定时器事件填满队列后使取消失灵。
计时只统计设备转换 start/end，不包含 USB 上传、启动自检、用户选文件与后续游戏载入。
