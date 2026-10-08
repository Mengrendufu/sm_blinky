# -*- coding: utf-8 -*-
"""画面布局草图：按 application/ui/thread/ui_disp.c 的真实坐标画。
标签框宽高由字体决定（lv_font_montserrat_14，行高 16），标注为估算。"""
S = 2                      # 放大倍数
W, H = 240, 320
LED = dict(x=84, y=76, w=72, h=72, r=12)     # CENTER (0,-48) → 中心 (120,112)
TITLE = dict(x=88, y=16, w=64, h=16)         # TOP_MID (0,+16)，宽为估算
STATE = dict(x=92, y=196, w=56, h=16)        # CENTER (0,+44) → 中心 (120,204)
EDGES = dict(x=84, y=224, w=72, h=16)        # CENTER (0,+72) → 中心 (120,232)

def frame(led_on, bg, tag, bg_css=None):
    led_c = "#00C853" if led_on else "#303840"
    st = "LED ON" if led_on else "LED OFF"
    bg_css = bg_css or bg
    return f'''
<div class="frame">
  <div class="screen" style="background:{bg_css}">
    <div class="grid"></div>
    <div class="cross" style="left:{120*S}px;top:0;height:{H*S}px"></div>
    <div class="cross" style="top:{160*S}px;left:0;width:{W*S}px"></div>
    <div class="lbl title" style="left:{TITLE['x']*S}px;top:{TITLE['y']*S}px;width:{TITLE['w']*S}px;height:{TITLE['h']*S}px;color:#E0E4E8">sm_blinky</div>
    <div class="box" style="left:{LED['x']*S}px;top:{LED['y']*S}px;width:{LED['w']*S}px;height:{LED['h']*S}px;border-radius:{LED['r']*S}px;background:{led_c}">72×72</div>
    <div class="lbl" style="left:{STATE['x']*S}px;top:{STATE['y']*S}px;width:{STATE['w']*S}px;height:{STATE['h']*S}px;color:#E0E4E8">{st}</div>
    <div class="lbl" style="left:{EDGES['x']*S}px;top:{EDGES['y']*S}px;width:{EDGES['w']*S}px;height:{EDGES['h']*S}px;color:#808890">edges 1234</div>
  </div>
  <div class="cap">{tag}</div>
</div>'''

