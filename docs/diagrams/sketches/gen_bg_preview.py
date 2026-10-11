# -*- coding: utf-8 -*-
"""背景图预览：在板资产（3 张 = 2 背景 + 1 开机图）+ e-bike 式布局叠加 + 资产表（槽位/尺寸/CRC/加载耗时）

数字口径（2026-10-12 重算）：PNG B 与 CRC32 直接从 ./bg/*.png|*.rgb565 现文件算；
整载荷 = bg-a + bg-b + bg-logo 三张首尾相接，CRC32 = 0x7BE8D093（与板上 Asset_.crc32 一致）。
"""
S = 2
LAYER = [  # (x,y,w,h,标签,是否不透明挡住背景)
    (0, 0, 240, 24, "顶栏", True),
    (40, 28, 160, 160, "大环 arc 160×160", False),
    (92, 64, 56, 48, "大数字", False),
    (100, 124, 40, 16, "单位", False),
    (12, 204, 104, 92, "卡片 A 电池", True),
    (124, 204, 104, 92, "卡片 B 里程", True),
    (16, 304, 64, 16, "assist", False),
    (176, 304, 48, 16, "帧时间", False),
]
# (文件, 名字, 风格, PNG B, RGB565 B, CRC32, 槽位(None=不在板上), 用途)
BGS = [
    ("bg-a", "夜色仪表", "垂直渐变 + 环区光晕 + 暗角", 2633, 153600, 2632684439, 0,
     "demo 页背景 —— <b>在显</b>"),
    ("bg-b", "网格科技", "24 px 网格 + 斜向光带 + 暗角", 1917, 153600, 4040072013, 1,
     '<span class="no">当前没有任何页面显示</span>'),
    ("bg-logo", "开机展示页", "深底 + 圆角边框 + 方波 + 5×7 点阵字 sm_blinky", 1027, 153600, 504837039, 2,
     "Splash 首帧 —— <b>在显</b>（上电先看它 1.5 s）"),
]

def overlay(show=True):
    if not show: return ""
    o = ""
    for x, y, w, h, tag, opaque in LAYER:
        cls = "lay opaque" if opaque else "lay free"
        o += (f'<div class="{cls}" style="left:{x*S}px;top:{y*S}px;width:{w*S}px;height:{h*S}px"></div>'
              f'<div class="laytag" style="left:{x*S+2}px;top:{y*S+2}px">{tag}</div>')
    return o

def frame(name, caption, show_overlay):
    return (f'<div class="frame"><div class="screen">'
            f'<img src="bg/{name}.png" width="{240*S}" height="{320*S}">'
            f'{overlay(show_overlay)}</div><div class="cap">{caption}</div></div>')

rows = ""
for name, title, desc, png_sz, b565, crc, slot, role in BGS:
    slot_cell = f"槽 {slot}" if slot is not None else "—"
    rows += (f'<tr><td class="num">{slot_cell}</td><td><code>{name}</code></td><td>{title}</td>'
             f'<td>{desc}</td><td class="num">{png_sz:,}</td><td class="num">{b565:,}</td>'
             f'<td class="num">0x{crc:08X}</td><td>{role}</td></tr>')

