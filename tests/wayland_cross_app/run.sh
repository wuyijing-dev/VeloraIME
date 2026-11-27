#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 || ! -x "$1/bin/qml" || ! -x "$2" ]]; then
    echo "Usage: $0 QT_HOST_PREFIX VELORAIME_WESTON_BINARY" >&2
    exit 2
fi
qt=$(realpath "$1")
keyboard=$(realpath "$2")
scripts=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
work=$(mktemp -d)
chmod 700 "$work"
export XDG_RUNTIME_DIR="$work" WAYLAND_DISPLAY=velora-qa

jobs_to_stop=()
cleanup() {
    local result=$?
    if ((result)); then
        if [[ -n ${VELORA_WAYLAND_FAILURE_PNG:-} && -n ${DISPLAY:-} ]]; then
            ffmpeg -hide_banner -loglevel error -y -f x11grab -video_size 800x640 \
                -i "$DISPLAY" -frames:v 1 "$VELORA_WAYLAND_FAILURE_PNG" || true
        fi
        for log in "$work"/*.log; do
            if [[ -f $log ]]; then tail -20 "$log" >&2; fi
        done
    fi
    for job in "${jobs_to_stop[@]}"; do kill "$job" 2>/dev/null || true; done
    wait 2>/dev/null || true
    rm -rf -- "$work"
}
trap cleanup EXIT

Xvfb -displayfd 3 -screen 0 1000x780x24 -nolisten tcp -ac \
    3>"$work/display" >"$work/xvfb.log" 2>&1 &
jobs_to_stop+=("$!")
for attempt in {1..30}; do
    [[ -s $work/display ]] && break
    sleep .1
done
export DISPLAY=":$(cat "$work/display")"
printf '#!/usr/bin/env bash\nLD_LIBRARY_PATH=%q exec %q\n' "$qt/lib" "$keyboard" >"$work/input-method"
chmod 700 "$work/input-method"
printf '[core]\nshell=desktop\n[input-method]\npath=%s/input-method\n' "$work" >"$work/weston.ini"
env -u LD_LIBRARY_PATH weston --backend=x11 --renderer=pixman --width=800 --height=640 \
    --socket="$WAYLAND_DISPLAY" --config="$work/weston.ini" >"$work/weston.log" 2>&1 &
jobs_to_stop+=("$!")
for attempt in {1..35}; do
    [[ -S $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY ]] && break
    sleep .1
done
test -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"

run_client() {
    env LD_LIBRARY_PATH="$qt/lib" QT_QPA_PLATFORM=wayland QT_QUICK_BACKEND=software \
        QT_WAYLAND_TEXT_INPUT_PROTOCOL=zwp_text_input_v1 \
        "$qt/bin/qml" "$scripts/Client.qml" -- "$1" >"$work/$1.log" 2>&1 &
    client=$!
    jobs_to_stop+=("$client")
}

click() {
    env LD_LIBRARY_PATH="${VELORA_X11_LIB_PATH:-}" xdotool mousemove "$1" "$2" click 1
}

expect() {
    for attempt in {1..25}; do
        [[ $(grep -c "$2" "$work/$1.log" || true) -ge $3 ]] && return 0
        sleep .12
    done
    return 1
}

x11_window=$(sed -n 's/.*window id \([0-9][0-9]*\).*/\1/p' "$work/weston.log" | tail -1)
test -n "$x11_window"
env LD_LIBRARY_PATH="${VELORA_X11_LIB_PATH:-}" xdotool windowfocus "$x11_window"

run_client text
expect text CLIENT_READY 1
sleep .25
click 300 99
sleep .45
click 55 408
click 132 408
expect text TEXT_QW 1

run_client pin
pin_client=$client
expect pin CLIENT_READY 1
sleep .25
click 300 100
sleep .45
click 145 411
click 400 411
expect pin PIN_MASKED 1
kill "$pin_client"

sleep .45
click 300 100
sleep .35
click 719 504
expect text 'TEXT_Q$' 2
echo "PASS: two Wayland clients, private PIN, and return to original field."
