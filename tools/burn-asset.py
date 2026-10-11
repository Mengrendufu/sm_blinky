#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""草案（未进仓库、未在硬件上跑过）：单脚本管全程 —— 串口常开 → J-Link 置标志复位 → 握手 → 分块烧写。

流程（对应图上 ①~⑥）：
  ① open_serial_quiet()：先设 dtr/rts=False 再 open → 打开瞬间即 deassert，不碰 NRST
  ② jlink_flag_and_reset()：写 noinit 标志 + NRST 复位（串口此时已开，但控制线是 deassert，NRST 归 J-Link）
  ③ handshake()：重复发握手直到 ACK（带超时与诊断）
  ④ 分块停等传输（NAK 重传）
  ⑤ 打印时间账
  ⑥ 关串口

协议里还没定的部分标了 TODO —— 定完就能真跑。
"""
import argparse, os, struct, subprocess, sys, time, zlib
import serial  # pyserial

JLINK = r"C:\mengrendufu\software_ins\JLink\JLink.exe"
JL_OPTS = "-device STM32F103ZE -if SWD -speed 4000 -autoconnect 1 -ExitOnError 1 -NoGui 1"
CMD = "cmd.exe"

# ---- 线协议（握手部分已与目标机联调验证，见 AGENTS.md 的镜像布局条）----
#   request  PC -> BL:  AA | ver(1) | len(4 BE) | crc32(4 BE)        10 B
#                        crc32 覆盖 ver+len
#   reply    BL -> PC:  55 | base(4 BE) | block(2 BE) | crc32(4 BE)  11 B
#                        crc32 覆盖前 7 B（帧头也在内）
#   refusal  BL -> PC:  4E（版本 / 长度 / 帧 CRC 任一不过）
# 块级载荷还没定（下一步），但整张的最终校验定了：资产头里的 CRC32。
REQ_MAGIC, ACK_MAGIC, NAK_MAGIC = 0xAA, 0x55, 0x4E
PROTO_VER = 1
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
    path = os.path.join(os.environ.get("TEMP", r"C:\tmp_atk"), "enter-burn.jlink")
    open(path, "w", encoding="utf-8").write(script)
    win = path   # already a Windows path when run from Windows
    if " " in win:
        sys.exit("[burn] 路径带空格，cmd.exe 传不过去")
    t0 = time.time()
    subprocess.run([CMD, "/c", f"{JLINK} {JL_OPTS} -CommandFile {win}"], check=True)
    print(f"[burn] J-Link 完成，用时 {time.time()-t0:.1f} s")


def handshake(ser, img_len):
    """③ 重复发握手帧直到 ACK；对回帧自己再算一遍 CRC（本轮唯一的硬要求：最终校验）。"""
    core = struct.pack(">BI", PROTO_VER, img_len)
    req = bytes([REQ_MAGIC]) + core + struct.pack(">I", zlib.crc32(core) & 0xFFFFFFFF)
    deadline = time.time() + HANDSHAKE_TIMEOUT_S
    sent = 0
    while time.time() < deadline:
        ser.reset_input_buffer()
        ser.write(req)
        sent += 1
        t0 = time.time()
        while time.time() - t0 < 0.2:
            b = ser.read(1)
            if b and b[0] == NAK_MAGIC:
                sys.exit("[burn] 目标回 NAK：版本 / 长度 / 帧 CRC 有一个没过")
            if not (b and b[0] == ACK_MAGIC):
                continue
            rest = ser.read(ACK_LEN - 1)
            if len(rest) != ACK_LEN - 1:
                sys.exit(f"[burn] ACK 只收到 {len(rest) + 1}/{ACK_LEN} 字节")
            body = bytes([ACK_MAGIC]) + rest[0:6]
            crc = struct.unpack(">I", rest[6:10])[0]
            want = zlib.crc32(body) & 0xFFFFFFFF
            if crc != want:
                sys.exit(f"[burn] ACK 的 CRC 对不上：{crc:08X} vs {want:08X}")
            base, block = struct.unpack(">IH", rest[0:6])
            print(f"[burn] ACK（第 {sent} 次握手）：基址 0x{base:08X}、块 {block} B、帧 CRC 验过")
            return base, block
    sys.exit("[burn] 握手超时。检查：① 串口开完后 rts/dtr 是否都是 deassert（否则 RESET 被压住）"
             " ② J-Link 那一步是否成功 ③ BOOT0 是否被 RTS 拉高进了 ROM bootloader（再复位一次）")



def send_blocks(ser, img, corrupt=None):
    """④ 分块停等 + NAK 重传。--corrupt N 会故意把第 N 块的载荷改一个字节，用来验重传。"""
    total = len(img)
    blocks = 0
    retries = 0
    # Each block now costs a 4 KB sector erase (~130 ms) plus programming and a
    # read-back, so the wait for its answer has to be seconds, not the 50 ms
    # the handshake used.
    ser.timeout = 3.0
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
            ser.reset_input_buffer()
            ser.write(hdr + bytes(blk))
            reply = ser.read(1)
            if reply and reply[0] == ACK_MAGIC:
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


def send_final(ser, img):
    """⑤ 最终校验：整张的字节数 + CRC32 —— 本协议唯一的硬要求。"""
    crc = zlib.crc32(img) & 0xFFFFFFFF
    fin = bytes([FIN_MAGIC]) + struct.pack(">I", len(img)) + struct.pack(">I", crc)
    ser.write(fin)
    r = ser.read(1)
    if not (r and r[0] == ACK_MAGIC):
        sys.exit(f"[burn] 最终校验没过（目标机回 {r.hex() if r else '无应答'}）")
    print(f"[burn] 最终校验通过：{len(img)} B，CRC32 {crc:08X}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--image", required=True, help="RGB565 裸数据（240x320x2 = 153600 B）")
    ap.add_argument("--elf", default="build/debug/sm_blinky.elf")
    ap.add_argument("--noinit", help="noinit base hex; skips arm-none-eabi-nm (WSL only)")
    ap.add_argument("--tpl", default=os.path.join(os.path.dirname(__file__), "enter-burn.jlink.tpl"))
    ap.add_argument("--no-jlink", action="store_true", help="跳过 J-Link（标志已置好时用，便于调协议）")
    ap.add_argument("--handshake-only", action="store_true", help="握上手就停（联调用，不碰 flash）")
    ap.add_argument("--blocks-only", action="store_true", help="块 + 最终校验做完就停（不碰 flash）")
    ap.add_argument("--corrupt-block", type=int, default=None, metavar="N",
                    help="故意弄坏第 N 块，验重传路径")
    a = ap.parse_args()

    img = open(a.image, "rb").read()
    crc = zlib.crc32(img) & 0xFFFFFFFF
    print(f"[burn] 镜像 {len(img)} B，CRC32 {crc}（0x{crc:08X}）")

    ser = open_serial_quiet(a.port)                      # ①
    print(f"[burn] 串口 {a.port} 已打开并保持（rts/dtr = deassert）")
    try:
        if not a.no_jlink:
            jlink_flag_and_reset(a.noinit if a.noinit else noinit_addr(a.elf), a.tpl)   # ②
        handshake(ser, len(img))                         # ③
        if a.handshake_only:
            print("[burn] --handshake-only：到此为止（不碰 flash）")
            return
        send_blocks(ser, img, a.corrupt_block)            # ④
        send_final(ser, img)                             # ⑤
        if a.blocks_only:
            print("[burn] --blocks-only：到此为止（没碰 flash）")
            return
    finally:
        ser.close()                                      # ⑥
        print("[burn] 串口已关闭")


if __name__ == "__main__":
    main()
