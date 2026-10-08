# -*- coding: utf-8 -*-
"""背景图预览：3 张图 + e-bike 式控件布局叠加 + 资产表（尺寸/CRC/加载耗时）"""
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
BGS = [
 ("bg-a", "夜色仪表", "垂直渐变 + 环区光晕 + 暗角", 10778, 153600, "3950042814"),
 ("bg-b", "网格科技", "24 px 网格 + 斜向光带 + 暗角", 19716, 153600, "3218318435"),
 ("bg-c", "暮色（准照片）", "斜向渐变 + 冷暖光斑 + 重暗角", 26122, 153600, "2157343619"),
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
for name, title, desc, png_sz, b565, crc in BGS:
    rows += (f'<tr><td><code>{name}</code></td><td>{title}</td><td>{desc}</td>'
             f'<td class="num">{png_sz:,}</td><td class="num">{b565:,}</td><td class="num">{crc}</td>'
             f'<td class="num">{b565*8/18e6*1000:.1f} ms</td></tr>')

html = f'''<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>sm_blinky · 整屏背景图（3 张预备资产）</title>
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
<h1>sm_blinky · 整屏背景图（3 张预备资产）</h1>
<div class="sub">
按 <b>screen-ebike 版布局</b>的锚点生成：顶栏 y0–24、大环 (40,28)–(200,188) 圆心 (120,108)、两张卡 y204–296、底栏 y304–320。
每张 <b>240×320</b>，同时导出两种形态：<code>.png</code>（给你看）与 <code>.rgb565</code>（LVGL 原生格式，153,600 B）。<br>
<span class="ok">绿框=背景露出来的区域</span>（顶栏、环周围、卡片间隙、底栏）；
<span class="no">红框=被不透明控件盖住的区域</span>（两张卡）—— 那部分画得再好也看不见，除非把卡片改成半透明（<code>LV_OPA_80</code>）。
</div>

<h2>① 三张背景（叠加布局锚点）</h2>
<div class="row">
{frame("bg-a", "夜色仪表 —— 默认主页面候选", True)}
{frame("bg-b", "网格科技 —— 纹理感更强", True)}
{frame("bg-c", "暮色准照片 —— charter 说的『照片级』少数页面", True)}
</div>

<h2>② 纯背景（同尺寸，看构图）</h2>
<div class="row">
{frame("bg-a", "bg-a", False)}
{frame("bg-b", "bg-b", False)}
{frame("bg-c", "bg-c", False)}
</div>

<h2>③ 资产表（可直接当"目录表"的内容）</h2>
<table>
<tr><th>文件</th><th>名字</th><th>风格</th><th>PNG B</th><th>RGB565 B</th><th>CRC32</th><th>18 MHz 加载</th></tr>
{rows}
<tr style="background:#f0f7f0"><td colspan="4"><b>3 张合计</b></td><td class="num"><b>460,800</b></td><td>——</td>
<td class="num"><b>204.8 ms</b></td></tr>
</table>

<h2>④ 存进 flash 的两种格式（这是加载路径要定的一个点）</h2>
<table>
<tr><th>方案</th><th>flash 占用</th><th>启动加载</th><th>LVGL 侧</th><th>代价</th></tr>
<tr><td><b>B1 · 裸 RGB565</b></td><td>460,800 B（450 KB）</td><td>204.8 ms（18 MHz 直读搬运）</td>
<td>SRAM 里就是原生格式，<code>bg_image_src</code> 直接指过去，<b>零解码</b></td><td>flash 占 450 KB；搬运期间 CPU 也在忙</td></tr>
<tr><td><b>B2 · RLE 压缩 + 启动解压到 SRAM</b></td><td>≈ 12–28 KB/张，<b>合 ≈ 60 KB</b>（实测游程 4,150 / 9,551 / 9,078）</td>
<td>flash 读 ≈ 27 ms + 解压（待实测）</td><td>解压后同样是 SRAM 里的原生 RGB565，LVGL <b>无感</b></td>
<td>要在 app 里写解压；比 B1 多一段 CPU 时间</td></tr>
<tr><td><b>B3 · 让 LVGL 直接读压缩资产</b></td><td>≈ 60 KB</td><td>0（不预载）</td>
<td>LVGL 每帧逐像素解码</td><td><span class="no">不建议</span>每帧解码 + 混合，刷新会被拖垮</td></tr>
</table>
<div class="note">
B2 的关键性质：<b>压缩只发生在 flash 里，SRAM 里永远是原生格式</b> —— 所以"启动期一次性搬进外挂 SRAM、之后切屏恒定命中"这条不变，只是搬运前多一步解压。<br>
<span class="warn">待实测</span>：本机没有真实 RLE 编码器，游程数是我按"相邻像素相同"直接数的；真正的 RLE 还要算长度字段开销（每段约 1–3 B）与解码耗时。
</div>

<h2>⑤ 文件在哪</h2>
<div class="note">
<code>./bg/</code> —— <code>bg-a.png</code> / <code>bg-a.rgb565</code>（小端 RGB565）等共 6 个文件；
生成脚本 <code>/tmp/gen_bg.py</code>（纯标准库，改颜色重跑即可）。
</div>
</body></html>'''
open('./bg-preview.html','w',encoding='utf-8').write(html)
print("写出 ./bg-preview.html")
