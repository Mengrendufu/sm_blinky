#!/usr/bin/env python3
"""Exercise the production LCD boot algorithm against a host panel model."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
source = (ROOT / "app/bsp/devices/bsp_lcd.c").read_text()
functions = [
    "BSP_lcdWaitSince_", "BSP_lcdReadGram_",
    "lcdSentinelX_", "lcdSentinelColour_",
    "BSP_lcdWritePixel_", "BSP_lcdSentinelWrite_",
    "BSP_lcdSentinelAll_", "BSP_lcdWritable_", "BSP_lcdInit",
]


def extract(name):
    pattern = r"^(?:static )?\w+ " + name + r"\([^;]*?\) \{"
    match = re.search(pattern, source, re.M)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


work = Path(tempfile.mkdtemp(prefix="lcd-boot-test-"))
macros = "\n".join(re.findall(r"^#define LCD_.*", source, re.M))
state = re.search(r"static struct \{.*?\} lcd_dbgBoot_;", source, re.S)
assert state
(work / "production.h").write_text(
    macros + "\n" + state[0] + "\n"
    + "static bool lcd_dbgBootDispOn_;\n"
    + "static uint16_t lcd_dbgBootId_;\n"
    + "\n".join(extract(name) for name in functions))
subprocess.run([
    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
    "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
    "-I", str(work), str(HERE / "test_lcd_boot.c"),
    "-o", str(work / "test"),
], check=True)
subprocess.run([str(work / "test")], check=True)
print(f"Host artifacts: {work}")
