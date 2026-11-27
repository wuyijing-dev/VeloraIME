# RK3568 输入验收

参考板为 Alientek ATK-DLRK3568，AArch64、Linux 6.1.141、Qt 6.12.0。5.5 英寸 DSI 屏以 1080×1920、54.4 Hz 输出，VeloraIME 使用 414×736 逻辑尺寸。面板测试走 Weston 和 Mali-G52 OpenGL ES 3.2。设备现有的专用开机入口以 `S95veloraime` 保持。

## 自动触摸测试

测试程序用 `uinput` 创建临时触摸设备。输入经过 Linux、libinput、Weston、Qt Quick 和输入控制器，坐标从实际屏幕上的键中心获取。测试开始前检查物理触屏是否有活动，结束后恢复当前 VeloraIME 全屏。运行时请留出短暂的空闲窗口。

```bash
rk_sdk="$HOME/rk3568"
rk_qt_host="$rk_sdk/qt-host/6.12.0"
export LD_LIBRARY_PATH="$rk_qt_host/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$rk_sdk/qt-arm64/6.12.0/bin/qt-cmake" -S tools/rk3568/touch_runtime -B build/touch-runtime-arm \
  -DCMAKE_BUILD_TYPE=Release \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$rk_sdk/toolchains/rk3568-buildroot.cmake" \
  -DQT_HOST_PATH="$rk_qt_host"
cmake --build build/touch-runtime-arm

rk_buildroot="$rk_sdk/workspace/atk_dlrk3568_linux6.1/buildroot/output/alientek_rk3568"
"$rk_buildroot/host/bin/aarch64-buildroot-linux-gnu-gcc" \
  -std=c11 -O2 -Wall -Wextra tools/rk3568/evdev_touch_probe.c -o build/evdev-touch-probe

bash tools/rk3568/touch_runtime/run_board.sh ADB_SERIAL direct
bash tools/rk3568/touch_runtime/run_board.sh ADB_SERIAL pin
bash tools/rk3568/touch_runtime/run_board.sh ADB_SERIAL pinyin
bash tools/rk3568/touch_runtime/run_board.sh ADB_SERIAL mixed
```

`direct` 键入两字母并删除一个，`pin` 验证数字模式和原生密码保护。`pinyin` 以固定节奏执行 44 组 `nihao` 加空格。`mixed` 包含短词、整句和拼音中的快速退格。两项均以最终文本、焦点和输入会话状态验收。

44 组回归已通过。板上接收了 264 次触摸，准确提交 88 个汉字。按键延迟从 Qt 触摸开始事件计到第一帧交换，263 个有效样本的 P50 为 9.20 毫秒，P95 为 34.05 毫秒。候选延迟从释放计到非空候选发布后的第一帧交换，193 个有效样本的 P50 为 40.04 毫秒，P95 为 52.59 毫秒。触屏到面板发光的延迟需要物理操作与高速录像。

## Weston 跨应用

主机用 Weston 14 的 desktop shell、Xvfb 和 xdotool 启动两个独立 Qt Wayland 客户端，覆盖系统键盘提交、PIN 掩码和返回普通编辑器后的退格。脚本使用独立 X11 与 Wayland 套接字。在 RK3568 上构建同一系统键盘目标。板上专用显示仍由 kiosk shell 承载应用内入口。

```bash
bash tests/wayland_cross_app/run.sh "$HOME/rk3568/qt-host/6.12.0" \
  build/host/system/veloraime-weston
```

## 其他验证

- 固定候选语料共 61 条，首选命中由 33 条提高至 59 条，前三候选覆盖 61 条。中文整句首选由 0/7 提高至 6/7。语料随主机引擎测试运行。
- 用 RK3568 真机的 Zinnia 模型识别「十」和「一」均首选。离线语音处理 3.072 秒官方样例得到预期片段。ALSA 硬件采集路径另外验证，数据写入 `/dev/null`。
- RK3568 的离线词包回归验证了信任签名、安装回滚、损坏缓存恢复与快照发布。主机词包回归覆盖 HTTPS、过期、篡改、重放和禁止联网。 [离线识别与词包验证](recognition-qa.md)
- 独立 Qt Quick 宿主通过安装导入和模型持久性测试。另使用 ARM64 Qt 运行时验证安装产物。 [独立宿主集成](integration.md)
