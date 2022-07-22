#!/bin/sh

die() {
    echo "error: $*"
    exit 1
} >&2

usage() {
    echo "usage: $0 <dev> <baud> [-v]"
}

v() {
    echo ">>> $*"
    "$@"
}

arch="$(uname -m)"
bin=./linux-serial-test."$arch"
test -x "$bin" || die "Test program '$bin' not found, or arch not supported."

dev="$1"
test -c "$dev" || die "Serial dev '$dev' not found"

baud="${2:-115200}"

extra_args=
if test "${3:-}" = "-v"; then
    extra_args="-R ascii"
fi

sudo chmod 666 "$dev" || :

v "$bin" -p "$dev" -b "$baud" -C -s -e $args $extra_args
