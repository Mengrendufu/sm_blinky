#!/bin/sh
# Configure the project and leave behind a compile database an editor can use.
#
# cmake --preset debug writes build/debug/compile_commands.json, but CMake
# leaves the compiler's own (implicit) include directories out of it, so clangd
# parses every file with clang's builtins and reports a wall of missing-header
# errors. clangd honours flags that arrive inside the database and drops the
# ones a .clangd file adds, so this script puts them in: it asks the compiler
# which directories it searches and appends them to every entry. Idempotent,
# and it re-asks on every run, so a toolchain upgrade needs no edit here.
#
# Nothing else: no build, no checks.
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
patched = 0
for entry in entries:
    if "command" in entry:
        if dirs[0] not in entry["command"]:
            entry["command"] += " " + " ".join("-isystem " + d for d in dirs)
            patched += 1
    else:
        args = entry.setdefault("arguments", [])
        if not any(a == "-isystem" and args[i + 1] == dirs[0]
                   for i, a in enumerate(args[:-1])):
            for d in dirs:
                args += ["-isystem", d]
            patched += 1
json.dump(entries, open(path, "w", encoding="utf-8"), indent=1)
print("database: %d entries, %d patched" % (len(entries), patched))
PY
