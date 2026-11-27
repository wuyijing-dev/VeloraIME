# 键盘热拉起延迟

探针使用独立 Qt Quick 示例，导入已安装的 `Rk.TouchKeyboard`。每轮先点击隐藏键盘，并等待隐藏状态完成一次交换。随后用 Qt 测试触摸设备轻点原本聚焦的备注编辑器。时钟从窗口收到该触点的 `TouchEnd` 开始，到 Qt Quick 交换第一帧结束。该帧的场景同步状态必须包含可见且可点击的 Q 键，并且编辑器仍有焦点。每轮再实际点一次 Q 键，确认输入到达编辑器。该次按键验证在计时区间之外。 [Qt Quick 帧同步与交换](https://doc.qt.io/qt-6.12/qquickwindow.html#frameSwapped)

探针先进行 8 轮热身，然后统计 200 次拉起。百分位采用最近秩。键盘帧超时、触点未到达或字符落入错误字段时，探针返回失败并报告轮次。

## 主机复现

在仓库根目录使用 Qt 6.12 主机工具。先构建项目库，再把安装前缀传给探针。

```sh
probe_prefix="$PWD/build/perf-install-host"
cmake --install build/host --prefix "$probe_prefix"
qt-cmake -S tests/keyboard_open -B build/keyboard-open-host -DCMAKE_BUILD_TYPE=Release
cmake --build build/keyboard-open-host

QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/keyboard-open-host/velora-keyboard-open-probe \
  --imports "$probe_prefix/share/veloraime/qml" --samples 200
```

在已有图形测试会话中可选择 `QT_QPA_PLATFORM=wayland QSG_RHI_BACKEND=opengl`。探针默认使用 414×736 逻辑窗口，并报告实际 QPA、图形 API、窗口尺寸与设备像素比。

## 主机记录

主机为 x86-64 Intel Core i7-9750H，Qt 6.12.0。两组各完成 200 次，窗体逻辑尺寸均为 414×736，DPR 为 1。

| 渲染 | P50 | P95 | 最大值 |
| --- | ---: | ---: | ---: |
| Qt offscreen 软件后端 | 12.71 ms | 13.16 ms | 14.32 ms |
| 独立 Weston 会话，Mesa llvmpipe OpenGL，16.67 ms vsync | 26.74 ms | 30.31 ms | 32.06 ms |

主机数据覆盖热拉起的应用内路径。实测截止在 Qt 把帧排入显示队列，物理触控采样、Weston 上屏时机和屏幕扫描属于其后续链路。冷启动、真机渲染和连续动画帧间隔仍适用各自的验收方法。 [RK3568 输入验收](board-acceptance.md) · [迭代验收指标](roadmap.md#首版验收方法)

## RK3568 真机记录

5.5 英寸参考板使用 Weston Wayland、Mali-G52 OpenGL、1080×1920 DSI 和 54.4 Hz 刷新。ARM64 探针以 414×736 逻辑尺寸及 2.609 的 DPR 完成 200 次热拉起。P50 为 **31.46 ms**，P95 为 **34.30 ms**，最大值 **52.65 ms**，每轮的首触键均提交至原编辑器。结果达到路线图中 200 ms 的热拉起目标，计时范围为 Qt 应用内的触摸释放至首帧提交。

## ARM64 探针

同一源程序已用 RK3568 SDK 交叉构建。可在具备 Qt Test 运行库的目标测试环境中，以与被测应用相同的兼容 Qt 运行时启动探针。探针使用独立的临时应用数据目录。

```sh
rk_sdk="$HOME/rk3568"
rk_qt_host="$rk_sdk/qt-host/6.12.0"
export LD_LIBRARY_PATH="$rk_qt_host/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$rk_sdk/qt-arm64/6.12.0/bin/qt-cmake" -S tests/keyboard_open -B build/keyboard-open-arm \
  -DCMAKE_BUILD_TYPE=Release \
  -DQT_CHAINLOAD_TOOLCHAIN_FILE="$rk_sdk/toolchains/rk3568-buildroot.cmake" \
  -DQT_HOST_PATH="$rk_qt_host"
cmake --build build/keyboard-open-arm
```

5.5 英寸参考机使用 1080×1920 的 Weston 输出、约 2.6087 倍缩放。授权测试窗口内，在目标设备的 Qt/Wayland 环境运行 ARM64 探针，使用 `--fullscreen --samples 200` 并传入已安装模块的 `--imports` 路径。实测时按探针输出区分主机和设备数据。

## 动态背景和候选帧间隔

同一探针的帧模式使用两个持续移动的背景色块。它先通过实际控制器生成 `nihao` 候选，再采集折叠候选栏、点击展开与展开后两个阶段的数据。两阶段各包含设定数量的相邻 `frameSwapped` 间隔。展开阶段从点击前一帧起算，采集结束时额外检查候选已展开且编辑器仍有焦点。

在启用了 OpenGL 的 Qt/Wayland 测试会话中运行。将 `probe` 指向对应构建目标，将 `qml_root` 指向该环境的安装目录。

```sh
QT_QPA_PLATFORM=wayland QSG_RHI_BACKEND=opengl \
  "$probe" --imports "$qml_root" --frames 300 --graphics full
QT_QPA_PLATFORM=wayland QSG_RHI_BACKEND=opengl \
  "$probe" --imports "$qml_root" --frames 300 --graphics light
```

板端可加 `--fullscreen` 复用参考机的原生显示尺寸。摘要包括 P50、P95、最大间隔和超过 20 ms 的间隔数。该测试量化持续动画下的提交节奏，样例负载为可重复的 Qt Quick 图形动画。

主机记录使用前述 Weston 与 llvmpipe 会话，414×736 逻辑尺寸，DPR 为 1。

| 玻璃与阶段 | 间隔数 | P50 | P95 | 最大值 | 超过 20 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 完整玻璃，折叠 | 300 | 16.90 ms | 17.21 ms | 18.73 ms | 0 |
| 完整玻璃，展开及展开后 | 300 | 16.93 ms | 17.45 ms | 18.97 ms | 0 |
| 轻量玻璃，折叠 | 300 | 16.92 ms | 17.49 ms | 18.73 ms | 0 |
| 轻量玻璃，展开及展开后 | 300 | 16.91 ms | 17.34 ms | 19.67 ms | 0 |

RK3568 的 SDK 环境设置了 `QSG_NO_VSYNC=0`。Qt 6.12 根据变量是否存在禁用垂直同步。专用启动脚本在创建 Qt 窗口前取消该变量。配合高 DPI 下的低分辨率玻璃层，真机动态背景的帧间隔中位数从约 43–49 ms 降至 18–19 ms。每阶段各有 300 个样本，完整玻璃与轻量玻璃的 P95 仍高于 20 ms 的目标。

| RK3568 玻璃与阶段 | P50 | P95 | 最大值 | 超过 20 ms |
| --- | ---: | ---: | ---: | ---: |
| 完整玻璃，折叠 | 18.62 ms | 33.55 ms | 35.91 ms | 76 |
| 完整玻璃，展开及展开后 | 18.48 ms | 31.70 ms | 35.05 ms | 46 |
| 轻量玻璃，折叠 | 18.85 ms | 34.13 ms | 37.91 ms | 92 |
| 轻量玻璃，展开及展开后 | 18.50 ms | 20.71 ms | 51.60 ms | 31 |
