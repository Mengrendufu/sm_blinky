#!/usr/bin/env python3
"""Build an AST2 package from concatenated fixed-size RGB565 image slots.

IDs follow input order: 0 = Sys background, 1 = Live background, 2 = Splash.
The burn tool sends this whole package; individual updates are unsupported.
"""
import argparse
from pathlib import Path
import struct
import zlib

SECTOR = 4096
SLOT_BYTES = 153600
MAX_SLOTS = 3
HEADER_BYTES = 68
VERSION = 2
MAGIC = 0x41535432


def build_pack(payload):
    if not payload or len(payload) % SLOT_BYTES:
        raise ValueError("payload must contain complete 153600-byte slots")
    count = len(payload) // SLOT_BYTES
    if count > MAX_SLOTS:
        raise ValueError("at most three image slots are supported")
    header = bytearray(struct.pack(
        ">4I", MAGIC, VERSION, count, len(payload)))
    for slot in range(MAX_SLOTS):
        if slot < count:
            offset = slot * SLOT_BYTES
            crc = zlib.crc32(payload[offset:offset + SLOT_BYTES])
            header += struct.pack(">4I", slot, offset, SLOT_BYTES, crc)
        else:
            header += bytes(16)
    header += struct.pack(">I", zlib.crc32(header))
    assert len(header) == HEADER_BYTES
    return bytes(header) + bytes([0xFF]) * (SECTOR - len(header)) + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="concatenated raw slots")
    parser.add_argument("output", type=Path, help="AST2 output package")
    args = parser.parse_args()
    try:
        package = build_pack(args.image.read_bytes())
    except ValueError as error:
        parser.error(str(error))
    args.output.write_bytes(package)
    print(f"AST2: {len(package)} bytes, "
          f"{(len(package) - SECTOR) // SLOT_BYTES} images")


if __name__ == "__main__":
    main()
