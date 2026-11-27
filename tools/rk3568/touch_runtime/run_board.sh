#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 || ! "$2" =~ ^(direct|pinyin|mixed|pin)$ ]]; then
    echo "Usage: $0 ADB_SERIAL {direct|pinyin|mixed|pin}" >&2
    exit 2
fi

adb_command=(adb -P "${ADB_SERVER_PORT:-5037}" -s "$1")
case_name=$2
touch_reader=${VELORA_EVDEV_PROBE:-build/evdev-touch-probe}
runtime=${VELORA_TOUCH_RUNTIME:-build/touch-runtime-arm/velora-touch-runtime}
remote=$("${adb_command[@]}" shell 'mktemp -d /tmp/velora-touch.XXXXXX')
if [[ ! $remote =~ ^/tmp/velora-touch\.[a-zA-Z0-9]+$ ]]; then
    echo "Invalid test staging directory on board" >&2
    exit 1
fi

"${adb_command[@]}" push "$touch_reader" "$remote/evdev"
"${adb_command[@]}" push "$runtime" "$remote/runtime"
"${adb_command[@]}" shell "cd $remote && sh -s -- '$case_name'" <<'REMOTE'
set -eu
device=/dev/input/event2
service=/etc/init.d/S95veloraime
./evdev "$device" info

scenario=$1
repeat=1
case "$scenario" in
    direct) sequence="q w backspace"; expected=q; target=3 ;;
    pinyin)
        sequence="n i h a o space"
        repeat=44
        expected=
        for iteration in $(seq 1 "$repeat"); do expected="$expected你好"; done
        target=264
        ;;
    mixed)
        scenario=pinyin
        sequence="z h o n g g u o space s h a n g x backspace h a i space j i n t i a n d e t i a n q i h e n h a o space"
        repeat=9
        expected=
        for iteration in $(seq 1 "$repeat"); do expected="$expected中国上海今天的天气很好"; done
        target=$(($(printf '%s\n' "$sequence" | wc -w) * repeat))
        ;;
    pin) sequence="1 2 3"; expected=123; target=3 ;;
esac

# Count bytes without retaining touch records from the live application.
if [ "$(timeout 1 cat "$device" 2>/dev/null | wc -c)" -ne 0 ]; then
    echo "Another touch is present on the display. Test left the application running." >&2
    exit 3
fi

probe=
restore() {
    if [ -n "$probe" ]; then
        kill "$probe" 2>/dev/null || true
    fi
    "$service" start
}
trap restore EXIT
"$service" stop
. /etc/profile.d/qt6.sh
export XDG_CONFIG_HOME="$PWD" XDG_DATA_HOME="$PWD" XDG_CACHE_HOME="$PWD"
export QT_SCALE_FACTOR=2.608695652 QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough
./runtime /usr/share/veloraime/qml "$scenario" "$expected" "$target" >probe.log 2>&1 &
probe=$!
for attempt in $(seq 1 55); do
    grep -q '^FOCUS 1' probe.log && break
    sleep 0.1
done
if ! grep -q '^FOCUS 1' probe.log; then
    cat probe.log >&2
    exit 1
fi

{
    for iteration in $(seq 1 "$repeat"); do
        for key in $sequence; do
            coordinates=$(awk -v key="$key" '$1 == "KEY" && $2 == key {print $3, $4}' probe.log)
            test -n "$coordinates"
            printf '%s\n' "$coordinates"
        done
    done
} >coordinates
./evdev virtual <coordinates

result=0
wait "$probe" || result=$?
probe=
cat probe.log
exit "$result"
REMOTE
