#!/usr/bin/env python3
"""Test the real bootloader journal and reporting with NOR/Flash models."""
from pathlib import Path
import ast
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
work = Path(tempfile.mkdtemp(prefix="fault-log-test-"))
command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
           "-fsanitize=address,undefined", "-fno-pie", "-no-pie"]
for folder in (HERE / "ports", ROOT / "project_common/fault",
               ROOT / "project_common/startup",
               ROOT / "bootloader/diagnostics", ROOT / "bootloader/link",
               ROOT / "project_common/link"):
    command += ["-I", str(folder)]
command += [str(HERE / "test_fault_log.c"),
            str(ROOT / "bootloader/diagnostics/bl_fault_log.c"),
            str(ROOT / "bootloader/diagnostics/bl_fault_report.c"),
            str(ROOT / "bootloader/link/bl_link_tx.c"),
            str(ROOT / "project_common/link/hdlc_frame.c"),
            "-Wl,--defsym,_sfault=0x2000FF60", "-o", str(work / "test")]
subprocess.run(command, check=True)
subprocess.run([str(work / "test"), str(work)], check=True)
# Extract the actual wire parser without requiring a serial-port package.
source = (ROOT / "tools/trace_read.py").read_text()
module = ast.parse(source)
parser = next(node for node in module.body
              if isinstance(node, ast.FunctionDef) and node.name == "decode")
namespace = dict(ESC=0x7D, MARK=0x7E, DEFAULT_MAX_PAYLOAD=255,
                 HUNT=0, SEQ=1, RECID=2, LEN=3, PAYLOAD=4, CHK=5)
exec(compile(ast.Module(body=[parser], type_ignores=[]), "parser", "exec"),
     namespace)
for name, result in (("saved", 1), ("duplicate", 2), ("replay", 0),
                     ("failed", 3)):
    stats = {}
    frames = namespace["decode"]((work / f"{name}.bin").read_bytes(),
                                stats=stats)
    assert stats["errors"] == 0, (name, stats)
    assert [frame[1] for frame in frames] == [1, 2]
    assert all(len(frame[2]) == 64 for frame in frames)
    assert (frames[1][0] - frames[0][0]) & 255 == 1
    values = struct.unpack(">32I", frames[0][2] + frames[1][2])
    assert values[:2] == (result, 0 if result == 3 else 1)
    expected = [0] * 30
    expected[:5] = [0x08004321, 0x08005679, 3, 0x10000, 0x40000000]
    expected[6:8] = [2 if name == "failed" else 1, 0x7E7D7E7D]
    expected[22] = 0xFFFFFFFD
    expected[29] = 1
    assert values[2:] == tuple(expected), (name, values)

stats = {}
frames = namespace["decode"]((work / "invalid.bin").read_bytes(), stats=stats)
assert stats["errors"] == 0 and [f[1] for f in frames] == [113]
print("PASS: existing HDLC parser, all register words, escaping and statuses")
print(f"Host artifacts: {work}")
