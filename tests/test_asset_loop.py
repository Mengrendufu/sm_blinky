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
BOOT = ROOT / "bootloader"
sys.path.insert(0, str(ROOT / "tools"))
from asset_pack import build_pack, SLOT_BYTES, SECTOR  # noqa: E402
from hdlc_link import AssetLink, encode  # noqa: E402


def main():
    work = Path(tempfile.mkdtemp(prefix="asset-loop-v2-"))
    includes = ["tests/asset_support", "project_common/assets",
                "project_common/flash", "project_common/fault",
                "project_common/startup", "project_common/link",
                "bootloader/link", "3rd_party/sm_hsm/include",
                "app/application",
                "app/bsp/devices", "app/ports/common_c",
                "3rd_party/common_c/include"]
    args = ["gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
            "-Werror", "-Wno-int-to-pointer-cast", "-ffunction-sections",
            "-fdata-sections", "-fsanitize=address,undefined", "-fno-pie",
            "-no-pie", "-Wl,--gc-sections"]
    args += [f"-I{ROOT / path}" for path in includes]
    args += [f"-I{BOOT / 'diagnostics'}"]
    args += [f'-DBOOTLOADER_SOURCE="{BOOT / "application/bl_boot.c"}"']
    args += [str(ROOT / "project_common/link/hdlc_parser.c"),
             str(ROOT / "project_common/link/hdlc_frame.c"),
             str(ROOT / "bootloader/link/bl_link_tx.c"),
             str(ROOT / "3rd_party/sm_hsm/src/sm_hsm.c"),
             str(ROOT / "tests/test_asset_loop.c"),
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
        modes = range(20) if count == 3 else [0]
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
    # Python controls/raw data -> actual C transfer HSM -> Python replies.
    class Serial:
        def __init__(self):
            self.writes, self.incoming, self.timeout = [], bytearray(), 0.05

        def write(self, data):
            self.writes.append(bytes(data))
            return len(data)

        def read(self, size):
            count = min(size, 13, len(self.incoming))
            data = bytes(self.incoming[:count])
            del self.incoming[:count]
            return data

    payload = bytearray(bytes([0x7E, 0x7D, 0x55, 0x4E])
                        * (3 * SLOT_BYTES // 4))
    embedded = struct.pack(">BI", 2, SECTOR + len(payload))
    embedded = encode(0, 0xF0, b"\xAA" + embedded
                      + struct.pack(">I", zlib.crc32(embedded)))
    payload[100:100 + len(embedded)] = embedded
    pack = build_pack(payload)
    fixture = work / "pack-mixed.bin"
    fixture.write_bytes(pack)
    core = struct.pack(">BI", 2, len(pack))
    packets = [(0xAA, 0, b"\xAA" + core
                + struct.pack(">I", zlib.crc32(core)), b"")]
    for idx, off in enumerate(range(0, len(pack), 4096)):
        block = pack[off:off + 4096]
        packets.append((0xB1, idx, struct.pack(
            ">BHHH", 0xB1, idx, len(block), zlib.crc32(block) & 0xFFFF)
            , block))
    packets.append((0xC1, 0, b"\xC1" + struct.pack(
        ">II", len(pack), zlib.crc32(pack)), b""))
    for drop in (False, True):
        serial = Serial()
        link = AssetLink(serial)
        wire = bytearray()
        log = encode(1, 116, b"\x00\x55\x4e\x7d\x7e")
        gap = 0
        for kind, idx, control, data in packets:
            serial.writes.clear()
            link.send(control, data)
            transaction = serial.writes[0]
            if drop and kind == 0xB1 and idx == 0:
                bad = bytearray(encode(88, 0xF0, control))
                bad[-2] ^= 1
                wire += bad + data
                gap = len(wire)
            wire += log + transaction
        input_file, output_file = work / "wire.bin", work / "reply.bin"
        input_file.write_bytes(wire)
        command = [str(work / "test"), str(fixture), "0",
                   str(input_file), str(output_file)]
        if drop:
            command.append(str(gap))
        subprocess.run(command, check=True)
        serial.incoming = bytearray(output_file.read_bytes())
        logs = []
        receiver = AssetLink(serial, trace=lambda *args: logs.append(args))
        for kind, idx, _, _ in packets:
            reply = receiver.receive(kind, idx, 0.2)
            assert reply and reply[0] == 0x55
        assert any(item[1] == 116 for item in logs)
    print(f"PASS 22 cases + two Python/C mixed-wire cases; artifacts: {work}")


if __name__ == "__main__":
    main()
