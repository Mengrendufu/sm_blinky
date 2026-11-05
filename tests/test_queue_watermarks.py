#!/usr/bin/env python3
"""Host-only tests of the real SST port and Timer queue trace hooks."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
work = Path(tempfile.mkdtemp(prefix="queue-watermarks-"))
command = [
    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
    "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
]
for folder in [
    "tests/ui_components/ports", "app/ports/static_pool",
    "3rd_party/sm_sst/include", "3rd_party/sm_hsm/include",
    "3rd_party/common_c/include",
]:
    command += ["-I", str(ROOT / folder)]
command += [
    str(ROOT / "tests/test_queue_watermarks.c"), "-o", str(work / "test"),
]
subprocess.run(command, check=True)
result = subprocess.run([str(work / "test")], check=True,
                        capture_output=True, text=True)
(work / "result.txt").write_text(result.stdout + result.stderr)
print(result.stdout, end="")
print(f"Report: {work / 'result.txt'}")
