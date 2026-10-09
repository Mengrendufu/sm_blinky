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
    py tools/trace_read.py --port COM12 --seconds 25 --gaps

--gaps 打印每条记录**到达主机**的时刻与间隔。记录本身不带时间戳，但
"刷屏期间泵有没有拿到时间片"只有到达时刻能回答：同步刷屏时一整帧的记录
会在帧末成批涌出（UI 线程占着 CPU，idle 没跑），DMA 之后应该散在帧内。

注意：建口时先把 DTR/RTS 压到 deassert 再 open，否则 CH340 那条支路会碰
NRST（见 AGENTS.md）。
"""
import argparse
import sys
import time

import serial

ESC, MARK = 0x7D, 0x7E

# 逐字节状态机，策略照抄 sm_tracer_tui 的 HdlcParser
# （application/aos/sp_mngr/hdlc_parser.c）：任何 0x7E 都重开一帧、字段一个个
# 收、**长度先验证再收载荷**、校验和决定这帧算不算数。
#
# 在这之前是"批量版"：找到 0x7E 就把到下一个 0x7E 之间的全当一帧 ✗ —— 长度字
# 节一坏就把**跟在后面的好几帧**一起吞掉。同一份 294 条的好抓包、同种子打坏，
# 记录存活率（旧 → 新）：丢 0.1% 字节 83% → 99%、丢 1% 48% → 90%、
# 丢 5% 35% → 62%（已接近上限：5% 丢字节会碰到约一半的帧）；坏 5 个字节
# 24% → 98%。
HUNT, SEQ, RECID, LEN, PAYLOAD, CHK = 0, 1, 2, 3, 4, 5
# 记录块一共 8 字节（recId + len + 载荷 + 校验），载荷最多 6。
DEFAULT_MAX_PAYLOAD = 6


def decode(buf, max_payload=DEFAULT_MAX_PAYLOAD, stats=None):
    """把原始字节流切成记录。返回 [(seq, recId, payload)]。

    stats 传 dict 时会被填上 frames / errors：一份日志解出 0 条时，
    errors 比 frames 更能说明发生了什么。
    """
    out = []
    errors = 0
    state = HUNT
    escaped = False
    frame = bytearray()
    left = 0

    for b in buf:
        if escaped:                  # 转义过的字节永远算数据
            byte = b ^ 0x20
            escaped = False
        elif b == ESC:
            escaped = True
            continue
        elif b == MARK:
            frame = bytearray()      # 任何 flag 都重开一帧：坏帧只赔自己
            state = SEQ
            continue
        else:
            byte = b

        if state == HUNT:
            continue                 # 帧与帧之间的噪声

        if state == SEQ:
            frame.append(byte)
            state = RECID
        elif state == RECID:
            frame.append(byte)
            state = LEN
        elif state == LEN:
            frame.append(byte)
            left = byte
            if left > max_payload:
                errors += 1          # 长度不可能：立刻丢，不吞后面的帧
                state = HUNT
            else:
                state = PAYLOAD if left else CHK
        elif state == PAYLOAD:
            frame.append(byte)
            left -= 1
            if left == 0:
                state = CHK
        else:                        # CHK
            frame.append(byte)
            seq = frame[0]
            rec = bytes(frame[1:-1])
            if (~(sum(rec) + seq)) & 0xFF == frame[-1]:
                out.append((seq, rec[0], bytes(rec[2:2 + rec[1]])))
            else:
                errors += 1
            state = HUNT

    if stats is not None:
        stats["frames"] = len(out)
        stats["errors"] = errors
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM12")
    ap.add_argument("--seconds", type=float, default=40.0)
    ap.add_argument("--rec", type=int, default=None, help="只看这个 RecID")
    ap.add_argument("--gaps", action="store_true",
                    help="打印到达时刻与间隔（刷屏有没有饿死 idle 泵）")
    ap.add_argument("--raw", default=None, help="把原始字节存到这个路径")
    ap.add_argument("--max-payload", type=int, default=DEFAULT_MAX_PAYLOAD,
                    help="记录载荷上限（默认 6，见记录块的 8 字节上限）")
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
    stamps = []
    shown = 0
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < a.seconds:
        pending = ser.in_waiting
        if pending == 0:
            time.sleep(0.0002)   # 0.2 ms 轮询：帧只有 10~20 ms，够分辨
            continue
        buf += ser.read(pending)
        if a.gaps:
            t = time.perf_counter() - t0
            got = decode(bytes(buf))
            while shown < len(got):
                stamps.append((t, got[shown]))
                shown += 1
    ser.close()

    if a.raw:
        with open(a.raw, "wb") as f:
            f.write(bytes(buf))
        print(f"[trace] 原始字节 -> {a.raw}")

    stats = {}
    recs = decode(bytes(buf), a.max_payload, stats)
    print(f"[trace] {len(buf)} B, {stats['frames']} 条记录, "
          f"{stats['errors']} 帧被丢弃（长度/校验失败）")
    if a.rec is not None:
        recs = [r for r in recs if r[1] == a.rec]
        print(f"[trace] 其中 RecID {a.rec}: {len(recs)} 条")
    if a.gaps and stamps:
        prev = stamps[0][0]
        for t, (seq, recId, payload) in stamps:
            print(f"  t=+{t * 1000:9.3f} ms  dt={((t - prev) * 1000):7.3f}"
                  f"  seq={seq:3d} recId={recId:3d}"
                  f" payload={payload.hex(' ')}")
            prev = t
        return 0

    for seq, recId, payload in recs:
        print(f"  seq={seq:3d} recId={recId:3d} payload={payload.hex(' ')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
