# 在独立 Qt Quick 应用中集成 VeloraIME

[独立编辑表单示例](../examples/qtquick-host/Main.qml) 使用标准 Qt Quick 控件。它有自己的 CMake 项目和应用数据目录，通过安装后的 `Rk.TouchKeyboard` 导入键盘。宿主的 C++ 目标链接 Qt Quick 与 Quick Controls 2，键盘本机插件由 QML 引擎加载。

## 主机安装与运行

在 VeloraIME 仓库根目录使用 Qt 6.12 工具。当前 RK3568 SDK 的 Qt 主机工具及 ICU 环境见 [构建路线](roadmap.md#m0-起步配置)。

```sh
qt-cmake -S . -B build/library-host \
  -DVELORA_BUILD_DEMO=OFF -DVELORA_BUILD_WESTON=OFF -DBUILD_TESTING=OFF
cmake --build build/library-host
example_prefix="$PWD/build/example-install"
cmake --install build/library-host --prefix "$example_prefix"

source_example="$PWD/examples/qtquick-host"
example_imports="$example_prefix/share/veloraime/qml"
qt-cmake -S "$source_example" -B build/example-host \
  -DVELORA_EXAMPLE_IMPORT_ROOT="$example_imports"
cmake --build build/example-host
ctest --test-dir build/example-host --output-on-failure
cmake --install build/example-host --prefix "$example_prefix"
QML_IMPORT_PATH="$example_imports" "$example_prefix/bin/velora-quick-host"
```

部署时保留安装目录下完整的 `Rk/TouchKeyboard`，包括模块描述、QML 源文件和两个共享库。安装的 QML 源文件用于 IDE 和 `qmllint` 解析。运行时共享库提供内嵌的 QML、着色器和基础词库，插件用相对路径定位同目录的共享库。可将整个安装目录移动到宿主发布位置，然后更新 QML 导入路径。

宿主可在进程环境设置 `QML_IMPORT_PATH`，或在加载主界面前调用 `engine.addImportPath("/实际前缀/share/veloraime/qml")`。路径指向包含 `Rk` 的目录。用 `QML_IMPORT_TRACE=1` 可诊断导入位置。Linux 运行环境需要匹配架构与 ABI 的 Qt 6.12、Qt 的平台和 QML 插件、QSQLITE 插件及 OpenSSL 3 运行库。中文显示需要系统提供 CJK 字体或宿主注册字体。 [Qt QML 导入 API](https://doc.qt.io/qt-6.12/qqmlengine.html#addImportPath)

## 宿主布局与输入契约

从 `Rk.TouchKeyboard 1.0` 导入 `KeyboardPanel`，让它和常规 `TextField`、`TextArea` 共用一个窗口，把面板锚定于底部。示例在输入框取得焦点或已聚焦控件被再次点击时显示键盘，在 `closeRequested` 中隐藏键盘。面板内部的触摸保留编辑器焦点，面板默认控制器根据窗口的活动焦点对象更新字段策略。

| 接口 | 宿主用法 |
| --- | --- |
| `visible`、`keyboardRectangle` | 面板可见时返回相对面板自身坐标的矩形。底部锚定的面板可用其高度作为编辑区域的底部留白；其他布局可将矩形映射到宿主坐标系 |
| `safeAreaInsets` | 按左、上、右、下传入 QML 逻辑像素，例如 `Qt.vector4d(0, 0, 0, 12)` |
| `backgroundSource` | 将独立的宿主内容节点传给玻璃底板采样。示例传入位于键盘后面的兄弟节点。面板可用 `reduceTransparency`、`highContrast`、`reduceMotion`、`fontScale` 适配无障碍偏好 |
| `controller.language`、`controller.pinyinMode` | 使用 `zh-CN`、`zh-TW`、`en-US`、`en-GB` 和 `full`、`double`、`t9` |
| `controller.inputMode`、`controller.sensitive` | 读取由当前字段决定的模式与隐私状态。宿主还可设置 `controller.privateMode`，主动关闭该编辑会话中的候选和个性化操作 |
| `controller.profileId` | 为宿主用户档案指定稳定、不含敏感内容的标识。维护应用标识、数据目录与档案标识的稳定性可以延续学习数据 |
| `controller.candidates`、`controller.selectCandidate(id)` | 自定义候选界面使用每项的字符串 ID。ID 随候选代次更换，焦点变化或重新查询后应刷新列表 |
| `controller.sessionToken`、`controller.inputLiteral(text, token)` | 手写、语音等异步结果发起时保存令牌，提交时传回。宿主可以检查提交结果 |
| `closeRequested()`、`keyFeedback(action)`、`preferencesRequested()` | 分别用于隐藏键盘、接入宿主音效或振动、打开宿主设置 |

表单使用标准 Qt 字段提示。邮箱用 `Qt.ImhEmailCharactersOnly`，网址用 `Qt.ImhUrlCharactersOnly`，金额用 `Qt.ImhFormattedNumbersOnly`，PIN 用 `Qt.ImhDigitsOnly | Qt.ImhHiddenText | Qt.ImhSensitiveData` 配合 `echoMode: TextInput.Password`。用 `EnterKey.type` 指定下一项、完成或搜索动作，并由宿主控件处理提交。对独立 PIN 表单还可以通过 `TextField.onAccepted` 接入宿主验证。 [Qt EnterKey](https://doc.qt.io/qt-6.12/qml-qtquick-enterkey.html)

## RK3568 交叉构建

示例和库使用同一套 ARM64 Qt 与 Buildroot 工具链。为主机 Qt 工具补充其本机库路径，并分别构建两个项目。

```sh
rk_sdk="$HOME/rk3568"
rk_qt_host="$rk_sdk/qt-host/6.12.0"
rk_qt_target="$rk_sdk/qt-arm64/6.12.0"
rk_toolchain="$rk_sdk/toolchains/rk3568-buildroot.cmake"
export LD_LIBRARY_PATH="$rk_qt_host/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$rk_qt_target/bin/qt-cmake" -S . -B build/library-rk3568 \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$rk_toolchain" -DQT_HOST_PATH="$rk_qt_host" \
  -DVELORA_BUILD_DEMO=OFF -DVELORA_BUILD_WESTON=OFF -DBUILD_TESTING=OFF
"$rk_qt_target/bin/qt-cmake" -S examples/qtquick-host -B build/example-rk3568 \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$rk_toolchain" -DQT_HOST_PATH="$rk_qt_host" -DBUILD_TESTING=OFF
cmake --build build/library-rk3568
cmake --build build/example-rk3568
arm_prefix="$PWD/build/example-install-arm"
cmake --install build/library-rk3568 --prefix "$arm_prefix"
cmake --install build/example-rk3568 --prefix "$arm_prefix"
file "$arm_prefix/bin/velora-quick-host" \
  "$arm_prefix/share/veloraime/qml/Rk/TouchKeyboard/libVeloraIMEPlugin.so"
```

目标设备放置同样的安装树，在设备现有 Qt/Wayland 环境中执行示例，并将设备上的 `share/veloraime/qml` 提供给 `QML_IMPORT_PATH`。在现有 SDK 上可以复用板端 Qt 运行库、平台插件及 CJK 字体。

## 升级与验收

库数据在宿主的 `QStandardPaths::AppLocalDataLocation` 下保存。升级应用或替换安装前缀时，保持宿主应用标识、持久化数据目录与 `controller.profileId` 一致。正常升级沿用 SQLite 档案，可继续读取个人词、自学习记录及最近 Emoji。用于手动交换词库的 JSON 接口覆盖个人词和学习条目。应用中调用 `exportUserData(fileUrl)` 或 `importUserData(fileUrl)`，以 `userDataOperationFinished` 信号确认完成。 [Qt 应用数据目录](https://doc.qt.io/qt-6.12/qstandardpaths.html) · [加密词库快照](dictionary-sync.md)

示例测试用安装目录运行 `qmllint`，确认插件和共享库从同一安装目录加载，并通过 Qt 的触摸事件检查普通字段、字段提示、下一项动作、PIN 掩码、键盘显隐和焦点。测试可使用两个实际安装版本，在同一临时档案目录中先写入后打开。

```sh
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
export VELORA_EXAMPLE_PROFILE_HOME="$(mktemp -d)"
VELORA_EXAMPLE_IMPORT_ROOT="$old_prefix/share/veloraime/qml" \
  build/example-host/velora-quick-host-check writeUpgradeProfile
VELORA_EXAMPLE_IMPORT_ROOT="$new_prefix/share/veloraime/qml" \
  build/example-host/velora-quick-host-check readUpgradeProfile touchFocusAndFieldHints
```
