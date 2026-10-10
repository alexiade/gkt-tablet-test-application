#!/bin/bash
# Builds both front ends with MSYS2's CLANG64 toolchain into ./build.
# Usually run through scripts/build.ps1; extra arguments go to meson setup.
export MSYSTEM=CLANG64
source /etc/profile || true    # not set -e safe
set -e
cd "$(dirname "$0")/.."
if [ ! -f build/build.ninja ]; then
    meson setup build "$@"
elif [ $# -gt 0 ]; then
    meson configure build "$@"
fi
ninja -C build
