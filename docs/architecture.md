# RK3568 QML 触摸键盘架构

状态为初始设计。首版面向 Qt Quick 应用内嵌，支持原生焦点输入、多种触摸布局和完全离线的中文输入。词包可在线更新，自学习默认留在设备。应用可独立导入模块，不依赖示例程序。

## 已核实的本机环境

| 项目 | 位置或结果 |
| --- | --- |
| Rockchip SDK | `/home/wyj/rk3568/workspace/atk_dlrk3568_linux6.1`，内核源码 Makefile 为 6.1.141 |
| 主机 Qt | `/home/wyj/rk3568/qt-host/6.12.0`，加入该前缀的 `lib` 到主机工具的 `LD_LIBRARY_PATH` 后，`qtpaths` 与 `qmllint` 实测均为 6.12.0 |
| 板端 Qt | `/home/wyj/rk3568/qt-arm64/6.12.0`，Qt Core 为 AArch64 ELF，包含 Wayland、eglfs、Qt Virtual Keyboard、QSQLite 和 TLS 插件文件 |
| 交叉工具链 | `/home/wyj/rk3568/toolchains/rk3568-buildroot.cmake`，编译器返回 `aarch64-buildroot-linux-gnu` |
| 固件模板 | `/home/wyj/rk3568/firmware-overlay`，配置为 Weston DRM + kiosk shell、`QT_QPA_PLATFORM=wayland`，Qt 目标安装前缀为 `/usr/local/qt6.12.0` |
| 现有示例与字体 | `/home/wyj/rk3568/examples/qt6-demo`，固件模板包含 Noto Sans CJK 和 DejaVu Sans |

真机正在运行的内核、Weston 版本、协议、渲染后端、屏幕、触控、设备存储与网络可达性由 M0 实测。

## 库与宿主的边界

| 模块 | 职责 |
| --- | --- |
| QML 模块 `Rk.TouchKeyboard`（暂定） | 通过 CMake `qt_add_qml_module` 发布，公开 `KeyboardPanel`、候选栏和主题配置。布局数据描述键位、可用操作、语言及尺寸策略 |
| 输入控制器 | C++ 持有输入会话、修饰键、预编辑、候选页和字段策略。候选用只读模型向 QML 提供稳定 ID，避免将显示索引用作提交标识 |
| 宿主输入适配器 | 通过当前窗口的焦点对象读取 Qt 输入法查询及 hints，发送 `QInputMethodEvent` 处理预编辑、提交和替换。退格、方向键、回车及快捷操作沿用宿主测试过的编辑路径 |
| 词库与引擎 | librime 负责中文拼音分词、候选和整句转换。英文和直输布局先独立工作，英文建议和纠错可换引擎。后台线程串行访问单个引擎会话 |
| 数据服务 | 拆分只读基础词库、用户可写数据、热词及扩展词包。下载、索引与数据库放到后台，QML 仅接收状态和结果 |

首版公共入口拟提供 `KeyboardPanel`、`visible`、`locale`、`inputMode`、`keyboardRectangle`、`safeAreaInsets`、主题及隐私设置，具体 API 由 M1 Demo 固化。宿主将普通 `TextField`、`TextArea` 等编辑器与面板置于同一个窗口，按键盘矩形滚动或缩放内容，面板触摸不抢编辑焦点。输入法显示、隐藏和宿主焦点配合方式由 Demo 验证。候选浮层、选键与长按菜单都遵守同一焦点规则。

会话按窗口和焦点对象绑定。每次查询记录会话 ID、请求代次和布局版本，仅在三者仍匹配时刷新候选。删除先处理预编辑，随后交给宿主编辑器，并验证组合音标、ZWJ 表情和中日韩文字。替换用 Qt 定义的偏移语义。销毁编辑器、切字段、隐藏键盘或切布局会使旧会话操作失效，预编辑的提交或取消规则需写入集成测试。回车按宿主 hints 执行搜索、完成、下一项或换行。

