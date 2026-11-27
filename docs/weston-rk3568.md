# RK3568 Weston 跨应用验收

本页用于在运行量产 Weston 的 RK3568 上开一个独立的 800×640 无头会话。测试复用已安装的 `/usr/bin/veloraime-weston`，通过 Weston 测试协议的虚拟 seat 操作两个独立 Qt 6.12 Wayland 进程。量产 Weston、kiosk、DRM、物理输入和 Qt 安装保持原样。所有构建与板端验证文件放在各自的临时目录。

## 实测结果

在 Linux 6.1.141 / Weston 14.0.2 的 RK3568 上，普通字段收到 `qw`，切换到 PIN 字段后收到并掩码显示 `12`，退格恢复掩码的 `1`。关闭 PIN 客户端后，普通字段接受 `q` 成为 `qwq`，退格恢复 `qw`，在空格键上左拖可移动光标，回车触发 `TextField.accepted`。量产 Weston 的 PID 在测试前后均为 715。对私有 Weston、输入法和两个客户端检查了打开的文件描述符，未发现 DRM、Mali、DMA heap、物理输入或 seatd 描述符。

板端原装 QtBase 将 `FEATURE_xkbcommon` 设为 OFF。Qt 6.12 的 `QWaylandTextInputV1::zwp_text_input_v1_keysym` 在该配置下忽略 keysym。实测原装客户端收到了 Backspace 按下和抬起，文本仍为 `qwq`。提交文本走 text-input-v1 的 commit string 请求，仍可输入普通文字和 PIN。启用 xkbcommon 的独立构建通过了全部上述检查。

本机复测产物位于 `/tmp/velora-board-headless.0568i4f8`。其中 `xkb-regression-pass.log` 记录应用标记，`xkb-client-qt-maps.log` 记录实际载入的 Qt 库和插件，`xkb-*-fd-audit.log` 记录隔离进程的文件描述符。原装 Qt 的对照日志为 `board-editing-evidence.txt`。

## 交叉构建 QtBase

下列命令在开发主机运行，使用 CMake、Ninja、Python 3、patchelf 和 ADB。目录采用 SDK 当前布局，按本机实际位置设置 `rk` 与 `br`。Qt 6.12 的 WaylandClient 和平台插件属于 QtBase。启用 xkbcommon 会编译 QtGui 中的 `QXkbCommon`，也会改变 WaylandClient 的条件编译。需从同一份 QtBase 构建 QtGui、WaylandClient 与对应插件，再一起覆盖测试进程的搜索路径。

```bash
rk=/home/wyj/rk3568
br="$rk/workspace/atk_dlrk3568_linux6.1/buildroot/output/alientek_rk3568"
qa=$(mktemp -d /tmp/velora-weston-qa.XXXXXX)
export rk br qa
export LD_LIBRARY_PATH="$rk/qt-host/6.12.0/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cmake -G Ninja -S "$rk/src/qtbase-everywhere-src-6.12.0" -B "$qa/qtbase-xkb" \
  -DCMAKE_TOOLCHAIN_FILE="$rk/toolchains/rk3568-buildroot.cmake" \
  -DCMAKE_INSTALL_PREFIX=/usr/local/qt6.12.0 \
  -DCMAKE_STAGING_PREFIX="$qa/qt-stage-xkb" \
  -DCMAKE_BUILD_TYPE=Release -DQT_HOST_PATH="$rk/qt-host/6.12.0" \
  -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DINPUT_opengl=es2 \
  -DFEATURE_eglfs=ON -DFEATURE_eglfs_gbm=ON -DFEATURE_linuxfb=ON \
  -DFEATURE_xcb=OFF -DFEATURE_xkbcommon=ON -DFEATURE_openssl=ON \
  -DFEATURE_precompile_header=OFF
grep -F '#define QT_FEATURE_xkbcommon 1' "$qa/qtbase-xkb/src/gui/qtgui-config.h"
cmake --build "$qa/qtbase-xkb" \
  --target WaylandClient QWaylandIntegrationPlugin QWaylandXdgShellIntegrationPlugin \
  --parallel 8

qtbuild="$qa/qtbase-xkb"
payload="$qa/payload"
mkdir -p "$payload/qt-xkb/lib" "$payload/qt-xkb/plugins/platforms" \
  "$payload/qt-xkb/plugins/wayland-shell-integration"
for component in Core Gui DBus WaylandClient; do
    lib=libQt6$component.so.6
    cp "$qtbuild/lib/$lib.12.0" "$payload/qt-xkb/lib/"
    ln -s "$lib.12.0" "$payload/qt-xkb/lib/$lib"
done
cp "$qtbuild/plugins/platforms/libqwayland.so" "$payload/qt-xkb/plugins/platforms/"
cp "$qtbuild/plugins/wayland-shell-integration/libxdg-shell.so" \
  "$payload/qt-xkb/plugins/wayland-shell-integration/"
cp "$rk/qt-arm64/6.12.0/bin/qml" "$payload/"
readelf -d "$payload/qt-xkb/lib/libQt6WaylandClient.so.6.12.0" | grep libxkbcommon
```

