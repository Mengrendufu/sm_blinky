#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Decode the tracer's records off the serial port.

Wire format (see SM_Tracer_idle in 3rd_party/sm_hsm/src/sm_tracer.c):

    [0x7E] [seq] [recId] [dataLen] [payload ...] [~chksum] [0x7E]

0x7E and 0x7D are escaped as `0x7D 0x20 ^ byte`, and the checksum is the sum of
the sequence number and the record bytes, sent inverted.

用法（Windows 侧，pyserial 3.5）:
    py tools/trace_read.py --port COM12 --seconds 40
    py tools/trace_read.py --port COM12 --seconds 40 --rec 37     # 只看 LCD GRAM

注意：建口时先把 DTR/RTS 压到 deassert 再 open，否则 CH340 那条支路会碰
NRST（见 AGENTS.md）。
"""
import argparse
import sys
import time

import serial

ESC, MARK = 0x7D, 0x7E


def decode(buf):
    """把原始字节流切成记录。返回 [(seq, recId, payload)]。"""
    out = []
    i = 0
    while i < len(buf):
        if buf[i] != MARK:
            i += 1
            continue
        # 帧体：直到下一个未转义的 0x7E
        j = i + 1
        body = bytearray()
        while j < len(buf) and buf[j] != MARK:
            if buf[j] == ESC and j + 1 < len(buf):
                body.append(buf[j + 1] ^ 0x20)
                j += 2
            else:
                body.append(buf[j])
                j += 1
        if j < len(buf) and len(body) >= 3:
            seq = body[0]
            rec = body[1:-1]
            chk = body[-1]
            if (~(sum(rec) + seq)) & 0xFF == chk:
                out.append((seq, rec[0], bytes(rec[2:2 + rec[1]])))
        i = j + 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM12")
    ap.add_argument("--seconds", type=float, default=40.0)
    ap.add_argument("--rec", type=int, default=None, help="只看这个 RecID")
    a = ap.parse_args()

    ser = serial.Serial()
    ser.port, ser.baudrate = a.port, 115200
    ser.dsrdtr = False
    ser.rtscts = False
    ser.rts = False          # 未打开 → 只记录，不碰线
    ser.dtr = False
    ser.timeout = 0.2
    ser.open()               # 打开瞬间写下去的就是 deassert ✓

    print(f"[trace] 读 {a.port} {a.seconds:.0f}s ...")
    buf = bytearray()
    t0 = time.time()
    while time.time() - t0 < a.seconds:
        buf += ser.read(4096)
    ser.close()

    recs = decode(bytes(buf))
    print(f"[trace] {len(buf)} B, {len(recs)} 条记录")
    if a.rec is not None:
        recs = [r for r in recs if r[1] == a.rec]
        print(f"[trace] 其中 RecID {a.rec}: {len(recs)} 条")
    for seq, recId, payload in recs:
        print(f"  seq={seq:3d} recId={recId:3d} payload={payload.hex(' ')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
