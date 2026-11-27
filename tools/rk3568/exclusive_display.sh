#!/bin/sh
set -eu

state=/etc/veloraime
archive=$state/paused-init
service=/etc/init.d/S95veloraime
competing='S10lv_demo S49weston S50systemui S70qt6-demo S99z-luma S99z-rk3568-voice'

[ "$(id -u)" = 0 ] || exit 1
[ -x "$service" ] || exit 1
umask 077
mkdir -p "$archive"
chmod 0700 "$state" "$archive"

case "${1:-}" in
    enable)
        for name in $competing; do
            original=/etc/init.d/$name
            [ -f "$original" ] || continue
            if [ -e "$archive/$name" ]; then
                printf 'Preserved launcher already exists for %s.\n' "$name" >&2
                exit 1
            fi
        done
        for name in $competing; do
            original=/etc/init.d/$name
            [ -f "$original" ] || continue
            # S80weston owns the active compositor. Preserve its running socket.
            if [ "$name" != S49weston ]; then
                "$original" stop
            fi
            mv "$original" "$archive/$name"
        done
        rm -f "$state/disabled"
        "$service" restart
        ;;
    restore)
        for name in $competing; do
            [ -f "$archive/$name" ] || continue
            if [ -e "/etc/init.d/$name" ]; then
                printf 'Cannot restore over existing launcher %s.\n' "$name" >&2
                exit 1
            fi
        done
        : >"$state/disabled"
        "$service" stop
        for name in $competing; do
            [ ! -f "$archive/$name" ] || mv "$archive/$name" "/etc/init.d/$name"
        done
        ;;
    *)
        printf 'Usage: %s {enable|restore}\n' "$0" >&2
        exit 2
        ;;
esac
