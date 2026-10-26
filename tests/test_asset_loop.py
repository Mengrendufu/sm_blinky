#!/usr/bin/env python3
"""Host-only AST2 integration; artifacts stay in a fresh /tmp directory.

Run: python3 tests/test_asset_loop.py
Uses production bootloader/loader and packer; mocks UART, NOR and scheduler.
It does not build firmware, access a probe or write board flash.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from asset_pack import build_pack, SLOT_BYTES, SECTOR  # noqa: E402


def main():
    work = Path(tempfile.mkdtemp(prefix="asset-loop-v2-"))
    includes = ["tests/asset_support", "shared", "application",
                "bsp/devices", "ports/common_c",
                "3rd_party/common_c/include"]
    args = ["gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
            "-Werror", "-Wno-int-to-pointer-cast", "-ffunction-sections",
            "-fdata-sections", "-fsanitize=address,undefined", "-fno-pie",
            "-no-pie", "-Wl,--gc-sections"]
    args += [f"-I{ROOT / path}" for path in includes]
    args += [str(ROOT / "tests/test_asset_loop.c"),
             str(ROOT / "3rd_party/common_c/src/crc/crc32.c"),
             "-o", str(work / "test")]
    subprocess.run(args, check=True, cwd=ROOT)
    for count in (1, 2, 3):
        payload = b"".join(bytes([slot + 1]) * SLOT_BYTES
                           for slot in range(count))
        package = build_pack(payload)
        assert package[SECTOR:] == payload
        assert struct.unpack_from(">I", package, 64)[0] == zlib.crc32(
            package[:64])
        for slot in range(count):
            entry = struct.unpack_from(">4I", package, 16 + slot * 16)
            assert entry == (slot, slot * SLOT_BYTES, SLOT_BYTES,
                             zlib.crc32(payload[slot * SLOT_BYTES:
                                                (slot + 1) * SLOT_BYTES]))
        fixture = work / f"pack-{count}.bin"
        fixture.write_bytes(package)
        modes = range(17) if count == 3 else [0]
        for mode in modes:
            subprocess.run([str(work / "test"), str(fixture), str(mode)],
                           check=True)
    for bad in (b"", b"short", bytes(SLOT_BYTES + 1), bytes(SLOT_BYTES * 4)):
        try:
            build_pack(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid raw payload accepted")
    print(f"PASS 19 integration cases + packer bounds; artifacts: {work}")


if __name__ == "__main__":
    main()
