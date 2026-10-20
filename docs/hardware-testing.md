# H1Debug 真机测试

本项目的发布准备没有运行游戏 CPU 或完整固件模拟器。
现有证据见 `docs/evidence/hardware-verification.json`，确切发布构建须先通过
`python -X utf8 tools/verify_release.py` 的已验证 BDA 散列与运行时代码检查。

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
真实测试需要用户自备设备、驻留器、游戏与 BIOS；CI 只执行实际 C 函数和静态检查。

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
