#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Burn a complete AST2 package over UART, with per-image stored CRC32.

Input is concatenated raw RGB565 slots (Sys, Live, Splash). The script builds
an AST2 directory, then transfers directory sector and payload as one package.
Bootloader verifies stored images before committing the directory.
"""
import argparse, os, struct, subprocess, sys, time, zlib
import tempfile
import serial  # pyserial

from asset_pack import build_pack, VERSION
from hdlc_link import AssetLink

JLINK = r"C:\mengrendufu\software_ins\JLink\JLink.exe"
JL_OPTS = "-device STM32F103ZE -if SWD -speed 4000 -autoconnect 1 -ExitOnError 1 -NoGui 1"

# HDLC IDs F0/F1 carry requests/replies; legacy inner fields follow.
#   request  PC -> BL:  AA | ver(1) | len(4 BE) | crc32(4 BE)        10 B
#                        crc32 覆盖 ver+len
#   reply    BL -> PC:  55 | base(4 BE) | block(2 BE) | crc32(4 BE)  11 B
#                        crc32 覆盖前 7 B（帧头也在内）
#   refusal  BL -> PC:  4E（版本 / 长度 / 帧 CRC 任一不过）
# v2: complete AST2 directory sector + image payload; transport CRC retained.
REQ_MAGIC, ACK_MAGIC, NAK_MAGIC = 0xAA, 0x55, 0x4E
PROTO_VER = VERSION
REQ_LEN, ACK_LEN = 10, 11
BLK_MAGIC, FIN_MAGIC = 0xB1, 0xC1
BLK_HDR_LEN, FIN_LEN = 7, 9
BLOCK = 4096
HANDSHAKE_TIMEOUT_S = 5.0
BLOCK_ACK_TIMEOUT_S = 2.0
MAX_BLOCK_RETRY = 3


def open_serial_quiet(port, baud=115200):
    """① 开串口，并保证【打开那一瞬】就不碰 DTR/RTS。

    依据（pyserial 源码）：默认 `_dtr_state/_rts_state = True`（serialutil.py:216-217）
    = 要 assert；但属性 setter 有 `if self.is_open` 守卫（serialutil.py:459-471），
    未打开时只记录；open() 再把记录的值写下去（serialposix.py:333-339；Windows 走 DCB
    serialwin32.py:208-211）→ 所以【先设 False 再 open】，打开瞬间就是 deassert ✓。

    反面写法（先 open 再 dtr=False）会先 assert 一下：那等于"干净地按了一次复位键"
    （必须 rts 先、dtr 后，否则复位释放时 BOOT0 还高 → 进 ROM bootloader）。

    ★ 已实测（2026-10-08，硬件验证）：RTS/DTR 都 OFF 时，串口保持打开也不干预 NRST，
      复位归 J-Link 独占 —— 本函数就是照这个结论写的（见 AGENTS.md 的 CH340 那条）。
    """
    ser = serial.Serial()
    ser.port, ser.baudrate = port, baud
    ser.bytesize, ser.parity, ser.stopbits = 8, serial.PARITY_NONE, serial.STOPBITS_ONE
    ser.dsrdtr = False
    ser.rtscts = False
    ser.rts = False   # 未打开 → 只记录，不碰线
    ser.dtr = False   # 同上（此时顺序无所谓）
    ser.timeout = 0.05
    ser.open()        # 打开瞬间写下去的就是 deassert ✓
    return ser


def noinit_addr(elf):
    out = subprocess.run(["arm-none-eabi-nm", elf], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == "_snoinit":
            return "0x" + parts[0]
    sys.exit("[burn] ELF 里找不到 _snoinit —— .ld 需要加 ._noinit 段")


def jlink_flag_and_reset(addr, tpl):
    """② 生成命令文件并执行：置标志 → 复位。（JLink 退出时用 qc，目标继续跑）"""
    script = open(tpl, encoding="utf-8").read().replace("@ADDR@", addr)
    task_dir = tempfile.mkdtemp(prefix="sm-blinky-burn-")
    path = os.path.join(task_dir, "enter-burn.jlink")
    open(path, "w", encoding="utf-8").write(script)
    print(f"[burn] J-Link command file: {path}")
    t0 = time.time()
    subprocess.run([JLINK, *JL_OPTS.split(), "-CommandFile", path],
                   check=True)
    print(f"[burn] J-Link 完成，用时 {time.time()-t0:.1f} s")


def handshake(link, img_len):
    """Negotiate through HDLC replies; diagnostic frames are routed apart."""
    core = struct.pack(">BI", PROTO_VER, img_len)
    req = bytes([REQ_MAGIC]) + core + struct.pack(
        ">I", zlib.crc32(core) & 0xFFFFFFFF)
    deadline = time.monotonic() + HANDSHAKE_TIMEOUT_S
    sent = 0
    while time.monotonic() < deadline:
        link.send(req)
        sent += 1
        answer = link.receive(REQ_MAGIC, 0, min(
            0.05, max(0, deadline - time.monotonic())))
        if answer is None:
            continue
        if answer == bytes([NAK_MAGIC]):
            sys.exit("[burn] handshake rejected: version/length/CRC")
        if len(answer) != ACK_LEN or answer[0] != ACK_MAGIC:
            sys.exit("[burn] invalid framed handshake reply")
        crc = struct.unpack(">I", answer[7:11])[0]
        if crc != zlib.crc32(answer[:7]) & 0xFFFFFFFF:
            sys.exit("[burn] handshake reply CRC mismatch")
        base, block = struct.unpack(">IH", answer[1:7])
        if block != BLOCK:
            sys.exit("[burn] target block size differs from this uploader")
        print(f"[burn] ACK ({sent} tries): base 0x{base:08X}, block {block}")
        return base, block
    sys.exit("[burn] HDLC handshake timed out; check reset/BOOT0/UART")


def send_blocks(link, img, corrupt=None):
    """④ 分块停等 + NAK 重传。--corrupt N 会故意把第 N 块的载荷改一个字节，用来验重传。"""
    total = len(img)
    blocks = 0
    retries = 0
    # Each block now costs a 4 KB sector erase (~130 ms) plus programming and a
    # read-back, so the wait for its answer has to be seconds, not the 50 ms
    # the handshake used.
    t0 = time.time()
    for idx, off in enumerate(range(0, total, BLOCK)):
        blk = bytearray(img[off:off + BLOCK])
        # 注意顺序：块头 CRC 按【好数据】算，然后才弄坏载荷 —— 这样块自校验才会
        # 不过、目标机才会 NAK。若先弄坏再算 CRC，块自校验照样通过，只有最终
        # 整张校验能发现（那验证的是另一件事）。
        lo16 = zlib.crc32(bytes(blk)) & 0xFFFF
        if corrupt is not None and idx == corrupt:
            blk[0] ^= 0xFF
            print(f"[burn] 故意弄坏第 {idx} 块的第一个字节（块头 CRC 仍是好数据的）")
        hdr = struct.pack(">BHHH", BLK_MAGIC, idx, len(blk), lo16)
        for attempt in range(1, MAX_BLOCK_RETRY + 1):
            link.send(hdr, bytes(blk))
            reply = link.receive(BLK_MAGIC, idx, BLOCK_ACK_TIMEOUT_S)
            if reply == bytes([ACK_MAGIC]):
                blocks += 1
                break
            retries += 1
            print(f"[burn] 块 {idx} 第 {attempt} 次被拒（目标机回 "
                  f"{reply.hex() if reply else '无应答'}），重传")
            if corrupt is not None and idx == corrupt and attempt == 1:
                blk[0] ^= 0xFF      # 只有第一次是坏的，重传发正确的数据
                print(f"[burn] 块 {idx} 改回正确数据")
        else:
            sys.exit(f"[burn] 块 {idx} 重传 {MAX_BLOCK_RETRY} 次仍失败")
        if (idx % 8 == 0) or (off + BLOCK >= total):
            done = min(off + BLOCK, total)
            print(f"[burn] {done * 100 // total:3d}%  ({done}/{total} B)")
    dt = time.time() - t0
    print(f"[burn] 块传输完成：{blocks} 块、重传 {retries} 次、用时 {dt:.1f} s"
          f"（{total / 1024:.0f} KiB；115200 的理论下限约 {total * 10 / 115200:.1f} s）")


def send_final(link, img):
    """Finish transport; target checks NOR images before directory commit."""
    crc = zlib.crc32(img) & 0xFFFFFFFF
    fin = bytes([FIN_MAGIC]) + struct.pack(">I", len(img)) + struct.pack(">I", crc)
    link.send(fin)
    # Final verification rereads all stored images before sending ACK.
    r = link.receive(FIN_MAGIC, 0, 10.0)
    if r != bytes([ACK_MAGIC]):
        sys.exit(f"[burn] 最终校验没过（目标机回 {r.hex() if r else '无应答'}）")
    print(f"[burn] 最终校验通过：{len(img)} B，CRC32 {crc:08X}")


def print_trace(sequence, kind, payload):
    text = payload.decode("utf-8", errors="replace") if kind == 0xF2 else (
        f"id={kind}: {payload.hex()}")
    print(f"[trace] {sequence} {text.rstrip()}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--image", required=True,
                    help="1-3 raw RGB565 slots, 153600 B each; "
                         "Sys, Live, Splash")
    ap.add_argument("--elf", default="build/debug/sm_blinky.elf")
    ap.add_argument("--noinit", help="noinit base hex; skips arm-none-eabi-nm (WSL only)")
    ap.add_argument("--tpl", default=os.path.join(os.path.dirname(__file__), "enter-burn.jlink.tpl"))
    ap.add_argument("--no-jlink", action="store_true",
                    help="skip J-Link; reset into the 200 ms HELLO window "
                         "within 5 s, or use an already-set upgrade flag")
    ap.add_argument("--handshake-only", action="store_true", help="握上手就停（联调用，不碰 flash）")
    ap.add_argument("--blocks-only", action="store_true",
                    help="完整烧写并提交后结束（兼容旧参数）")
    ap.add_argument("--corrupt-block", type=int, default=None, metavar="N",
                    help="故意弄坏第 N 块，验重传路径")
    a = ap.parse_args()

    try:
        img = build_pack(open(a.image, "rb").read())
    except ValueError as error:
        ap.error(str(error))
    crc = zlib.crc32(img) & 0xFFFFFFFF
    print(f"[burn] 镜像 {len(img)} B，CRC32 {crc}（0x{crc:08X}）")

    ser = open_serial_quiet(a.port)                      # ①
    print(f"[burn] 串口 {a.port} 已打开并保持（rts/dtr = deassert）")
    try:
        if not a.no_jlink:
            jlink_flag_and_reset(a.noinit if a.noinit else noinit_addr(a.elf), a.tpl)   # ②
        link = AssetLink(ser, trace=print_trace)
        handshake(link, len(img))                         # ③
        if a.handshake_only:
            print("[burn] --handshake-only：到此为止（不碰 flash）")
            return
        send_blocks(link, img, a.corrupt_block)            # ④
        send_final(link, img)                             # ⑤
        if a.blocks_only:
            print("[burn] --blocks-only：完整资产包已烧写并提交")
            return
    finally:
        ser.close()                                      # ⑥
        print("[burn] 串口已关闭")


if __name__ == "__main__":
    main()