板端启动命令会在私有目录重新建立 Qt 库的 SONAME 链接。

## 交叉构建无头 Weston

取板端 SDK 配套的 Weston 14.0.2 源码。在该 BSP 中，Pixman 的 EGL 分支会主动打开 `/dev/dri/card0`，即使指定 headless 和 Pixman。复制源码到临时目录，移除该测试构建中的 EGL、GLES2、GBM 探测，同时关闭 DRM backend。板端现有的 Weston、桌面 shell 和库无需替换。测试插件的空指针保护允许 Weston 在常规前端中以独立虚拟 seat 运行该模块。

```bash
python3 - <<'PY'
import ast
import configparser
import os
from pathlib import Path
import shutil
import subprocess

br = Path(os.environ["br"])
qa = Path(os.environ["qa"])
sdk_source = br / "build/weston-14.0.2"
src = qa / "weston-src"
shutil.copytree(sdk_source, src, ignore=shutil.ignore_patterns("build", ".git"))

meson_file = src / "meson.build"
text = meson_file.read_text()
for dependency in ("egl", "glesv2"):
    text = text.replace(f"dependency('{dependency}', required: false)",
                        "dependency('', required: false)")
text = text.replace("if dependency('gbm', required: false).found()", "if false")
meson_file.write_text(text)

test_plugin = src / "tests/weston-test.c"
text = test_plugin.read_text()
changes = [
    ("wl_list_for_each_safe(bp, tmp, &tsd->breakpoints.list, link)",
     "if (!tsd)\n\t\treturn;\n\n\twl_list_for_each_safe(bp, tmp, &tsd->breakpoints.list, link)"),
    ("bp = calloc(1, sizeof(*bp));",
     "if (!tsd)\n\t\treturn;\n\tbp = calloc(1, sizeof(*bp));"),
    ("assert(tsd->wl_client);\n\ttsd->wl_client = NULL;",
     "if (tsd) {\n\t\tassert(tsd->wl_client);\n\t\ttsd->wl_client = NULL;\n\t}"),
    ("assert(!tsd->wl_client);\n\ttsd->wl_client = client;",
     "if (tsd) {\n\t\tassert(!tsd->wl_client);\n\t\ttsd->wl_client = client;\n\t}"),
    ("\n\tdata->wl_client = NULL;", "\n\tif (data)\n\t\tdata->wl_client = NULL;"),
]
for before, after in changes:
    assert text.count(before) == 1, before
    text = text.replace(before, after)
test_plugin.write_text(text)

cfg = configparser.ConfigParser(interpolation=None)
cfg.read(sdk_source / "build/meson-private/cmd_line.txt")
options = dict(cfg["options"])
options.update({"backend-headless": "true", "backend-drm": "false",
                "backend-default": "headless", "renderer-gl": "false", "screenshare": "false"})
cross = ast.literal_eval(cfg["properties"]["cross_file"])[0]
tools = br / "host/bin"
env = dict(os.environ, PATH=f"{tools}:{os.environ['PATH']}")
build = qa / "weston-build"
subprocess.run([tools/"meson", "setup", build, src, "--cross-file", cross,
                *(f"-D{key}={value}" for key, value in options.items())], env=env, check=True)
targets = ["frontend/weston", "libweston/backend-headless/headless-backend.so",
           "tests/test-plugin.so", "desktop-shell/desktop-shell.so"]
subprocess.run([tools/"ninja", "-C", build, "-j", "4", *targets], env=env, check=True)
targets += ["frontend/libexec_weston.so.0.0.0", "libweston/libweston-14.so.0.0.2"]
for target in targets:
    output = qa / "payload" / Path(target).name
    shutil.copy2(build / target, output)
    subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", output], check=True)
PY
```

