#!/usr/bin/env bash
# Build the debug preset (WSL) and flash over SWD with J-Link Commander
# (Windows side, through cmd.exe interop).
#   tools/flash.sh            build + flash
#   tools/flash.sh -nobuild   flash only
# Run from any directory: paths are resolved from this file's location.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CMD=/mnt/c/Windows/System32/cmd.exe
JLINK='C:\mengrendufu\software_ins\JLink\JLink.exe'
OPTS='-device STM32F103ZE -if SWD -speed 4000 -autoconnect 1'
OPTS="$OPTS -ExitOnError 1 -NoGui 1"

if [[ ! -f /mnt/c/mengrendufu/software_ins/JLink/JLink.exe ]]; then
    echo "[flash] J-Link Commander not found: $JLINK"
    exit 1
fi
if [[ ! -f "$ROOT/tools/flash.jlink" ]]; then
    echo "[flash] command file not found: $ROOT/tools/flash.jlink"
    exit 1
fi

if [[ "${1:-}" != "-nobuild" ]]; then
    echo "[flash] build both images..."
    (cd "$ROOT" && cmake --preset debug && cmake --build --preset build-debug)
fi

echo "[flash] artifact:"
ls -l "$ROOT/build/debug/bootloader.hex" "$ROOT/build/debug/sm_blinky.hex"

CFG="$(wslpath -w "$ROOT/tools/flash.jlink")"
echo "[flash] J-Link Commander over SWD..."
# cmd.exe strips the first and last quote of its command line, so a quoted
# -CommandFile value arrives at J-Link with the quotes inside the filename and
# the open fails ("Could not open ... '\\wsl...'"). The path goes out bare,
# which cmd.exe carries intact - but only while it has no spaces, hence the
# guard. Measured both ways: bare works, quoted does not.
if [[ "$CFG" == *" "* ]]; then
    echo "[flash] path has a space, cmd.exe cannot carry it: $CFG"
    echo "[flash] move the repo to a space-free path, or symlink one"
    exit 1
fi
if ! "$CMD" /c "$JLINK $OPTS -CommandFile $CFG"; then
    echo "[flash] FAILED"
    exit 1
fi
echo "[flash] done."
