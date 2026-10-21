# 设备上首次转换游戏 ZIP

v0.2.0 的默认 `H1Arcade.bda` 同时包含 PGM 与 Neo Geo。游戏和商业 BIOS 由用户自行提供；PC 转换工具仅是 Neo Geo 的可选入口，设备端即可完成首次转换。

## PGM《三国战纪》V117

把 `kov.zip` 和自己的 `pgm.zip` 放在 `A:\ARCADE\`，打开“街机游戏”并选择 `kov.zip`。目前只支持 `kov` V117；其他改版、PGM游戏和PGM2/PGM3尚不支持。
首次转换按块解压并检查成员，完成读回校验后提交独立的 `.h1pgm` 文件；不会把预处理内容写回ZIP。缓存保存原始程序、图形和声音，程序解密在加载时进行，图形仍按需解码。

`kov.h1pgm` 为67,305,856字节，约64.2 MiB。通常生成在原ZIP旁；A盘写入失败且本次临时文件已成功清理时，会尝试写入 `B:\ARCADE\kov.h1pgm`。之后仍选择原 `A:\ARCADE\kov.zip`，程序会自动复用对应缓存，无需PC工具或重复转换。实际真机本次缓存位于B盘。

请留足原ZIP与新增缓存空间。转换过程中可返回、ESC或触摸取消；原ZIP及已有缓存均保留，失败时清理本次临时文件。首次转换的成员CRC与读回检查保留，运行时省去页/区CRC计算；文件格式、长度、地址范围和读取失败检查仍保留。
缓存建立不等于全部图形已预渲染，首次遇到新ROM页或精灵仍可能加载停顿。详见[PGM说明及分阶段真机证据](pgm.md)。

## Neo Geo

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
`tests/pgm_import_host.py` 另用生成的非游戏数据检查PGM转换。这些主机测试不是游戏CPU或固件模拟。真机验证记录按各自确切BDA单独归档；Neo Geo v0.1.2的历史转换验证不等于v0.2.0已重跑Neo Geo游戏回归。
