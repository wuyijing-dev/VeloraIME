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

混合语料连续运行 9 组，共 378 次触摸，提交内容与预期逐字一致。首次通过时，按下到交换帧共 378 个样本，P50 为 13.64 毫秒，P95 为 38.27 毫秒。候选可见共 325 个样本，P50 为 43.25 毫秒，P95 为 55.64 毫秒。缩减词库索引内存后，同一语料再次通过。板上另有离屏循环并行时，按下 P95 为 43.94 毫秒，候选 P95 为 64.96 毫秒。

以专用启动脚本的垂直同步和分配器参数复测，264 次快速输入全部通过。按下 P95 为 20.66 毫秒，208 个候选样本的 P95 为 60.70 毫秒。378 次混合输入全部通过，按下 P95 为 30.58 毫秒，340 个候选样本的 P95 为 65.89 毫秒。

原生 Weston/Wayland 与 Mali OpenGL 下，独立 Qt Quick 宿主热拉起键盘 200 次，每轮点击首键均到达目标编辑器。首次 P95 为 34.30 毫秒，应用启动参数下复测 P95 为 46.87 毫秒。 [热拉起验证方法](keyboard-open-latency.md)

## 持续输入与内存

离屏测试在隔离的应用数据目录下反复执行英文直输、中文候选提交和 PIN。每轮在中文查询完成前切换到密码字段，验证旧候选被丢弃。生产窗口可同时运行。`soak` 统计持续运行的轮数和 Linux PSS，`memory` 另在同一窗体加载键盘模块实例前记录宿主的 PSS。

```bash
# 把上面构建的 velora-touch-runtime 放到板端 /tmp 后，在设备上运行。
qa_dir=$(mktemp -d /tmp/velora-soak.XXXXXX)
export XDG_CONFIG_HOME="$qa_dir" XDG_DATA_HOME="$qa_dir" XDG_CACHE_HOME="$qa_dir"
. /etc/profile.d/qt6.sh
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
export MALLOC_ARENA_MAX=1 MALLOC_MMAP_THRESHOLD_=65536
/tmp/velora-touch-runtime /usr/share/veloraime/qml soak unused 1800
/tmp/velora-touch-runtime /usr/share/veloraime/qml memory unused 60
```

该工作负载通过应用内控制器发起输入。离屏 PSS 用于内存回归比对，GPU 缓冲需要在实际图形配置下单独测量。

空格确认修复后的第一轮完整运行持续 1801 秒，完成 1219 轮。每轮检查英文提交、中文候选、准确提交以及切换到 PIN 后的候选隔离。进程按预期退出，无输入断言失败。PSS 峰值为 152190 KiB，结束时为 145938 KiB。该轮使用打包和板端分配器优化前的版本。

60 秒离屏实测中，宿主从未加载面板实例到加载中英文与拼音索引后的 PSS 增量由 106.6 MiB 降至 94.7 MiB。RK3568 启动脚本为 VeloraIME 进程启用上述 glibc 参数后，同一测试增量为 79.3 MiB。ARM64 模块的基础词库资源启用 zlib 后，模块文件从约 17 MiB 缩小到约 11 MiB。

在输入循环运行期间，参考板分别以 Linux `pm_test` 的 `freezer` 和 `devices` 模式验证浅睡眠路径。两个模式均自动恢复，生产应用和 Weston 保持原 PID，`pm_test` 已恢复为 `none`。设备阶段恢复时，内核记录了 xHCI 控制器重新初始化及 Realtek Wi-Fi 驱动告警。USB ADB 随后重新连接。 [Linux 内核电源管理调试说明](https://www.kernel.org/doc/html/v6.1/power/basic-pm-debugging.html)

## Weston 跨应用

主机用 Weston 14 的 desktop shell、Xvfb 和 xdotool 启动两个独立 Qt Wayland 客户端，覆盖系统键盘提交、PIN 掩码和返回普通编辑器后的退格。脚本使用独立 X11 与 Wayland 套接字。

RK3568 在独立的 Weston 14 headless/Pixman desktop shell 会话中，验证了两个实际应用的输入切换、PIN 掩码及退格、返回原字段、移动光标及回车。测试会话使用启用 xkbcommon 的独立 Qt 6.12 构建。现有 kiosk 使用应用内键盘。 [构建与板端复现](weston-rk3568.md)

```bash
bash tests/wayland_cross_app/run.sh "$HOME/rk3568/qt-host/6.12.0" \
  build/host/system/veloraime-weston
```

## 其他验证

- 固定候选语料共 61 条，首选命中由 33 条提高至 59 条，前三候选覆盖 61 条。中文整句首选由 0/7 提高至 6/7。语料随主机引擎测试运行。
- 用 RK3568 真机的 Zinnia 模型识别「十」和「一」均首选。离线语音处理 3.072 秒官方样例得到预期片段。ALSA 硬件采集路径另外验证，数据写入 `/dev/null`。
- RK3568 的离线词包回归验证了签名、回滚、损坏缓存恢复与快照发布。另以进程级文件大小限制制造写入失败和更新进程退出，旧词包和状态逐字节保留，重启后可重新更新。主机还覆盖 HTTPS、篡改、重放、禁止联网，并在词包最终写入 `state.json` 时注入 `ENOSPC` 和 `SIGKILL`。 [离线识别与词包验证](recognition-qa.md)
- 独立 Qt Quick 宿主通过安装导入和模型持久性测试。另使用 ARM64 Qt 运行时验证安装产物。 [独立宿主集成](integration.md)
