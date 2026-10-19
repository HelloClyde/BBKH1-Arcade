# 许可核对

本仓库公开 H1 原创移植层与构建脚本，不把完整组合描述为统一 GPL 或无商业限制的软件。
以下核对针对 `UPSTREAM.json` 的确切版本，不用其他版本的新许可替代旧来源。

| 组件 | 证据 | 当前处理 |
| --- | --- | --- |
| H1 原创代码 | `LICENSE`、`LICENSE-EXCEPTION`、源码声明 | GPL-2.0-or-later，加独立模块链接例外 |
| GnGeo 原创部分 | 固定提交的 `README` 与 `COPYING` | 保留 GPLv2 与上游独立模块链接例外，H1 修改延续该例外 |
| Z80 | `src/mamez80/z80.c` 文件头 | 明确允许非商业修改、使用和再分发，保留署名和修改说明 |
| YM2610 | 源码版权头、GnGeo README 对 MAME 来源与非商业限制的说明 | 保留作者和历史 MAME 许可；不套用现行 MAME 的新许可 |
| Musashi | 固定提交 `m68k.h` 文件头 | 保留 MIT 风格许可全文 |
| H1 SDK | 固定提交 `LICENSE` | Apache-2.0 |
| 菜单字体 | Noto Sans CJK SC、OFL-1.1 全文 | 字体数据独立适用 OFL，不套项目代码 GPL |
| 图标 | 项目 GPT 图像生成 PNG 与提示词 | 作为项目资产提供，来源记录见 `assets/README.md`；不宣称拥有第三方品牌或游戏角色权利 |

## A68K MIPS 来源与声明

实际使用的来源为
[`dmitrysmagin/fba-a320@68af7cc0065757c688595adc409f5d47977793ae`](https://github.com/dmitrysmagin/fba-a320/tree/68af7cc0065757c688595adc409f5d47977793ae)，
文件 `src/cpu/a68k/mips32r1/fba_make68k.c`。
原文件头声明 Mike Coates、Darren Olafson、Manuel Geran、Dmitry Smagin 的版权，
完整头部原文另外保存在 `assets/licenses/A68K-copyright.txt`。
H1 适配和总线优化在本项目中，生成器的修改仅作用于 build 副本；上游源码保持固定提交。
完整对应源码包包含这些源文件和生成脚本，成品 BDA 与源码同一版本发布。

该固定检出没有单独附带完整的 A68K MIPS 许可文本，因此这里保留实际作者声明和原始来源，
不为上游另造授权，也不把它重标为本项目的 GPL。
历史 FBA 的 [组件声明](https://github.com/libretro/fbalpha2012/blob/master/docs/license.txt)
同样把 A68K 列为独立组件。第三方各自条款与非商业限制继续保留在源码和安装包中。

## 完整对应源码

Release 的源码 ZIP 附带 SDK、GnGeo、Musashi 和 A68K 的固定源码快照与逐文件 SHA256 清单。
只排除未链接的历史二进制、生成测试程序和可选归档；GnGeo 数据归档仅保留11个支持游戏的驱动元数据，
不包含未使用的 XOR 数据或游戏内容。快照归档中的改动与排除项记录在 `UPSTREAM-SOURCE-MANIFEST.json`。
解压即可离线使用这些依赖构建，无需依赖 git 子模块服务继续可用。

## 已保存的许可文本

`assets/licenses/` 包含原始 GPL、GnGeo 链接例外、Z80 / YM2610 / Musashi 声明、SDK Apache-2.0 和字体 OFL。
历史 MAME 许可原文来自官方
[`mamedev/mame@5cef4e1f91010f0d573bbd662208979fa39e73ec/docs/mamelicense.txt`](https://github.com/mamedev/mame/blob/5cef4e1f91010f0d573bbd662208979fa39e73ec/docs/mamelicense.txt)，
文件 SHA256 为 `42f4fd8ab87595919d28824337e53fd0040e9d7354d1da4189701742e1f50465`。
保留这份文本用于记录 GnGeo 所述历史 MAME 条款；它不替代其他组件自己的版权与许可声明。

所有游戏及商业 BIOS 由用户自行提供，本项目不分发这些数据或原机固件。
