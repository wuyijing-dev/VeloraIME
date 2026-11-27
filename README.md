# VeloraIME

面向 RK3568、Linux 6.1 与 Qt 6.12 的可嵌入 QML 触摸键盘。项目使用原创 Liquid Glass 界面，面向应用内文本输入，并以静态签名词包实现无需自建服务器的词库更新。

简体和繁体支持全拼、小鹤双拼、九键、联想与用户短语。英文支持美式和英式词序、补全、纠错与滑行输入。面板提供数字、符号、Emoji、剪贴板、长按、光标拖动及单手布局。键盘默认离线，可在设置中启用公开词包更新与剪贴板历史。

## 应用内集成

将安装后的 `share/veloraime/qml` 加入宿主的 QML 导入路径。键盘与编辑控件放在同一窗口，宿主按面板的可见矩形为编辑器腾出空间。

```qml
import QtQuick
import QtQuick.Controls
import Rk.TouchKeyboard

ApplicationWindow {
    id: window
    width: 1024
    height: 600
    visible: true
    property bool keyboardOpen: true

    Item {
        id: page
        anchors.fill: parent
        TextField {
            anchors.centerIn: parent
            placeholderText: "输入文本"
            focus: true
            onActiveFocusChanged: if (activeFocus) window.keyboardOpen = true
            TapHandler { onTapped: window.keyboardOpen = true }
        }
    }

    KeyboardPanel {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: window.keyboardOpen
        backgroundSource: page
        onCloseRequested: window.keyboardOpen = false
    }
}
```

## 构建和运行

依赖 Qt 6.12、CMake 3.25 或更新版本和 OpenSSL 3。使用 Qt SDK 的 `qt-cmake`，其中已配置对应 Qt 安装前缀。本机 Rockchip SDK 的路径与交叉命令见 [迭代路线](docs/roadmap.md)。

```bash
qt-cmake -S . -B build/host
cmake --build build/host
ctest --test-dir build/host --output-on-failure
./build/host/veloraime-demo
```

## 手写、语音和 Weston

- 手写使用本机 Zinnia 库和 Tegaki 模型。默认模型位置为 `/usr/share/tegaki/models/zinnia/handwriting-zh_CN.model`，可用 `VELORA_HANDWRITING_MODEL` 指定本地模型。两项均可用时，键盘工具栏显示手写入口。 [Zinnia 上游](https://taku910.github.io/zinnia/) · [Tegaki 模型](https://tegaki.github.io/)
- 离线语音使用 ALSA 的 `arecord` 和本机 sherpa-onnx SenseVoice，默认复用板上 `/userdata/rk3568-voice` 的 RKNN 运行时与模型。以 `VELORA_SPEECH_ROOT`、`VELORA_SPEECH_MODEL`、`VELORA_SPEECH_TOKENS`、`VELORA_SPEECH_PROVIDER` 和 `VELORA_CAPTURE_DEVICE` 配置。工具栏由用户点击开始录音，密码字段隐藏入口，切字段会丢弃旧结果。 [sherpa-onnx RK NPU](https://k2-fsa.github.io/sherpa/onnx/rk-npu/index.html)
- 使用 `-DVELORA_BUILD_WESTON=ON` 构建独立的 `veloraime-weston`。需要 wayland-client、xkbcommon、wayland-protocols 和与 Qt 运行时同构建版本的 GuiPrivate 开发头文件。Weston 的 desktop shell 以 `[input-method]` 的 `path=/usr/bin/veloraime-weston` 启动输入法。现有板上 kiosk shell 使用应用内组件，系统输入客户端需要支持 input-method-v1 的 Weston shell。

## RK3568 固件集成

以下命令以现有 RK3568 SDK 的 firmware 输出为底图。脚本构建新镜像，验证 AArch64 ABI、镜像内文件和文件系统，将结果写到 `build`。原 SDK 固件可继续用于恢复。

```bash
DESTDIR="$PWD/build/stage-rk3568" cmake --install build/rk3568 --prefix /usr
python3 tools/rk3568/package_firmware.py \
  --stage build/stage-rk3568 --output build/veloraime-rk3568-update.img
sha256sum build/veloraime-rk3568-update.img
```

固件把已知的其他显示应用启动器移至可恢复目录。Weston 的 Wayland 套接字就绪后，启动脚本全屏启动 VeloraIME，日志位于 `/var/log/veloraime-demo.log`。`/userdata` 已挂载时，设置与词库缓存在该分区。全量 Rockchip 镜像包含 userdata 分区，刷写前需保留设备上的数据并确认板型与存储介质。

对正在使用的板子，可增量安装到 eMMC，并在现有 Weston 上验证输入。此路径适合保留板上的其他应用与数据。

```bash
bash tools/rk3568/deploy_adb.sh build/stage-rk3568 ADB_SERIAL
```

加上 `--exclusive` 后，板子开机进入 VeloraIME 全屏。专用模式保留 Weston 与硬件、网络服务，并把已知的其他显示应用启动器移至 `/etc/veloraime/paused-init`。在板端以 root 运行 `veloraime-display restore` 可还原启动器配置，重启后生效。5.5 英寸 1080×1920 DSI 屏默认采用约 414×736 逻辑像素，物理分辨率由 Weston 保持。应用缩放可通过 `/etc/default/veloraime` 的 `VELORA_DISPLAY_SCALE` 覆盖。

## 文档

- [开发约定](AGENTS.md)
- [架构、词库与隐私](docs/architecture.md)
- [加密词库同步](docs/dictionary-sync.md)
- [Liquid Glass 设计规范](docs/liquid-glass.md)
- [迭代路线与验收](docs/roadmap.md)
