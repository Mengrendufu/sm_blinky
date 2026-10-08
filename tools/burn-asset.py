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
CMD = "/mnt/c/Windows/System32/cmd.exe"

# ---- TODO(协议)：这些数字定下来才能互操作 ----
MAGIC_REQ, MAGIC_ACK, MAGIC_NAK = 0xAA, 0x55, 0x4E
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
    path = "/tmp/enter-burn.jlink"
    open(path, "w", encoding="utf-8").write(script)
    win = subprocess.run(["wslpath", "-w", path], capture_output=True, text=True).stdout.strip()
    if " " in win:
        sys.exit("[burn] 路径带空格，cmd.exe 传不过去")
    t0 = time.time()
    subprocess.run([CMD, "/c", f"{JLINK} {JL_OPTS} -CommandFile {win}"], check=True)
    print(f"[burn] J-Link 完成，用时 {time.time()-t0:.1f} s")


def handshake(ser, img_len, crc32):
    """③ 重复发握手帧直到 ACK；超时给明确诊断。"""
    req = struct.pack(">BBI I", MAGIC_REQ, 1, img_len, crc32)  # TODO(协议): 帧格式/长度字段
    deadline = time.time() + HANDSHAKE_TIMEOUT_S
    sent = 0
    while time.time() < deadline:
        ser.reset_input_buffer()
        ser.write(req); sent += 1
        t0 = time.time()
        while time.time() - t0 < 0.2:
            b = ser.read(1)
            if b and b[0] == MAGIC_ACK:
                rest = ser.read(8)                      # TODO(协议): ACK 载荷 = 基址 + 块大小
                print(f"[burn] 收到 ACK（第 {sent} 次握手），基址/块大小 {rest.hex()}")
                return
            if b and b[0] == MAGIC_NAK:
                sys.exit("[burn] 目标回 NAK（帧非法或长度超 assets 区）")
    sys.exit("[burn] 握手超时。检查：① 串口开完后 rts/dtr 是否都是 deassert（否则 RESET 被压住）"
             "② J-Link 那一步是否成功 ③ BOOT0 是否被 RTS 拉高进了 ROM bootloader（再复位一次）")


def send_blocks(ser, img):
    """④ 38 块停等 + NAK 重传。"""
    total = len(img)
    t0 = time.time()
    for idx, off in enumerate(range(0, total, BLOCK)):
        blk = img[off:off + BLOCK]
        hdr = struct.pack(">BHH H", 0xB1, idx, len(blk), zlib.crc32(blk) & 0xFFFF)  # TODO(协议)
        for attempt in range(1, MAX_BLOCK_RETRY + 1):
            ser.reset_input_buffer()
            ser.write(hdr + blk)
            reply = ser.read(1)
            if reply and reply[0] == MAGIC_ACK:
                break
            print(f"[burn] 块 {idx} 第 {attempt} 次失败，重传")
        else:
            sys.exit(f"[burn] 块 {idx} 重传 {MAX_BLOCK_RETRY} 次仍失败")
        if idx % 8 == 0 or off + BLOCK >= total:
            done = min(off + BLOCK, total)
            print(f"[burn] {done*100//total:3d}%  ({done}/{total} B)")
    dt = time.time() - t0
    print(f"[burn] 传输完成，用时 {dt:.1f} s（{total/1024:.0f} KiB，{total*8/18e6*1000:.0f} ms 是理论 SPI 时间，"
          f"其余是串口 13.3 s 量级 + 擦写）")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--image", required=True, help="RGB565 裸数据（240x320x2 = 153600 B）")
    ap.add_argument("--elf", default="build/debug/bootloader.elf")
    ap.add_argument("--tpl", default=os.path.join(os.path.dirname(__file__), "enter-burn.jlink.tpl"))
    ap.add_argument("--no-jlink", action="store_true", help="跳过 J-Link（标志已置好时用，便于调协议）")
    a = ap.parse_args()

    img = open(a.image, "rb").read()
    crc = zlib.crc32(img) & 0xFFFFFFFF
    print(f"[burn] 镜像 {len(img)} B，CRC32 {crc}（0x{crc:08X}）")

    ser = open_serial_quiet(a.port)                      # ①
    print(f"[burn] 串口 {a.port} 已打开并保持（rts/dtr = deassert）")
    try:
        if not a.no_jlink:
            jlink_flag_and_reset(noinit_addr(a.elf), a.tpl)   # ②
        handshake(ser, len(img), crc)                    # ③
        send_blocks(ser, img)                            # ④ + ⑤
        todo = ser.read(8)                               # TODO(协议): DONE 帧 + 回读 CRC 结果
        print(f"[burn] 收尾回复：{todo.hex()}")
    finally:
        ser.close()                                      # ⑥
        print("[burn] 串口已关闭")


if __name__ == "__main__":
    main()