系统级输入单独验证 Weston 的客户端文本输入协议及输入法端协议、权限与同 Weston 自带键盘的协调。届时复用相同布局与引擎，通过实测决定适配接口。Qt Virtual Keyboard 能做扩展输入法，SDK 也已安装该模块。其官方发行条款为 Commercial 或 GPLv3，因此基础库默认使用 Qt 公开的 GUI 与 Quick 接口。若后续做 Qt Virtual Keyboard 适配，先核对交付许可。 [Qt 输入法事件](https://doc.qt.io/qt-6.12/qinputmethodevent.html) · [Qt Virtual Keyboard](https://doc.qt.io/qt-6.12/qtvirtualkeyboard-index.html)

## 本地词库与自学习

- **基础包**内置简体全拼，保留繁体、双拼、九键的输入方案入口。优先验证 librime 与朙月拼音，按发行组合清单单独评估雾凇拼音。librime 为 BSD-3-Clause，朙月拼音上游标示 LGPL-3.0，雾凇拼音上游标示 GPL-3.0-only。发布前逐项核对词典、OpenCC 数据、传递依赖和字体的实际文件许可。 [librime](https://github.com/rime/librime) · [朙月拼音](https://github.com/rime/rime-luna-pinyin) · [雾凇拼音](https://github.com/iDvel/rime-ice)
- **构建**锁定 librime、方案和词典源版本。M0 确认 Boost、LevelDB、Marisa、OpenCC、yaml-cpp 的 sysroot 可用性，缺项按相同 ABI 交叉构建。验证主机预编译词库在板上是否可读，必要时在目标平台生成并缓存。开机阶段的耗时由 M0/M2 测量。
- **用户数据**放入设备用户目录并按宿主提供的用户配置隔离。显式用户短语、频率与近期性、候选屏蔽、导入和持久化用本地 SQLite。首选应用统一管理学习权重，验证 Rime 方案可关闭写入，防止同一次提交被计算两次。学习仅记录实际接受的非敏感词项与必要编码，不保存整段上下文。支持关闭、单词删除、全部清除及用户主动导出。
- **排序**以匹配程度和本地语言模型为基础，显式用户词优先，学习按频率与近期性有限加权，热词按时效轻量加权。热词候选必须与当前编码相符，合并时保留 Rime 的分段与整句语义。目标是新名词可输入且常用词排序稳定。热词接入先验证候选注入或合并与 Rime 提交是否一致。
- **隐私**保守处理密码、PIN、隐藏文字、敏感提示、禁预测字段及宿主指定的隐私模式。敏感或隐私字段仅保留允许的直输布局，禁用候选、学习、历史和剪贴板入口。输入上下文按场景最少读取。清除学习后重启仍应为空，日志与接受的词项无关。

## 云端词库和热词，无需自建服务器

采用**静态词包分发**。设备只获取发布清单与经过签名的公开词包，候选生成仍在本地。扩展词包包含行业词、地名等低频更新内容，热词包存放带有效期的人名、事件及新词。用户自己的短语和学习数据与这些包独立。

1. 先实现本地发布脚本与离线导入，配可复现测试词包。正式上线时，可用 GitHub Actions 周期采集并生成草稿，人工抽查后签名发布至 GitHub Pages。自动化调度可能延迟，设备只依赖最近一次有效发布。生产签名密钥由发布方管理，私钥不在固件内。 [GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages) · [GitHub Actions 调度](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#schedule)
2. 热词 MVP 用 Wikimedia 中文站访问量榜作为趋势信号，叠加人工维护的词条。流水线过滤非词条、敏感个人信息及无效标题，复核多音字、拼音和字符长度，保留来源和批准记录。此来源反映站点阅读趋势。后续可增加获授权数据源，高优先级保留车机或行业自定义包。 [Wikimedia 热门页面 API](https://doc.wikimedia.org/generated-data-platform/aqs/analytics-api/reference/page-views.html) · [Wikimedia API 使用规范](https://foundation.wikimedia.org/wiki/Policy:Wikimedia_Foundation_API_Usage_Guidelines)
3. 签名清单包含 `schemaVersion`、`packId`、`kind`、`locale`、`version`、`publishedAt`、`expiresAt`、兼容版本范围、`size`、`sha256`、下载地址和签名键 ID。词条使用规范化文本、拼音、有限权重及可选过期时间。只接受受信发布键与纯数据格式，拒绝脚本、超限文件和网络旧版本重放。支持受信任的签名密钥轮换。
4. 联网由宿主或用户开启。后台按日尝试检查，使用 ETag、退避与失败缓存，输入时不触发请求。下载至暂存区，验证签名、摘要、格式、体积和版本后构建索引，再原子切换指针，保留上一版。已有组合会话继续用旧快照。热词包超期后停止加权，系统时间不可信时先保留基础词库。回滚通过本地上一版完成，并保留远端版本高水位。
5. 在目标网络实测 DNS、HTTPS、证书和板端时钟。若发布点不可达，可把同一签名包放到能访问的静态镜像，或通过 U 盘、局域网文件分发导入。签名验证保持一致。断网、签名失败或发布暂停时，仍可使用基础包、用户词库及有效的缓存包。

在线逐键联想、第三方语音接口及账号同步属于后续可选能力。主线无需它们。首个公开发布需有可访问的托管位置和维护者，当前阶段仅确定方案。