## 准备虚拟输入工具

在开发主机准备测试驱动。该工具只连接由 `WAYLAND_DISPLAY` 和 `XDG_RUNTIME_DIR` 指定的私有 socket，使用 Weston 测试模块导出的接口。两个参数用于单次点按，四个参数用于拖动。测试进程以 nobody 身份运行，降权程序只用于本次临时验证。

<details>
<summary>展开 AArch64 测试工具和生成命令</summary>

```bash
cat > "$qa/virtual-pointer.c" <<'C'
#include <linux/input-event-codes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include "weston-test-client.h"

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version)
{
    struct weston_test **test = data;
    if (strcmp(interface, weston_test_interface.name) == 0)
        *test = wl_registry_bind(registry, name, &weston_test_interface, 1);
}
static void remove_global(void *data, struct wl_registry *registry, uint32_t name) {}
static void pointer(void *data, struct weston_test *test, wl_fixed_t x, wl_fixed_t y) {}
static const struct wl_registry_listener registry_listener = {global, remove_global};
static const struct weston_test_listener test_listener = {pointer};
int main(int argc, char **argv)
{
    if (argc != 3 && argc != 5)
        return 2;
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        return 3;
    struct weston_test *test = NULL;
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &test);
    if (wl_display_roundtrip(display) < 0 || !test)
        return 4;
    weston_test_add_listener(test, &test_listener, NULL);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    weston_test_move_pointer(test, (uint64_t)now.tv_sec >> 32, now.tv_sec, now.tv_nsec,
                             atoi(argv[1]), atoi(argv[2]));
    if (wl_display_roundtrip(display) < 0)
        return 5;
    weston_test_send_button(test, (uint64_t)now.tv_sec >> 32, now.tv_sec, now.tv_nsec,
                            BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
    if (argc == 5) {
        int x = atoi(argv[1]), y = atoi(argv[2]);
        int end_x = atoi(argv[3]), end_y = atoi(argv[4]);
        const struct timespec pause = {.tv_nsec = 30000000};
        for (int part = 1; part <= 5; ++part) {
            nanosleep(&pause, NULL);
            clock_gettime(CLOCK_MONOTONIC, &now);
            weston_test_move_pointer(test, (uint64_t)now.tv_sec >> 32, now.tv_sec, now.tv_nsec,
                                     x + (end_x - x) * part / 5,
                                     y + (end_y - y) * part / 5);
            if (wl_display_roundtrip(display) < 0)
                return 7;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    weston_test_send_button(test, (uint64_t)now.tv_sec >> 32, now.tv_sec, now.tv_nsec,
                            BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
    int result = wl_display_roundtrip(display) < 0 ? 6 : 0;
    weston_test_destroy(test);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return result;
}
C
cat > "$qa/nobody.c" <<'C'
#include <grp.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (setgroups(0, NULL) < 0 || setgid(65534) < 0 || setuid(65534) < 0) {
        perror("drop privileges");
        return 3;
    }
    execvp(argv[1], argv + 1);
    perror("exec");
    return 4;
}
C
weston_source="$br/build/weston-14.0.2"
"$br/host/bin/wayland-scanner" client-header \
  "$weston_source/protocol/weston-test.xml" "$qa/weston-test-client.h"
cc="$br/host/bin/aarch64-buildroot-linux-gnu-gcc"
"$cc" -O2 "$qa/virtual-pointer.c" "$qa/weston-build/protocol/weston-test-protocol.c" \
  -lwayland-client -o "$payload/virtual-pointer"
"$cc" -O2 "$qa/nobody.c" -o "$payload/nobody"
```

</details>

## 启动板端独立会话

在主机的仓库根目录执行。生成只有测试标记的第二版 QML 客户端，并将构建产物复制到板端新建的临时路径。ADB 连接使用本机明确选定的板卡，多个设备时通过 `ANDROID_SERIAL` 指定序列号。

<details>
<summary>展开板端启动及自动验收脚本</summary>