html = f'''<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>sm_blinky · 画面布局草图（照 ui_disp.c 的 demo）</title>
<style>
 body{{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#f5f5f5;color:#222;margin:0;padding:24px 32px 64px}}
 h1{{font-size:20px;margin:0 0 4px}} .sub{{color:#666;font-size:13px;margin-bottom:20px}}
 h2{{font-size:15px;margin:28px 0 10px;border-bottom:1px solid #ddd;padding-bottom:4px}}
 .row{{display:flex;gap:28px;flex-wrap:wrap;align-items:flex-start}}
 .frame{{text-align:center}}
 .screen{{position:relative;width:{W*S}px;height:{H*S}px;border:1px solid #999;
          box-shadow:0 2px 10px rgba(0,0,0,.18);overflow:hidden}}
 .grid{{position:absolute;inset:0;
        background-image:linear-gradient(to right,rgba(255,255,255,.10) 1px,transparent 1px),
                         linear-gradient(to bottom,rgba(255,255,255,.10) 1px,transparent 1px);
        background-size:{20*S}px {20*S}px}}
 .cross{{position:absolute;background:rgba(90,160,255,.45)}}
 .box{{position:absolute;display:flex;align-items:center;justify-content:center;
       font-size:12px;color:rgba(0,0,0,.55);box-shadow:0 0 0 1px rgba(255,255,255,.25) inset}}
 .lbl{{position:absolute;display:flex;align-items:center;justify-content:center;
       font-size:{7*S}px;font-family:Consolas,monospace;
       border:1px dashed rgba(150,150,150,.75);border-radius:2px}}
 .lbl.title{{letter-spacing:.5px}}
 .cap{{font-size:12px;color:#555;margin-top:6px}}
 table{{border-collapse:collapse;font-size:12.5px;margin-top:6px;width:100%}}
 th,td{{border:1px solid #e3e3e3;padding:3px 8px;text-align:left;vertical-align:top}}
 th{{background:#f2f2f2;font-weight:600}}
 code{{background:#eee;padding:0 4px;border-radius:3px;font-size:12px}}
 pre{{background:#fff;border:1px solid #e3e3e3;border-radius:6px;padding:12px;font-size:12.5px;overflow:auto}}
 .note{{font-size:12.5px;color:#555;line-height:1.7;margin-top:10px}}
 .tag{{display:inline-block;font-size:11px;padding:1px 6px;border-radius:3px}}
 .ok{{background:#e6f4ea;color:#1e7a3c;border:1px solid #b7dfc4}}
 .est{{background:#fff4e5;color:#8a5300;border:1px solid #f0d5ab}}
</style></head><body>
<h1>sm_blinky · 画面布局草图</h1>
<div class="sub">照 <b>application/ui/thread/ui_disp.c</b> 的真实坐标与颜色画（2× 放大，网格 20 px，蓝线是屏幕中心 x=120 / y=160）。<b>标签框宽高由字体决定</b>（lv_font_montserrat_14，行高 16 px），框为估算；方块 72×72 与颜色是精确值。</div>

<h2>① 两个状态并排（同一套控件，只有颜色与文字变）</h2>
<div class="row">
{frame(True,  "#000000", "LED ON —— 方块 #00C853")}
{frame(False, "#000000", "LED OFF —— 方块 #303840")}
</div>

<h2>② 背景三形态（charter §6 已定：纯色 / 垂直渐变 / 整屏图）</h2>
<div class="note">控件一个都不动，只换 screen 的 <code>bg_color</code> / <code>bg_grad_*</code> / <code>bg_image_src</code>。
第三张就是"一次性加载 flash 资源"要供的东西 —— <b>整屏图 240×320×2 = 153,600 B</b>。</div>
<div class="row">
{frame(True, "#000000", "纯色（现状，0 资产）", "#000000")}
{frame(True, "#000000", "垂直渐变（0 资产）", "linear-gradient(to bottom,#101820,#1d3a2a)")}
{frame(True, "#000000", "整屏图（≤3 张，启动期预载）", "repeating-linear-gradient(45deg,#1b2a4a 0 12px,#22345c 12px 24px)")}
</div>

<h2>③ 控件清单（值取自 ui_disp.c）</h2>
<table>
<tr><th>控件</th><th>LVGL 调用</th><th>对齐 / 偏移</th><th>实际像素框</th><th>颜色</th><th>备注</th></tr>
<tr><td>screen</td><td>lv_screen_active()</td><td>—</td><td>(0,0)–(240,320)</td><td>bg #000000<br>opa COVER</td><td>不可滚动；与 BSP 清屏的黑一致，首帧不染色</td></tr>
<tr><td>title</td><td>lv_label</td><td>TOP_MID (0, +16)</td><td>(88,16)–(152,32) <span class="tag est">估算</span></td><td>#E0E4E8</td><td>文本 "sm_blinky"</td></tr>
<tr><td>led</td><td>lv_obj_create</td><td>CENTER (0, −48)</td><td><b>(84,76)–(156,148)</b></td><td>ON #00C853<br>OFF #303840</td><td>72×72，圆角 12，border 0 —— 唯一实心块</td></tr>
<tr><td>state</td><td>lv_label</td><td>CENTER (0, +44)</td><td>(92,196)–(148,212) <span class="tag est">估算</span></td><td>#E0E4E8</td><td>"LED ON" / "LED OFF"</td></tr>
<tr><td>edges</td><td>lv_label</td><td>CENTER (0, +72)</td><td>(84,224)–(156,240) <span class="tag est">估算</span></td><td>#808890</td><td>lv_label_set_text_fmt("edges %u")</td></tr>
</table>

<h2>④ 对象树</h2>
<pre>screen（240×320，bg #000000，opa COVER，去 SCROLLABLE）
├── label  "sm_blinky"     TOP_MID  (0, +16)   #E0E4E8
├── obj    72×72 r=12      CENTER   (0, −48)   #00C853 / #303840
├── label  "LED ON|OFF"    CENTER   (0, +44)   #E0E4E8
└── label  "edges %u"      CENTER   (0, +72)   #808890</pre>

<div class="note">
<b>与刷屏账本的对应</b>：charter §6 记的"方块那块 72×72 flush 0.80 ms、两行字各 flush ≈0.27 ms"就是这三块脏区 —— 方块是唯一整块重绘的对象，所以它占了 flush 的大头。<br>
<b>与 flash 资源的接口点</b>：只有第 ②组第三张（整屏图）需要资产；另外两张是零资产形态，也是资产加载失败时的降级目标。
</div>
</body></html>'''
open('./screen-layout.html','w',encoding='utf-8').write(html)
print("写出 ./screen-layout.html")
