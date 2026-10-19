# 设备上首次转换游戏 ZIP

把支持的原始游戏 ZIP（例如 `kof97.zip`）和自己的 `neogeo.zip` 放在同一目录，
推荐 `A:\ARCADE\`。在“街机游戏”中直接选择游戏 ZIP，首次解压、解码并校验，
成功后在同目录生成 `.h1neo`，随后进入游戏。之后选择相同 ZIP 会直接使用已有缓存。
仍可直接选择 `.h1neo`；同名缓存沿用原有设置与存档。
更换 BIOS 或需要重新转换时，先移动或删除同名 `.h1neo` 缓存；保留旁边的 `.cfg0/1` 和 `.nv0/1` 即可继续沿用设置与存档。

游戏 ZIP 文件名需保持驱动名称：`kof94`、`kof95`、`kof96`、`kof97`、`mslug`、
`mslug2`、`samsho`、`samsho2`、`samsho3`、`samsho4`、`lastblad`。
仅支持这些原始未加密集合，列出驱动不代表全部游戏已在真机游玩验证。
文件名不匹配但 CRC 唯一匹配的 ROM 成员仍可识别。ZIP 使用常见的 stored 或 deflate 压缩。
独立目录中的游戏也会回退查找 `A:\ARCADE\neogeo.zip`。

转换时显示当前阶段和进度，按返回、ESC 或触摸“取消”可以取消。
原始 ZIP 与已有 `.h1neo` 均不覆盖。新内容写入同目录 `.h1neo.tmp`，
完整回读校验后用 V1.41 原生 FS+0x28 不覆盖改名提交；失败或取消会清理本次临时文件。
断电可能留下 `.h1neo.tmp`，删除该不完整缓存后可重新转换。
需要足够空间同时保存 ZIP 和解压后的缓存，例如拳皇97的缓存约58 MiB。

转换在游戏核心和音频打开前执行，按32 KiB块处理，不分配整个卡带。
成对精灵 ROM 使用两个32 KiB解压字典，交错合并后直接解码写出；
空洞、字符图形、68000字节序、使用标志、逐页CRC和最终容器与 PC 工具兼容。
ZIP32 目录最多512项，归档限128 MiB、单项限64 MiB，拒绝加密、ZIP64、多卷及不支持的压缩方法。

`tools/generate_import_tables.py` 从固定 GnGeo 生成 `src/import_drivers.h` 和 `src/import_gfx.h`。
`src/import_vendor/miniz_tinfl.c` 与 `miniz.h` 原样来自 rofl0r/gnuboy 固定提交
`c367bb4ba96fb07cd62f72f5ecb43aeff7012564`，保留完整 MIT 通知；此处只使用其流式解压器。
普通构建使用已生成的文件，无需额外获取 Gnuboy。

`tests/import_host.py` 在主机执行实际 C 转换代码，用生成的非游戏数据覆盖全部11个驱动，
逐字节比较 PC 工具，并检查 stored、deflate、data descriptor、取消、损坏输入和已有输出保护。
这不是游戏 CPU 或固件模拟。真机验证记录单独归档。
