#!/usr/bin/env python3
"""Test retained-record validation and production startup reporting."""
from pathlib import Path
import re
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
source = (ROOT / "app/bsp/bsp.c").read_text()


def function(name, text=source):
    pattern = (r"^(?:__attribute__\(\(used\)\) )?"
               r"\w+ " + name + r"\([^;]*?\)\s*\{")
    match = re.search(pattern, text, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]


work = Path(tempfile.mkdtemp(prefix="fault-record-test-"))
(work / "production.h").write_text(
    function("BSP_dbgBootReport"))
subprocess.run([
    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
    "-I", str(ROOT / "project_common/fault"), "-I", str(work),
    str(HERE / "test_fault_record.c"), "-o", str(work / "test"),
], check=True)
subprocess.run([str(work / "test"), *sys.argv[1:]], check=True)
print(f"Host artifacts: {work}")