```bash
cat > "$payload/Client-xkb.qml" <<'QML'
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    width: 440
    height: 156
    visibility: Window.Maximized
    title: pin ? "Velora PIN Client" : "Velora Text Client"
    readonly property bool pin: Qt.application.arguments.includes("pin")
    Component.onCompleted: console.log("CLIENT_READY")

    onActiveChanged: {
        if (active) {
            field.forceActiveFocus()
            Qt.inputMethod.show()
        }
    }

    TextField {
        id: field
        x: 16
        y: 25
        width: parent.width - 32
        placeholderText: window.pin ? "PIN probe" : "Cross application probe"
        focus: true
        echoMode: window.pin ? TextInput.Password : TextInput.Normal
        inputMethodHints: window.pin ? Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhHiddenText : Qt.ImhNone

        onActiveFocusChanged: if (activeFocus) Qt.inputMethod.show()
        onAccepted: console.log("TEXT_ACCEPTED")
        onCursorPositionChanged: if (!window.pin && text === "qw" && cursorPosition < 2)
            console.log("TEXT_CURSOR_MOVED")
        onTextChanged: {
            if (window.pin && text === "12" && displayText !== text)
                console.log("PIN_MASKED")
            if (window.pin && text === "1" && displayText !== text)
                console.log("PIN_ONE_MASKED")
            if (!window.pin && text === "q")
                console.log("TEXT_Q")
            if (!window.pin && text === "qwq")
                console.log("TEXT_RETURNED")
            if (!window.pin && text === "qw")
                console.log("TEXT_QW")
        }

        TapHandler {
            onTapped: {
                field.forceActiveFocus()
                Qt.inputMethod.show()
            }
        }
    }
}
QML
cat > "$payload/server.sh" <<'SH'
#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$base"
umask 077
unset DISPLAY XDG_SEAT QT_QPA_PLATFORM LD_LIBRARY_PATH
mkdir -p runtime data config cache
export XDG_RUNTIME_DIR="$base/runtime"
export XDG_DATA_HOME="$base/data"
export XDG_CONFIG_HOME="$base/config"
export XDG_CACHE_HOME="$base/cache"
export WAYLAND_DISPLAY=velora-qa
export WESTON_MODULE_MAP="headless-backend.so=$base/headless-backend.so;test-plugin.so=$base/test-plugin.so;desktop-shell.so=$base/desktop-shell.so"
ln -sf libexec_weston.so.0.0.0 libexec_weston.so.0
ln -sf libweston-14.so.0.0.2 libweston-14.so.0
cat > input-method <<'IM'
#!/bin/sh
export LD_LIBRARY_PATH=/usr/local/qt6.12.0/lib
export QT_PLUGIN_PATH=/usr/local/qt6.12.0/plugins
export QML_IMPORT_PATH=/usr/local/qt6.12.0/qml:/usr/share/veloraime/qml
export QT_QUICK_BACKEND=software QT_WAYLAND_CLIENT_BUFFER_INTEGRATION=none
exec /usr/bin/veloraime-weston
IM
chmod 700 input-method
printf '[core]\nshell=desktop\n[input-method]\npath=%s/input-method\n[shell]\nbackground-color=0xff1d2630\npanel-position=none\nlocking=false\n' "$base" > weston.ini
exec timeout 300 nice -n 10 "$base/weston" --backend=headless --renderer=pixman --width=800 --height=640 --idle-time=0 --socket="$WAYLAND_DISPLAY" --modules=test-plugin.so --config="$base/weston.ini" --log="$base/weston.log"
SH
cat > "$payload/clients.sh" <<'SH'
#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$base"
export XDG_RUNTIME_DIR="$base/runtime" XDG_CONFIG_HOME="$base/config" XDG_DATA_HOME="$base/data" XDG_CACHE_HOME="$base/cache" WAYLAND_DISPLAY=velora-qa
export LD_LIBRARY_PATH="$base/qt-xkb/lib:/usr/local/qt6.12.0/lib"
export QT_PLUGIN_PATH="$base/qt-xkb/plugins:/usr/local/qt6.12.0/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$base/qt-xkb/plugins/platforms"
export QML_IMPORT_PATH=/usr/local/qt6.12.0/qml
export QT_WAYLAND_CLIENT_BUFFER_INTEGRATION=none QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software QT_WAYLAND_TEXT_INPUT_PROTOCOL=zwp_text_input_v1
text=
pin=
finish() {
    if [ -n "$pin" ]; then kill "$pin" 2>/dev/null || true; fi
    if [ -n "$text" ]; then kill "$text" 2>/dev/null || true; fi
    wait
    tail -25 text.log
    if [ -f pin.log ]; then grep -E "CLIENT_READY|PIN_.*MASKED" pin.log || true; fi
}
trap finish EXIT
click() {
    ./virtual-pointer "$@"
    sleep .15
}
expect() {
    for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
        if [ "$(grep -c "$2" "$1.log" || true)" -ge "$3" ]; then return 0; fi
        sleep .2
    done
    echo "missing $1 $2 $3" >&2
    return 1
}
./qml Client-xkb.qml -- text > text.log 2>&1 &
text=$!
expect text CLIENT_READY 1
sleep .35
click 300 99
sleep 1.1
click 55 408
click 132 408
expect text TEXT_QW 1

./qml Client-xkb.qml -- pin > pin.log 2>&1 &
pin=$!
expect pin CLIENT_READY 1
sleep .35
click 300 100
sleep 1.1
click 145 411
click 400 411
expect pin PIN_MASKED 1
ls -l /proc/$text/fd /proc/$pin/fd > "$base/client-fd-audit.log"
grep -F "$base/qt-xkb" /proc/$text/maps > "$base/client-qt-maps.log"
click 666 592
expect pin PIN_ONE_MASKED 2
kill "$pin"
wait "$pin" 2>/dev/null || true
pin=
sleep .45
click 300 100
sleep 1.5
click 55 408
expect text TEXT_RETURNED 1
click 720 505
expect text TEXT_QW 2
click 430 554 360 554
expect text TEXT_CURSOR_MOVED 1
click 683 554
expect text TEXT_ACCEPTED 1
printf '%s\n' 'PASS: two Wayland clients, private PIN, and return to original field, Backspace, cursor movement, and Return.'
SH
```

