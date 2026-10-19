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
