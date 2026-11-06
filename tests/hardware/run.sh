#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD=$(mktemp -d "${TMPDIR:-/tmp}/note4c-hardware-test.XXXXXX")
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wno-unused-variable -I"$HERE/stubs" "$HERE/test.cpp" -o "$BUILD/hardware-tests"
"$BUILD/hardware-tests"
