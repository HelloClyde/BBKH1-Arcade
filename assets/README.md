# 街机图标

`icon.png` 是使用内置 GPT 图像生成工具新设计的 1254×1254 RGBA 图标，
设计时参照 HelloClyde 的 H1 GBA 项目图标及物理 H1 桌面截图的高光、渐变、圆角和立体质感。
这些图片仅作生成时的风格参考，不是本仓库构建依赖。
采用蓝色底座、红橙色街机柜、球头摇杆和彩色按钮，没有游戏人物或品牌标记。
原始生成结果直接复制到项目；透明边缘由生成工具提供，未另行绘制或去背。

完整生成提示词见 [`icon-prompt.txt`](icon-prompt.txt)。输入图片只作风格参考。
原始生成文件为 `exec-327951f1-8892-4f1a-8933-94d528e52095.png`，
图标 SHA256：`a1fdd0a82360299f3f05b13764b56c758150fdd490dcdb453f2a5bac034d6ba2`。

构建时通过固定版本 SDK 转换为 45×45、57×57 的 RGB565＋Alpha 资源，
及两份 49×60 RGB565 资源，尺寸与资源保留区不变。桌面显示名称为“街机游戏”。
生成时暂定五字名称“街机游戏机”，真机标签未显示，随后缩短为四字；图标内没有文字，原始提示词保留。
独立仓库从源码重建后，由 `tools/verify_release.py` 核对整个 BDA 与真机版本逐字节一致。

# 菜单字体

`src/menu_font.h` 使用 Noto Sans CJK SC Regular 的 18×21 位图字形子集，
沿用工作区 GBA 菜单的字体与基线规则。字体许可为 OFL 1.1，完整文本见
[`licenses/NotoSansCJK-OFL.txt`](licenses/NotoSansCJK-OFL.txt)，不适用项目代码的 GPL。

来源为 notofonts/noto-cjk，固定提交 `f8d157532fbfaeda587e826d4cd5b21a49186f7c`，
`Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`，SHA256：
`2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`。
普通构建使用已生成的头文件，不需要 OTF 或网络。

从本仓库根目录再生（自行取得上述确切字体文件）：

```powershell
python -X utf8 tools/generate_menu_font.py --font "你的/NotoSansCJKsc-Regular.otf" --source src/arcade_menu.c --output src/menu_font.h
```