html = f'''<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>sm_blinky · 整屏资产（板上 3 张 = 2 背景 + 1 开机图）</title>
<style>
 body{{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#f5f5f5;color:#222;margin:0;padding:24px 32px 64px}}
 h1{{font-size:20px;margin:0 0 4px}} .sub{{color:#666;font-size:13px;margin-bottom:18px;line-height:1.75}}
 h2{{font-size:15px;margin:26px 0 10px;border-bottom:1px solid #ddd;padding-bottom:4px}}
 .row{{display:flex;gap:26px;flex-wrap:wrap;align-items:flex-start}}
 .frame{{text-align:center}}
 .screen{{position:relative;width:{240*S}px;height:{320*S}px;border:1px solid #999;
          box-shadow:0 2px 10px rgba(0,0,0,.2);overflow:hidden;background:#000}}
 .screen img{{display:block;image-rendering:pixelated}}
 .lay{{position:absolute;border:1px dashed rgba(255,255,255,.85);border-radius:{6*S}px}}
 .lay.opaque{{background:rgba(255,60,60,.14)}}
 .lay.free{{background:rgba(60,200,120,.12)}}
 .laytag{{position:absolute;font-size:10.5px;font-family:Consolas,monospace;color:#fff;
          background:rgba(0,0,0,.55);padding:0 3px;border-radius:2px;white-space:nowrap}}
 .cap{{font-size:12px;color:#555;margin-top:6px}}
 table{{border-collapse:collapse;font-size:12.5px;margin-top:6px;width:100%}}
 th,td{{border:1px solid #e3e3e3;padding:3px 8px;text-align:left;vertical-align:top}}
 th{{background:#f2f2f2;font-weight:600}} td.num{{text-align:right;font-family:Consolas,monospace}}
 code{{background:#eee;padding:0 4px;border-radius:3px;font-size:12px}}
 .note{{font-size:12.5px;color:#555;line-height:1.8;margin-top:10px}}
 .ok{{background:#e6f4ea;color:#1e7a3c;border:1px solid #b7dfc4;padding:1px 6px;border-radius:3px;font-size:11px}}
 .no{{background:#fdecea;color:#a12a1c;border:1px solid #f2c0b8;padding:1px 6px;border-radius:3px;font-size:11px}}
 .warn{{background:#fff8e1;color:#8a6100;border:1px solid #f0dcab;padding:1px 6px;border-radius:3px;font-size:11px}}
</style></head><body>
<h1>sm_blinky · 整屏资产：板上 3 张（2 背景 + 1 开机图）</h1>
<div class="sub">
布局锚点按 <b>screen-ebike 版</b>生成：顶栏 y0–24、大环 (40,28)–(200,188) 圆心 (120,108)、两张卡 y204–296、底栏 y304–320。
每张 <b>240×320</b>，两种形态：<code>.png</code>（给你看）与 <code>.rgb565</code>（LVGL 原生格式，153,600 B/张）。<br>
<b>boot 期 <code>Asset_load()</code> 把三张<b>全部</b>搬进外挂 SRAM</b>（460,800 B、一次搬运、单 CRC 校验），运行时只有两张被显示：
<b>槽 0</b> 是 demo 页背景、<b>槽 2</b> 是开机展示页，<b>槽 1（bg-b）目前空转</b>。<br>
<span class="ok">绿框=背景露出来的区域</span>（顶栏、环周围、卡片间隙、底栏）；
<span class="no">红框=被不透明控件盖住的区域</span>（两张卡）—— 那部分画得再好也看不见，除非把卡片改成半透明（<code>LV_OPA_80</code>）。
</div>

<h2>① 在板的三张（叠加布局锚点；槽 2 是开机图，不叠）</h2>
<div class="row">
{frame("bg-a", "槽 0 · 夜色仪表 —— demo 页背景（默认）", True)}
{frame("bg-b", "槽 1 · 网格科技 —— 已进 flash 但当前没有页面显示", True)}
{frame("bg-logo", "槽 2 · 开机展示页 —— 上电先看它 1.5 s，不叠布局锚点", False)}
</div>

<h2>② 纯背景（同尺寸，看构图）</h2>
<div class="row">
{frame("bg-a", "bg-a · 槽 0（在显）", False)}
{frame("bg-b", "bg-b · 槽 1（在板未显）", False)}
{frame("bg-logo", "bg-logo · 槽 2（开机图）", False)}
</div>

<h2>③ 资产表（槽位 / 尺寸 / 单张 CRC / 用途）</h2>
<table>
<tr><th>槽</th><th>文件</th><th>名字</th><th>风格</th><th>PNG B</th><th>RGB565 B</th><th>单张 CRC32</th><th>用途（2026-10-12）</th></tr>
{rows}
<tr style="background:#f0f7f0"><td colspan="5"><b>板上载荷 = 槽 0+1+2 首尾相接</b>（bg-a + bg-b + bg-logo）</td>
<td class="num"><b>460,800</b></td><td class="num"><b>0x7BE8D093</b></td><td>整载荷一次搬运、单 CRC 校验（<code>asset.c</code>）</td></tr>
</table>
<div class="note">
线速账（18 MHz SPI、每张 153,600 B）：<b>68.3 ms/张、三张 204.8 ms</b>；实测（2026-10-10，<code>BSP_dbgBootMs_</code> 标记差）：
payload 读 <b>263 ms</b>（1.75 MB/s，差的 ~58 ms 是分块开销；块已试到 32→64 KB 上限、无改善 ✗）+ CRC32 <b>205 ms</b>（逐字节）⇒
<b>资产段合计 468 ms</b>，加面板段 411 ms ⇒ 暖复位到资产就位 <b>879 ms</b>。<br>
<span class="warn">可压的两处</span>：① 槽 1 目前不显示，去掉它可省 ~155 ms；② CRC 要提速得动 <code>3rd_party/common_c</code>。
</div>

<h2>④ flash 里存什么格式（2026-10-08 已定案 = B1；B2/B3 评估后未采用，留作对照）</h2>
<table>
<tr><th>方案</th><th>flash 占用</th><th>启动加载</th><th>LVGL 侧</th><th>代价</th></tr>
<tr style="background:#f0f7f0"><td><b>B1 · 裸 RGB565（采用 ✓）</b></td><td>460,800 B（450 KB）</td><td>204.8 ms 线速 / 实测 263 ms</td>
<td>SRAM 里就是原生格式，<code>lv_image_dsc_t</code> 直接指过去，<b>零解码</b></td><td>flash 占 450 KB</td></tr>
<tr><td>B2 · RLE 压缩 + 启动解压到 SRAM</td><td>≈ 12–28 KB/张，合 ≈ 60 KB</td>
<td>flash 读 ≈ 27 ms + 解压（未实测）</td><td>解压后同样是 SRAM 里的原生 RGB565，LVGL <b>无感</b></td>
<td>要在 app 里写解压；多一段 CPU 时间</td></tr>
<tr><td>B3 · 让 LVGL 直接读压缩资产</td><td>≈ 60 KB</td><td>0（不预载）</td>
<td>LVGL 每帧逐像素解码</td><td><span class="no">不建议</span>每帧解码 + 混合，刷新会被拖垮</td></tr>
</table>
<div class="note">
<span class="warn">RLE 游程数（4,150 / 9,551，bg-a / bg-b）是按"相邻像素相同"直接数的，没有真实编码器；B2 若真做还要算长度字段与解码耗时。</span>
</div>

<h2>⑤ 文件在哪 / 怎么烧 / 怎么核</h2>
<div class="note">
图源与产物：在板三张 = <code>bg-a</code> / <code>bg-b</code> / <code>bg-logo</code>，各一份 <code>.png</code>（看）与 <code>.rgb565</code>（LVGL 原生，小端）。<br>
生成器：<code>gen_bg.py</code>（三张背景）+ <code>gen_logo.py</code>（开机图），都在本目录，纯标准库；本页由 <code>gen_bg_preview.py</code> 生成。<br>
烧写：<code>tools/burn-asset.py</code>（Windows 侧，CH340 = COM12）把三槽拼成 460,800 B 经 bootloader 写进 NOR <code>0x00081000</code>。<br>
核验（三方对齐）：烧完读 app 的 <code>Asset_.crc32</code>（J-Link）＝ SRAM 槽头 ＝ 仓库拼出来的 <code>0x7BE8D093</code> ✓。
</div>
</body></html>'''
open('./bg-preview.html','w',encoding='utf-8').write(html)
print("写出 ./bg-preview.html")
