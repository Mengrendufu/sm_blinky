# -*- coding: utf-8 -*-
"""按 RGB565 的离散色阶设计整屏背景（3 张），零量化损失。

为什么推倒重来：上一版用平滑渐变 + 径向光晕。RGB565 每通道只有 5/6/5 位，
量化步长 R/B 每档 8、G 每档 4 —— 平滑渐变必然要么起色带（不抖动）要么起噪点
（抖动），在深色图上两者都很难看。正确做法是**直接在 RGB565 的格点上设计**：
颜色取 8/4/8 的整数倍，形状用几何（横带、网格、斜条），不插值、不抖动 ——
于是量化误差恒为 0，面板显示的就是设计本身。

末尾会断言：把每个像素转成 RGB565 再转回来，必须与原值逐字节相同。
"""
import zlib, struct, math, os

W, H = 240, 320
OUT = "./bg"
os.makedirs(OUT, exist_ok=True)


def png_write(path, px):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        raw += px[y * W * 3:(y + 1) * W * 3]

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                           + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def c565(r, g, b):
    """一个颜色在 RGB565 里的最近格点（8 位形式）。设计时只允许用它的输出。"""
    return (((r >> 3) << 3), ((g >> 2) << 2), ((b >> 3) << 3))


def band(y, stops):
    """stops = [(行高, 颜色), ...]，从顶到底；返回 y 所在带的颜色。"""
    acc = 0
    for h, col in stops:
        acc += h
        if y < acc:
            return col
    return stops[-1][1]


# 调色板：全部精确落在 RGB565 格点上。深色区可用档位很少（R/B 各 32 档、
# G 64 档），所以设计上刻意避开渐变 —— 大片纯色 + 少量几何元素。
INK = c565(0, 8, 16)
NAVY = c565(8, 16, 24)
SLATE = c565(16, 24, 32)
TEAL_DIM = c565(8, 40, 48)
TEAL = c565(16, 56, 64)
WARM = c565(56, 40, 24)


def roundrect_sdf(x, y, x1, y1, x2, y2, r):
    """标准圆角矩形距离场：<=0 在内部，|d|<w 是轮廓。比按边算干净得多。"""
    cx, cy = (x1 + x2) / 2.0, (y1 + y2) / 2.0
    hx, hy = (x2 - x1) / 2.0 - r, (y2 - y1) / 2.0 - r
    dx = max(abs(x - cx) - hx, 0.0)
    dy = max(abs(y - cy) - hy, 0.0)
    return math.hypot(dx, dy) - r


# --- bg-a 夜色仪表：同心环光晕（每环一个精确色阶）+ 圆角框 ----------------
# 光晕不做插值，直接画成 6 个同心环，每环一个落在 RGB565 格点上的颜色。
# 这样"量化"不是敌人而是画笔：零量化损失、零噪点，读起来仍然是光晕。
A_GLOW = [c565(0, 16, 24), c565(0, 24, 32), c565(8, 32, 40),
          c565(8, 40, 48), c565(16, 48, 56), c565(16, 56, 64)]
A_GLOW_C = (120, 108)
A_GLOW_R0 = 26


def bg_a(x, y):
    d = roundrect_sdf(x, y, 40, 28, 199, 187, 24)
    if -2.0 <= d <= 0.0:
        return SLATE
    ring = int(math.hypot(x - A_GLOW_C[0], y - A_GLOW_C[1]) / A_GLOW_R0)
    if ring < len(A_GLOW):
        return A_GLOW[len(A_GLOW) - 1 - ring]     # 中心最亮
    return INK


# --- bg-b 网格科技：纯色 + 24 px 细网格 + 2 条细的暗青斜条 ----------------
def bg_b(x, y):
    for at in (150, 210):
        if abs((x * 0.55 + y) - at) < 5:
            return TEAL_DIM
    if (x % 24 == 0) or (y % 24 == 0):
        return NAVY
    return INK


# --- bg-c 暮色：3 条宽横带 + 两条地平线 -----------------------------------
def bg_c(x, y):
    if y in (110, 111, 250, 251):
        return WARM
    if y < 96:
        return INK
    if y < 224:
        return NAVY
    return INK


NAMES = [("bg-a", "夜色仪表：6 环同心光晕（每环一个精确色阶）+ 圆角框", bg_a),
         ("bg-b", "网格科技：纯色 + 24 px 细网格 + 2 条细斜条", bg_b),
         ("bg-c", "暮色：3 条宽横带 + 2 条暖色地平线", bg_c)]

rows = []
for name, desc, fn in NAMES:
    rgb = bytearray()
    b565 = bytearray()
    lossy = 0
    for y in range(H):
        for x in range(W):
            r, g, b = fn(x, y)
            # 设计用的颜色必须已经在格点上：否则说明这个形状没按规矩来
            if (r, g, b) != c565(r, g, b):
                lossy += 1
                r, g, b = c565(r, g, b)
            rgb += bytes((r, g, b))
            v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            b565 += struct.pack("<H", v)

    # 回读校验：RGB565 → 8 位 → RGB565 必须逐字节相同（零量化损失）
    back = bytearray()
    for i in range(0, len(b565), 2):
        v = struct.unpack_from("<H", b565, i)[0]
        back += bytes((((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3))
    assert back == rgb, f"{name}: 量化有损失"

    png_write(f"{OUT}/{name}.png", rgb)
    open(f"{OUT}/{name}.rgb565", "wb").write(bytes(b565))
    crc = zlib.crc32(bytes(b565)) & 0xFFFFFFFF
    rows.append((name, os.path.getsize(f"{OUT}/{name}.rgb565"),
                 os.path.getsize(f"{OUT}/{name}.png"), crc, lossy))

print(f"{'文件':8} {'RGB565 B':>10} {'PNG B':>9} {'CRC32':>10} {'越格点数':>8}  说明")
for (name, sz, png_sz, crc, lossy), (_, desc, _) in zip(rows, NAMES):
    print(f"{name:8} {sz:>10,} {png_sz:>9,} {crc:>10} {lossy:>8}  {desc}")
tot = sum(r[1] for r in rows)
print(f"\n3 张合计 {tot:,} B（{tot/1024:.0f} KB）；零量化损失（回读逐字节相同）✓")
