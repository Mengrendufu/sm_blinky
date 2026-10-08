# -*- coding: utf-8 -*-
"""按 e-bike 式布局生成 240×320 整屏背景图（3 张），并输出资产表所需信息。
纯标准库：自己写 PNG（zlib），同时导出 RGB565 裸数据 + CRC32。
布局锚点（来自 screen-ebike 版）：顶栏 y0-24、大环 (40,28)-(200,188) 中心 (120,108) 半径 74
                           两张卡 y204-296、底栏 y304-320
"""
import zlib, struct, math, os, binascii

W, H = 240, 320
OUT = "./bg"
os.makedirs(OUT, exist_ok=True)

def png_write(path, px):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        raw += px[y*W*3:(y+1)*W*3]
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                           + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))

def clamp(v): return 0 if v < 0 else (255 if v > 255 else int(v))
def mix(a, b, t): return tuple(a[i] + (b[i]-a[i])*t for i in range(3))
def blob(x, y, cx, cy, r, color, alpha):
    d = math.hypot(x-cx, y-cy)
    if d >= r: return None
    t = (1 - d/r)**2 * alpha
    return (color, t)

def bg_a(x, y):                      # 夜色仪表：垂直渐变 + 环区光晕 + 暗角
    c = list(mix((10,14,20), (22,30,42), y/H))
    g = blob(x, y, 120, 108, 120, (85,255,235), 0.10)
    if g: c = list(mix(c, g[0], g[1]))
    v = math.hypot((x-120)/120, (y-160)/160)
    c = list(mix(c, (4,6,10), max(0.0, (v-0.75))*1.4))
    return c

def bg_b(x, y):                      # 网格科技：深底 + 24px 网格 + 斜向光带
    c = list(mix((11,15,21), (16,22,30), y/H))
    if x % 24 == 0 or y % 24 == 0: c = list(mix(c, (34,46,60), 0.55))
    band = abs((x*0.55 + y) - 190)
    if band < 34:
        c = list(mix(c, (85,255,235), 0.07*(1-band/34)))
    v = math.hypot((x-120)/120, (y-160)/160)
    c = list(mix(c, (3,5,8), max(0.0, (v-0.72))*1.5))
    return c

def bg_c(x, y):                      # 暮色（准照片感）：斜向渐变 + 冷暖光斑 + 重暗角
    c = list(mix((28,44,78), (11,18,32), (x*0.35 + y*0.65)/H))
    for b in (blob(x, y, 62, 66, 120, (255,178,120), 0.22),
              blob(x, y, 186, 244, 130, (85,255,235), 0.14)):
        if b: c = list(mix(c, b[0], b[1]))
    v = math.hypot((x-120)/120, (y-160)/170)
    c = list(mix(c, (5,7,12), max(0.0, (v-0.62))*1.6))
    return c

NAMES = [("bg-a", "夜色仪表（垂直渐变 + 环区光晕）", bg_a),
         ("bg-b", "网格科技（24 px 网格 + 斜向光带）", bg_b),
         ("bg-c", "暮色准照片（冷暖光斑 + 暗角）", bg_c)]

rows = []
for name, desc, fn in NAMES:
    rgb = bytearray()
    for y in range(H):
        for x in range(W):
            r, g, b = (clamp(v) for v in fn(x, y))
            rgb += bytes((r, g, b))
    png_write(f"{OUT}/{name}.png", rgb)
    # RGB565 小端（LVGL 的原生格式）
    b565 = bytearray()
    for i in range(0, len(rgb), 3):
        r, g, b = rgb[i], rgb[i+1], rgb[i+2]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        b565 += struct.pack("<H", v)
    open(f"{OUT}/{name}.rgb565", "wb").write(bytes(b565))
    crc = zlib.crc32(bytes(b565)) & 0xFFFFFFFF
    png_sz = os.path.getsize(f"{OUT}/{name}.png")
    # RLE 估算（LVGL 支持 RLE 压缩资产）：统计相同像素游程
    runs, prev, n = 0, None, 0
    for i in range(0, len(b565), 2):
        pxv = b565[i:i+2]
        if pxv == prev: n += 1
        else:
            if prev is not None: runs += 1
            prev, n = pxv, 1
    runs += 1
    rows.append((name, desc, os.path.getsize(f"{OUT}/{name}.rgb565"), png_sz, crc, runs))

print(f"{'文件':10} {'RGB565 B':>10} {'PNG B':>9} {'CRC32':>10} {'游程数':>8}  说明")
for name, desc, sz, png_sz, crc, runs in rows:
    print(f"{name:10} {sz:>10,} {png_sz:>9,} {crc:>10} {runs:>8,}  {desc}")
tot = sum(r[2] for r in rows)
print(f"\n3 张合计 {tot:,} B（{tot/1024:.0f} KB）；18 MHz SPI 单张 ≈ {tot/3*8/18e6*1000:.1f} ms，三张 ≈ {tot*8/18e6*1000:.0f} ms")
