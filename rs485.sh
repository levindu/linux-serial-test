#!/bin/sh

die() {
    echo "error: $*"
    exit 1
} >&2

usage() {
    echo "usage: $0 {send|recv} <dev> <baud>"
}

v() {
    echo ">>> $*"
    "$@"
}

arch="$(uname -m)"
bin=./linux-serial-test."$arch"
test -x "$bin" || die "Test program '$bin' not found, or arch not supported."

dev="$2"
test -c "$dev" || die "Serial dev '$dev' not found"

case "$1" in
    r|recv)
        args="--no-tx"
        ;;
    s|send)
        args="--no-rx"
        ;;
    master)
        args="--rx-timeout 50 --tx-delay 1200"
        ;;
    slave)
        args="-K --rx-timeout 50 --tx-delay 50"
        ;;
    *)
        usage
        ;;
esac

baud="${3:-9600}"

extra_args=
if test "${4:-}" = "-v"; then
    extra_args="-R ascii"
fi

sudo chmod 666 "$dev" || :

v "$bin" -p "$dev" -b "$baud" -C -s -e $args $extra_args