</details>

```bash
board=$(adb shell -n 'mktemp -d /tmp/velora-crossboard.XXXXXX')
adb push "$payload" "$board/"
adb shell -n "cd '$board/payload/qt-xkb/lib' &&
  for component in Core Gui DBus WaylandClient; do
    ln -sf libQt6\${component}.so.6.12.0 libQt6\${component}.so.6
  done &&
  chown -R 65534:65534 '$board'"
adb shell -n "'$board/payload/nobody' sh '$board/payload/server.sh'" &
server_job=$!
for attempt in {1..30}; do
    adb shell -n "test -S '$board/payload/runtime/velora-qa'" && break
    sleep .2
done
adb shell -n "'$board/payload/nobody' sh '$board/payload/clients.sh'"
adb pull "$board/payload/client-fd-audit.log" "$qa/"
adb pull "$board/payload/client-qt-maps.log" "$qa/"
adb pull "$board/payload/weston.log" "$qa/"
weston_pid=$(adb shell -n "ps | awk '\$3 == \"$board/payload/weston\" {print \$1; exit}'")
if [ -n "$weston_pid" ]; then
    adb shell -n "test \"\$(readlink /proc/$weston_pid/exe)\" = '$board/payload/weston' &&
      kill '$weston_pid'"
fi
wait "$server_job" || true
```

也可在终端分别运行服务和验收脚本，由服务的 300 秒超时结束私有 Weston。成功时客户端输出 `TEXT_RETURNED`、第二个 `TEXT_QW`、`TEXT_CURSOR_MOVED`、`TEXT_ACCEPTED`、`PIN_MASKED` 及前后两次 `PIN_ONE_MASKED`。检查映射以确认加载了私有 QtGui、WaylandClient、平台和 xdg-shell 插件。检查私有进程的 `/proc/PID/fd`，确认没有指向 `/dev/dri`、`/dev/input`、`/dev/mali*`、`/dev/dma_heap` 或 seatd 的描述符。

## 可选的 SDK 集成

将 xkbcommon 开启项放入目标 QtBase 配方，使用 Buildroot sysroot 已提供的 `libxkbcommon.so.0`，由同一次构建生成 QtGui、WaylandClient、Wayland 平台和 shell 插件。正式 SDK 发布时，把 QtBase 及其私有依赖作为统一版本构建和打包，并复跑宿主输入测试及目标机跨进程测试。单独替换平台插件无法为当前 QtGui 增加 `QXkbCommon`。临时叠加同为 6.12.0 的库已通过本页验证，正式部署仍应按完整 SDK 流程生成一致的目标 Qt 运行时。无头测试模块仅用于临时验收。
