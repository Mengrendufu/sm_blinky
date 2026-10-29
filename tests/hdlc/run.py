#!/usr/bin/env python3
"""Exercise imported HSM, C/Python codecs, raw data and upload routing."""
from pathlib import Path
import importlib.util
import random
import struct
import subprocess
import sys
import tempfile
import types

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools"))
from hdlc_link import AssetLink, Decoder, encode  # noqa: E402

work = Path(tempfile.mkdtemp(prefix="hdlc-link-test-"))
base = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-g",
        "-Wno-unused-function", "-fsanitize=address,undefined",
        "-fno-pie", "-no-pie"]
for folder in (ROOT / "bootloader/ports", ROOT / "project_common/link",
               ROOT / "3rd_party/sm_hsm/include"):
    base += ["-I", str(folder)]
sources = [str(ROOT / p) for p in (
    "project_common/link/hdlc_parser.c", "project_common/link/hdlc_frame.c",
    "3rd_party/sm_hsm/src/sm_hsm.c")]
subprocess.run(base + sources + [str(HERE / "oracle.c"), "-o",
                                str(work / "oracle")], check=True)
subprocess.run(base + sources + ["-DHDLC_IMPORTED_TEST",
    str(HERE / "oracle.c"), str(HERE / "test_imported_parser.c"),
    "-o", str(work / "imported")], check=True)
subprocess.run([str(work / "imported")], check=True)


def oracle(mode, data):
    p = subprocess.run([str(work / "oracle"), mode], input=data,
                       capture_output=True, check=True)
    if mode == "encode":
        return p.stdout
    frames, off = [], 0
    while off < len(p.stdout):
        size = int.from_bytes(p.stdout[off:off + 2], "big")
        off += 2
        frames.append(p.stdout[off:off + size])
        off += size
    return frames


rng = random.Random(14)
stream = bytearray(b"noise ACK\x55 NAK\x4e")
for n in (0, 1, 6, 14, 126, 125, 255):
    payload = bytes(rng.randrange(256) for _ in range(n))
    for seq in (0, 0x7D, 0x7E, 255):
        raw = bytes([seq, 0x7E, n]) + payload
        wire = encode(seq, 0x7E, payload)
        assert oracle("encode", raw) == wire
        assert oracle("frames", wire) == [raw]
        stream.extend(wire)
# Resynchronization, repeated escape, truncated frames and bad checksum.
stream += b"\x7e\x01\x7d\x7d\x55\x4e"
stream += encode(1, 115, b"\x55\x4e\x7e\x7d")[:-4]
stream += b"\x7e\x01\x02\x00\x00\x7e"
stream += encode(2, 116, b"\x55\x4e\x7e\x7d")
expected = oracle("frames", stream)
for chunk in (1, 2, 7, 31, len(stream)):
    decoder, actual = Decoder(), []
    for off in range(0, len(stream), chunk):
        actual += decoder.feed(stream[off:off + chunk])
    assert [bytes([s, i, len(p)]) + p for s, i, p in actual] == expected


class Serial:
    def __init__(self, wire=b""):
        self.writes, self.wire, self.timeout = [], bytearray(wire), 0.05

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def read(self, size):
        n = min(size, 7, len(self.wire))
        result = bytes(self.wire[:n])
        del self.wire[:n]
        return result


# A block transaction contains one HDLC control frame and exact raw bytes.
packet = b"\xB1" + struct.pack(">HHH", 3, 4096, 0x1234)
raw_data = bytes([0x7E, 0x7D, 0x55, 0x4E]) * 1024
serial = Serial()
AssetLink(serial).send(packet, raw_data)
assert len(serial.writes) == 1
transaction = serial.writes[0]
assert transaction == encode(0, 0xF0, packet) + raw_data
assert transaction.endswith(raw_data)
assert oracle("frames", encode(0, 0xF0, packet)) == [
    bytes([0, 0xF0, len(packet)]) + packet]
trace = encode(33, 116, b"\x55\x4e\x7d\x7e")

# A diagnostic ACK-looking byte and a stale block ACK cannot acknowledge
# the current transaction. Also check checksum failures before a good reply.
logs = []
reply = lambda kind, idx, answer: encode(
    44, 0xF1, bytes([kind]) + struct.pack(">H", idx) + answer)
serial = Serial(trace + reply(0xB1, 2, b"\x55")
                + reply(0xB1, 3, b"\x55"))
link = AssetLink(serial, trace=lambda *args: logs.append(args))
assert link.receive(0xB1, 3, 0.2) == b"\x55" and len(logs) == 1

# Exercise the real uploader functions without opening a physical port.
sys.modules.setdefault("serial", types.ModuleType("serial"))
spec = importlib.util.spec_from_file_location(
    "burn", ROOT / "tools/burn-asset.py")
burn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(burn)
import zlib
body = b"\x55" + struct.pack(">IH", 0x81000, 4096)
hello = body + struct.pack(">I", zlib.crc32(body))
serial = Serial(trace + reply(0xAA, 0, hello)
                + trace + reply(0xB1, 0, b"\x4e")
                + trace + reply(0xB1, 0, b"\x55")
                + trace + reply(0xC1, 0, b"\x55"))
link = AssetLink(serial)
assert burn.handshake(link, 4096) == (0x81000, 4096)
burn.send_blocks(link, bytes([0x7E]) * 4096, corrupt=0)
burn.send_final(link, bytes([0x7E]) * 4096)
assert serial.writes
print("PASS: imported HSM tests, C/Python framing, raw-data transmission,")
print("      mixed diagnostics, stale ACK filtering and uploader retry/final")
print(f"Host artifacts: {work}")
