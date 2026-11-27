# Liquid Glass 设计规范

本项目用原创 QML 实现 iOS 26 Liquid Glass 的材质与交互原则。编辑器使用安静的内容底色。悬浮工具、输入面板和临时菜单构成交互层。光线在圆润边缘集中，背景色经过折射与模糊进入控件。键盘根据背景、明暗和辅助功能改变材质。 [Apple 材质指南](https://developer.apple.com/design/human-interface-guidelines/materials) · [Adopting Liquid Glass](https://developer.apple.com/documentation/technologyoverviews/adopting-liquid-glass) · [WWDC25 Meet Liquid Glass](https://developer.apple.com/videos/play/wwdc2025/219/)

## 官方原则在 Qt 中的落点

| 官方设计原则 | VeloraIME 实现 |
| --- | --- |
| 交互层让内容透出 | 键盘与工具胶囊独立于正文表面。正文采用标准实色材料，玻璃面的背景来自宿主，窗口边缘保留可见的内容间隔 |
| Regular 材质保护密集内容 | 键盘底板、候选与常用工具选择可读性优先的 regular 表现。明暗主题调整玻璃底色、边缘反光和阴影 |
| 控件轮廓与光学共同表达深度 | 使用同心圆角、定向高光、浅阴影与边缘采样折射。常规键帽共享底板，一次按下只影响对应控件 |
| 动效延续触摸意图 | 按下压缩键帽并强化局部反馈，释放恢复轮廓。减少动态效果和减少透明度各自对应可独立切换的渲染路径 |
| 键位服务当前编辑任务 | 普通文本、邮箱、数字、PIN 各自使用匹配的布局和操作。候选区保持清晰，紧凑屏提供九键，主要独立控件保留 44×44 逻辑触摸区域 |

[Apple 虚拟键盘指南](https://developer.apple.com/design/human-interface-guidelines/virtual-keyboards) · [Apple 按钮指南](https://developer.apple.com/design/human-interface-guidelines/buttons) · [Apple 无障碍指南](https://developer.apple.com/design/human-interface-guidelines/accessibility)

## 视觉层级

| 层级 | 视觉与行为 |
| --- | --- |
| 宿主内容 | 保持在键盘之后。由宿主提供可选背景源和明暗信息，高对比度内容经过模糊与色调调整后透出 |
| 键盘底板 | 单一连续玻璃面，使用 regular 材质方向。圆角、细描边、柔和边缘反光与适度的底色共同定义轮廓 |
| 键帽与候选 | 键帽采用轻微色差和清晰触摸边界，候选条沿用面板材质。文字、图标、焦点环清晰渲染，按下或选中用对比度、轮廓与填色表达 |
| 长按菜单、撤销提示 | 共享材质语言，局部提高底色不透明度，保证与候选区的层次稳定。长按菜单靠近源键并保持在安全区域 |

采用原创字形搭配、图标、控件比例与排布。Demo 使用 Nunito 搭配 Noto Sans CJK，键盘沿用清晰的系统字形并提供语言回退。功能键提供图标或短标签及可访问名称，Emoji 作为输入内容。 [Nunito 上游](https://fonts.google.com/specimen/Nunito) · [Nunito 分发许可](../assets/fonts/LICENSE.Nunito)

## 设计参数和交互

- 语义令牌至少覆盖 `surfaceTint`、`surfaceOpacity`、`keyFill`、`keyPressedFill`、`rimHighlight`、`textPrimary`、`focusRing`、`accent`、`panelRadius` 和 `motionDuration`。浅色与深色分别调参。面板圆角初值 24、键帽 10–14、键间视觉间隔 4–8，单位均为 QML 逻辑像素，按真机 DPI 和手测调整。
- 文字合成后的对比度目标至少 4.5:1，功能轮廓、焦点环及大图标至少 3:1。以合成后像素验收，覆盖极亮、极暗、视频、纹理及高对比条纹背景。高对比模式增强文字和边界，减少透明度模式使用实色面板。
- 交互目标为 44×44 QML 逻辑像素且触控区互不重叠。十列布局在容器不足时先调整可用宽度、边距和热区，再提供放大、九键等可操作布局，紧凑 QWERTY 若达不到触控目标须记录可用尺寸区间及误触结果。不同 DPI 下以板端实际触摸区域验证。
- 5.5 英寸参考屏按 1080×1920 物理像素输出。RK3568 启动器为 VeloraIME 设置 1080 / 414 的应用缩放系数，竖屏布局为约 414×736 逻辑像素。目标触摸高度仍按逻辑像素计算，44 对应约 115 物理像素。等比旋转时复用同一缩放。候选栏可翻页或展开，宿主按键盘可见矩形让出编辑区。 [Qt High DPI](https://doc.qt.io/qt-6.12/highdpi.html)
- 按下立即给局部高光、色调或键帽反馈，按下和释放的视觉变化不改变命中区域。进退场和布局变化初值 150–240 ms，背景采样只跟随内容可见变化。减少动态效果时取消弹性、位移和持续反光，保留可读的状态变化。
- 快速多指输入保持单次触摸对应一次字符或操作。轻点、长按、滑行、空格移动光标及退格连发的触点归属要明确。长按菜单验证点选、滑选、手指移出取消以及抬起后无二次提交。触摸不依赖悬停。应能用实体键和辅助技术操作候选切换及键盘关闭。

## Qt Quick 渲染

| 模式 | 实现 |
| --- | --- |
| 完整 | 宿主显式提供独立于键盘的背景节点。使用共享、降采样的背景纹理，按面板区域取样，避免采样自己。使用 `ShaderEffectSource` 配合已打包的 QSB 或 `MultiEffect` 做背景模糊、色调与局部边缘折射。键帽共用底板采样，文字保持独立锐利 |
| 轻量 | 通过较低采样分辨率或减少更新频率保留玻璃主体。删除实时折射，优先显示稳定轮廓与按键反馈。具体阈值由真机剖析确定 |
| 实色 | 软件渲染、宿主无背景源、减少透明度或持续超过帧预算时使用。保持同一套布局、圆角、层级和状态，用底色、描边表达边界 |

背景源由宿主提供，跨进程场景使用合成器明确提供的能力。键盘隐藏时停止采样与动画。实现要检查高 DPI 下的纹理尺寸、采样裁切、亮度变动和显存，性能降级带迟滞，避免反复切换。 [Qt ShaderEffectSource](https://doc.qt.io/qt-6.12/qml-qtquick-shadereffectsource.html) · [Qt MultiEffect](https://doc.qt.io/qt-6.12/qml-qtquick-effects-multieffect.html)

## 视觉验收

用同一套参考背景记录浅色、深色、低透明、高对比、减少动态效果的截图与触摸录屏。每一档都检查普通键、修饰键、候选、长按菜单和隐私字段，覆盖横竖屏与最大字体。确认快速连打时反馈清晰、输入焦点稳定、背景视频下字形可读。真机尺寸、分辨率、Qt 渲染后端和性能指标与截图一起记录。
