#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 || ( $# == 3 && "$3" != --exclusive ) ||
      ! -d "$1/usr/share/veloraime" || ! -x "$1/usr/bin/veloraime-demo" ]]; then
    echo "Usage: $0 STAGED_INSTALLATION ADB_SERIAL [--exclusive]" >&2
    exit 2
fi

stage=$(realpath "$1")
serial=$2
exclusive=${3:-}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
velora_adb=(adb -P "${ADB_SERVER_PORT:-5037}" -s "$serial")
velora_archive=$(mktemp)
velora_remote=$("${velora_adb[@]}" shell 'mktemp -d /tmp/veloraime-install.XXXXXX')

if [[ ! "$velora_remote" =~ ^/tmp/veloraime-install\.[a-zA-Z0-9]+$ ]]; then
    rm -f "$velora_archive"
    echo "Device did not allocate a valid staging path." >&2
    exit 1
fi
trap 'rm -f "$velora_archive"; "${velora_adb[@]}" shell "rm -rf -- $velora_remote" >/dev/null 2>&1 || true' EXIT

device=$("${velora_adb[@]}" shell 'uname -m && tr "\000" "\n" </proc/device-tree/model')
if [[ "$device" != $'aarch64\n'*RK3568* ]]; then
    echo "VeloraIME firmware deployment expects an AArch64 RK3568." >&2
    exit 1
fi

tar -cf "$velora_archive" -C "$stage" usr
"${velora_adb[@]}" push "$velora_archive" "$velora_remote/payload.tar"
"${velora_adb[@]}" shell "cd $velora_remote && sh -s -- '$exclusive'" <<'REMOTE'
set -eu
tar -xf payload.tar
find usr -type f | while IFS= read -r file; do
    case "$file" in
        usr/bin/veloraime-demo|usr/bin/veloraime-weston|usr/share/veloraime/*|\
        usr/share/tegaki/models/zinnia/handwriting-zh_CN.model|usr/lib/libzinnia.so.0|\
        usr/share/fonts/NotoColorEmoji.ttf) ;;
        *) echo "Unexpected file in deployment: $file" >&2; exit 1 ;;
    esac
    mkdir -p "/${file%/*}"
    temporary=$(mktemp "/${file%/*}/.veloraime-XXXXXX")
    cp -p "$file" "$temporary"
    chown 0:0 "$temporary"
    cmp -s "$file" "$temporary"
    mv -f "$temporary" "/$file"
done
sync
. /etc/profile.d/qt6.sh
umask 077
export XDG_DATA_HOME="$PWD/test-data" XDG_CONFIG_HOME="$PWD/test-config"
if [ "${1:-}" = --exclusive ]; then
    /usr/bin/veloraime-demo --smoke-test
else
    /usr/bin/veloraime-demo --verify-input
fi
REMOTE

if [[ $exclusive == --exclusive ]]; then
    "${velora_adb[@]}" push "$script_dir/S95veloraime" "$velora_remote/S95veloraime"
    "${velora_adb[@]}" push "$script_dir/exclusive_display.sh" "$velora_remote/exclusive_display.sh"
    "${velora_adb[@]}" shell "set -eu
        install -o root -g root -m 0755 $velora_remote/S95veloraime /etc/init.d/S95veloraime
        install -o root -g root -m 0755 $velora_remote/exclusive_display.sh /usr/sbin/veloraime-display
        /usr/sbin/veloraime-display enable
        sync"
fi
