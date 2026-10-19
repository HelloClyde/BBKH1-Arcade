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

## A68K MIPS 缺项

实际使用的来源为
[`dmitrysmagin/fba-a320@68af7cc0065757c688595adc409f5d47977793ae`](https://github.com/dmitrysmagin/fba-a320/tree/68af7cc0065757c688595adc409f5d47977793ae)，
文件 `src/cpu/a68k/mips32r1/fba_make68k.c`。
它声明 Mike Coates、Darren Olafson、Manuel Geran、Dmitry Smagin 的版权，
但该固定检出没有可核对的完整许可文件，生成器文件也没有明确的修改与再分发授权条款。
所以不能仅凭 GitHub 公开可读、其他 FBA 版本的总体许可或上游文件没有 GPL 头，推断此 MIPS 版本可统一按 GPL 发布。

已核对的 [FBA2012 许可文件](https://github.com/libretro/fbalpha2012/blob/master/docs/license.txt)
把 A68K 列为具有独立许可的组件；它不能代替上述固定版本中缺失的明确授权。
本仓库只记录原上游的子模块 URL 与固定 gitlink，不复制、重许可该生成器。
本次公开 Release **仅含 H1 项目源码**，不含 A68K BDA、DLL、生成汇编或安装包。
CI 可以在其工作目录构建和核对散列，但不会把这些二进制或生成器输出上传为公开工件。
在取得对应作者授权或可核对的完整许可前，不开放其二进制下载。

## 已保存的许可文本

`assets/licenses/` 包含原始 GPL、GnGeo 链接例外、Z80 / YM2610 / Musashi 声明、SDK Apache-2.0 和字体 OFL。
历史 MAME 许可原文来自官方
[`mamedev/mame@5cef4e1f91010f0d573bbd662208979fa39e73ec/docs/mamelicense.txt`](https://github.com/mamedev/mame/blob/5cef4e1f91010f0d573bbd662208979fa39e73ec/docs/mamelicense.txt)，
文件 SHA256 为 `42f4fd8ab87595919d28824337e53fd0040e9d7354d1da4189701742e1f50465`。
保留这份文本用于记录 GnGeo 所述历史 MAME 条款，不据此替 A68K 的 MIPS 修改者补授权。

所有游戏及商业 BIOS 由用户自行提供，本项目不分发这些数据或原机固件。
