"""开机展示页（power-on image）：240x320，RGB565 精确格点，零量化损失。

规矩与 gen_bg.py 一样：设计时只允许用 c565() 的输出，末尾断言
RGB565 -> 8 位 -> RGB565 逐字节相同。这一张不是背景，是启动画面：
深底 + 圆角边框 + 一条方波（blinky 的意思）+ 点阵字 "sm_blinky"。
"""
import math, os, struct, zlib

W, H = 240, 320
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "bg")
OUT = os.path.normpath(OUT)


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def png_write(path, rgb):
    raw = bytearray()
    for y in range(H):
        raw += b"\x00" + rgb[y * W * 3:(y + 1) * W * 3]
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
                           + chunk(b"IEND", b""))


def c565(r, g, b):
    """一个颜色在 RGB565 里的格点（8 位形式，纯截断）。

    与 gen_bg.py 同一口径：截断后的值再放回 5/6/5 位必须原样回来。
    不要做位复制（r | r>>5）—— 那会让 b=64 变成 66，回读校验立刻抓住。
    """
    return ((r >> 3) << 3, (g >> 2) << 2, (b >> 3) << 3)


INK = c565(8, 16, 24)          # 底
FRAME = c565(24, 56, 64)       # 圆角边框
WAVE = c565(0, 96, 112)        # 方波
TEXT = c565(200, 216, 224)     # 字

# 5x7 点阵，只用到的九个字符。
GLYPHS = {
    "s": ["01111", "10000", "01110", "00001", "11110", "00000", "00000"],
    "m": ["00000", "11010", "10101", "10101", "10101", "00000", "00000"],
    "_": ["00000", "00000", "00000", "00000", "00000", "00000", "11111"],
    "b": ["10000", "10000", "11110", "10001", "10001", "11110", "00000"],
    "l": ["11000", "01000", "01000", "01000", "01000", "01110", "00000"],
    "i": ["01000", "00000", "11000", "01000", "01000", "01110", "00000"],
    "n": ["00000", "10110", "11001", "10001", "10001", "10001", "00000"],
    "k": ["10000", "10010", "10100", "11000", "10100", "10010", "00000"],
    "y": ["00000", "10001", "10001", "01111", "00001", "01110", "00000"],
}


def in_round_rect(x, y, x0, y0, x1, y1, rad, thick):
    """圆角矩形的一圈边（thick 像素宽）。"""
    cx = min(max(x, x0 + rad), x1 - rad)
    cy = min(max(y, y0 + rad), y1 - rad)
    d = math.hypot(x - cx, y - cy)
    inside = (x0 <= x <= x1) and (y0 <= y <= y1) and (d <= rad + 0.5)
    inner = (x0 + thick <= x <= x1 - thick) and (y0 + thick <= y <= y1 - thick)
    if inner:
        d2 = math.hypot(x - min(max(x, x0 + thick + rad), x1 - thick - rad),
                        y - min(max(y, y0 + thick + rad), y1 - thick - rad))
        inner = d2 <= rad - thick + 0.5
    return inside and not inner


def square_wave(x, y):
    """一条方波：周期 60 px、幅 22 px、线宽 3 px，居中在 y=132 上下。"""
    per = 60.0
    ph = (x % per) / per
    lvl = 1 if ph < 0.5 else 0
    yy = 132 + (0 if lvl else 22)
    return abs(y - yy) <= 1


def logo(x, y):
    if in_round_rect(x, y, 10, 10, W - 11, H - 11, 18, 2):
        return FRAME
    if 46 <= x <= W - 47 and square_wave(x, y):
        return WAVE
    # 字：5x7，放大 4 倍，字符间距 4，居中
    scale, gap = 4, 4
    word = "sm_blinky"
    total = len(word) * (5 * scale + gap) - gap
    x0 = (W - total) // 2
    y0 = 214
    for i, ch in enumerate(word):
        gx = x0 + i * (5 * scale + gap)
        if gx <= x < gx + 5 * scale and y0 <= y < y0 + 7 * scale:
            col = (x - gx) // scale
            row = (y - y0) // scale
            if GLYPHS[ch][row][col] == "1":
                return TEXT
    return INK


rgb = bytearray()
b565 = bytearray()
lossy = 0
for y in range(H):
    for x in range(W):
        r, g, b = logo(x, y)
        if (r, g, b) != c565(r, g, b):
            lossy += 1
            r, g, b = c565(r, g, b)
        rgb += bytes((r, g, b))
        b565 += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))

back = bytearray()
for i in range(0, len(b565), 2):
    v = struct.unpack_from("<H", b565, i)[0]
    back += bytes((((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3))
assert back == rgb, "量化有损失"

png_write(os.path.join(OUT, "bg-logo.png"), rgb)
open(os.path.join(OUT, "bg-logo.rgb565"), "wb").write(bytes(b565))
print("bg-logo.rgb565  %d B  crc32=0x%08X  越格点=%d"
      % (len(b565), zlib.crc32(bytes(b565)) & 0xFFFFFFFF, lossy))
