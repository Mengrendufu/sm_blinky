#!/bin/sh
# Configure the project and leave behind a compile database that describes
# exactly what this project compiles - one entry per source, no other target's
# flags, and the compiler's own include directories in place.
#
# Two things stand between cmake --preset debug and such a database:
#
#   * CMake exports all targets. Keep App, bootloader and LVGL; prefer App's
#     flags for shared sources so each file has one deterministic entry.
#
#   * CMake leaves the compiler's implicit include directories out of the
#     commands. clangd then parses with its own builtins and reports missing
#     headers (newlib's sys/types.h among them); flags arriving inside the
#     database are honoured, while the ones a .clangd file adds are dropped.
#     The directories are asked of the compiler here, so a toolchain upgrade
#     needs no edit.
#
# Nothing else: no build, no checks. Idempotent - it starts from what cmake
# wrote, not from what it left behind last time.
#
# Usage: tools/ide_db.sh [project-dir]        (default: the current directory)
set -eu

root="${1:-$PWD}"
cd "$root"
test -f CMakePresets.json || { echo "no CMakePresets.json in $root" >&2; exit 1; }

cmake --preset debug

python3 - build/debug/compile_commands.json <<'PY'
import json
import subprocess
import sys

path = sys.argv[1]

probe = subprocess.run(["arm-none-eabi-gcc", "-E", "-v", "-"], input="",
                       capture_output=True, text=True)
dirs, inside = [], False
for line in probe.stderr.splitlines():
    if "search starts here:" in line:
        inside = True
    elif "End of search list" in line:
        inside = False
    elif inside:
        dirs.append(line.strip())
if not dirs:
    raise SystemExit("the compiler reported no include directories")

entries = json.load(open(path, encoding="utf-8"))
kept, dropped = {}, 0
for entry in entries:
    command = entry.get("command") or " ".join(entry.get("arguments", []))
    if "sm_blinky.elf.dir" in command:
        rank = 3
    elif "bootloader.elf.dir" in command:
        rank = 2
    elif "lvgl.dir" in command:
        rank = 1
    else:
        dropped += 1        # another target: not this project's business
        continue
    previous = kept.get(entry["file"])
    if previous is None or rank > previous[0]:
        kept[entry["file"]] = (rank, entry)

out = []
for rank, entry in kept.values():
    if "command" in entry:
        if dirs[0] not in entry["command"]:
            entry["command"] += " " + " ".join("-isystem " + d for d in dirs)
    else:
        args = entry.setdefault("arguments", [])
        if not any(a == "-isystem" and args[i + 1] == dirs[0]
                   for i, a in enumerate(args[:-1])):
            for d in dirs:
                args += ["-isystem", d]
    out.append(entry)

json.dump(out, open(path, "w", encoding="utf-8"), indent=1)
print("database: %d entries kept (%d from other targets dropped)"
      % (len(out), dropped))
PY
