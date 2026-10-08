# -*- coding: utf-8 -*-
"""画面布局 v3：照 lvgl/lv_demos 的 e-bike demo 结构，改造成 240×320 纵向。
事实来源：lv_demos/src/ebike/lv_demo_ebike_home.c 与 lv_demo_ebike_private.h（联网取得）
          + 本仓库 lv_conf.h / ELF 符号（本地实测）"""
import math
S = 2; W, H = 240, 320
TURQ, LIME, BG, CARD, DIM = "#55FFEB", "#91FF3B", "#0d1117", "#161b22", "#1f2630"
RENDER, FLUSH, PERIOD = 1.2, 0.16, 33.0

def ticks():
    out = []
    for i in range(9):
        a = math.radians(150 + i * 30)          # 150° → 390°，240° 扫角
        r = 84
        x = 120 + r*math.cos(a); y = 108 + r*math.sin(a)
        out.append(f'<div class="tick" style="left:{(x-2)*S}px;top:{(y-6)*S}px;'
                   f'width:{4*S}px;height:{12*S}px;transform:rotate({150+i*30+90}deg)"></div>')
    return "".join(out)

def screen(led_on, val, caption, dirty=False):
    led = TURQ if led_on else "#39404a"
    pct = val
    a0, sweep = 150, 240
    fill = sweep * pct / 100
    d = ""
    if dirty:
        for (x, y, w, h, tag) in [
            (40, 28, 160, 160, "整环失效仅在跳变 >180°：160×160 ≈ 34.8 ms"),
            (92, 64, 56, 48, "大数字 56×48 ≈ 3.7 ms"),
            (20, 260, 88, 16, "卡片 bar 88×16 ≈ 1.9 ms"),
            (8, 8, 10, 10, "led 8×8 ≈ 0.09 ms"),
        ]:
            d += (f'<div class="dirty" style="left:{x*S}px;top:{y*S}px;width:{w*S}px;height:{h*S}px"></div>'
                  f'<div class="dirtag" style="left:{x*S+2}px;top:{y*S+2}px">{tag}</div>')
    return f'''
<div class="frame"><div class="screen">
  <div class="w topbar"></div>
  <div class="w" style="left:{8*S}px;top:{8*S}px;width:{8*S}px;height:{8*S}px;border-radius:50%;background:{led}"></div>
  <div class="w lbl" style="left:{20*S}px;top:{4*S}px;width:{48*S}px;height:{16*S}px;color:#E0E4E8;font-size:{7*S}px">blinky</div>
  <div class="w" style="left:{196*S}px;top:{10*S}px;width:{5*S}px;height:{5*S}px;border-radius:50%;background:{DIM}"></div>
  <div class="w" style="left:{206*S}px;top:{10*S}px;width:{5*S}px;height:{5*S}px;border-radius:50%;background:{DIM}"></div>
  <div class="w" style="left:{216*S}px;top:{9*S}px;width:{7*S}px;height:{7*S}px;border-radius:50%;background:{TURQ}"></div>
  <div class="ring" style="left:{40*S}px;top:{28*S}px;width:{160*S}px;height:{160*S}px;
       background:conic-gradient(from {a0}deg, {TURQ} 0 {fill}deg, {DIM} {fill}deg {sweep}deg, transparent {sweep}deg 360deg)">
    <div class="hole"></div></div>
  {ticks()}
  <div class="w lbl" style="left:{92*S}px;top:{64*S}px;width:{56*S}px;height:{48*S}px;color:#E0E4E8;font-size:{24*S}px">{val}</div>
  <div class="w lbl" style="left:{100*S}px;top:{124*S}px;width:{40*S}px;height:{16*S}px;color:#808890;font-size:{7*S}px">km/h</div>
  <div class="card" style="left:{12*S}px;top:{204*S}px;width:{104*S}px;height:{92*S}px;background:{CARD}">
    <div class="w lbl" style="left:{8*S}px;top:{10*S}px;width:{88*S}px;height:{16*S}px;color:#E0E4E8;font-size:{7*S}px">BAT {val}%</div>
    <div class="bar" style="left:{8*S}px;top:{56*S}px;width:{88*S}px;height:{16*S}px">
      <div style="width:{val}%;height:100%;border-radius:{8*S}px;background:{LIME}"></div></div>
  </div>
  <div class="card" style="left:{124*S}px;top:{204*S}px;width:{104*S}px;height:{92*S}px;background:{TURQ}">
    <div class="w lbl" style="left:{8*S}px;top:{10*S}px;width:{88*S}px;height:{32*S}px;color:{BG};font-size:{14*S}px">12.4</div>
    <div class="w lbl" style="left:{8*S}px;top:{50*S}px;width:{48*S}px;height:{16*S}px;color:{BG};font-size:{7*S}px">km</div>
  </div>
  <div class="w lbl" style="left:{16*S}px;top:{304*S}px;width:{64*S}px;height:{16*S}px;color:#808890;font-size:{7*S}px">assist 2</div>
  <div class="w lbl" style="left:{176*S}px;top:{304*S}px;width:{48*S}px;height:{16*S}px;color:#808890;font-size:{7*S}px">12.3 ms</div>
  {d}
</div><div class="cap">{caption}</div></div>'''

DIRTY = [
 ("arc 值动画（增量 ≤180°）", "只失效<b>变化扇区</b>：半径 74、弧宽 12、每帧 ~2° → ≈31 px", 31, "<code>lv_arc.c:900</code> lv_obj_invalidate_area()", "✓✓ 大环也能连续动画"),
 ("arc 跳变 >180°（如 100→0）", "整控件 160×160", 160*160, "<code>lv_arc.c:175</code> lv_obj_invalidate()", "✗ 只做一次性过渡"),
 ("大数字 label", "56×48", 56*48, "文本重绘", "✓"),
 ("卡片 bar", "88×16", 88*16, "值变化", "✓✓"),
 ("顶栏 lv_led", "8×8", 64, "呼吸灯", "✓✓"),
 ("刻度 obj ×9（若随值转）", "4×12 ×9", 4*12*9, "transform_rotation", "✓"),
 ("整屏（对照）", "240×320", 240*320, "——", "✗ 不可能"),
]
rows = ""
for name, how, px, basis, verdict in DIRTY:
    us = px*(RENDER+FLUSH)
    rows += (f'<tr><td>{name}</td><td>{how}</td><td class="num">{px:,}</td>'
             f'<td class="num">{us/1000:.2f} ms</td><td class="num">{us/1000/PERIOD*100:.1f}%</td>'
             f'<td>{basis}</td><td>{verdict}</td></tr>')
rows += ('<tr style="background:#f0f7f0"><td><b>合计：弧扇区 + 大数字 + 卡片 bar 同时动</b></td><td>——</td>'
         f'<td class="num"><b>{31+56*48+88*16:,}</b></td>'
         f'<td class="num"><b>{(31+56*48+88*16)*(RENDER+FLUSH)/1000:.1f} ms</b></td>'
         f'<td class="num"><b>{(31+56*48+88*16)*(RENDER+FLUSH)/1000/PERIOD*100:.0f}%</b></td>'
         '<td>——</td><td><b>✓ 舒服</b></td></tr>')

html = f'''<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>sm_blinky · 画面布局 v3（照 LVGL e-bike demo 改造）</title>
<style>
 body{{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#f5f5f5;color:#222;margin:0;padding:24px 32px 64px}}
 h1{{font-size:20px;margin:0 0 4px}} .sub{{color:#666;font-size:13px;margin-bottom:18px;line-height:1.7}}
 h2{{font-size:15px;margin:26px 0 10px;border-bottom:1px solid #ddd;padding-bottom:4px}}
 .row{{display:flex;gap:28px;flex-wrap:wrap;align-items:flex-start}}
 .frame{{text-align:center}}
 .screen{{position:relative;width:{W*S}px;height:{H*S}px;background:{BG};border:1px solid #999;
          box-shadow:0 2px 10px rgba(0,0,0,.18);overflow:hidden}}
 .w{{position:absolute;display:flex;align-items:center;justify-content:center;font-family:Consolas,monospace}}
 .lbl{{border:1px dashed rgba(150,150,150,.35);border-radius:2px}}
 .topbar{{left:0;top:0;width:{W*S}px;height:{24*S}px;background:#080c11;border-bottom:1px solid #1f2630}}
 .ring{{position:absolute;border-radius:50%;display:flex;align-items:center;justify-content:center}}
 .hole{{width:136/160*100%;height:136/160*100%;border-radius:50%;background:{BG}}}
 .tick{{position:absolute;background:#39404a;border-radius:1px}}
 .card{{position:absolute;border-radius:{12*S}px;overflow:hidden}}
 .bar{{position:absolute;background:{DIM};border-radius:{8*S}px;overflow:hidden}}
 .dirty{{position:absolute;border:2px dashed rgba(255,60,60,.9);background:rgba(255,60,60,.10)}}
 .dirtag{{position:absolute;font-size:10.5px;color:#ffdede;background:rgba(180,20,20,.88);
          padding:0 4px;border-radius:2px;font-family:Consolas,monospace;white-space:nowrap}}
 .cap{{font-size:12px;color:#555;margin-top:6px}}
 table{{border-collapse:collapse;font-size:12.5px;margin-top:6px;width:100%}}
 th,td{{border:1px solid #e3e3e3;padding:3px 8px;text-align:left;vertical-align:top}}
 th{{background:#f2f2f2;font-weight:600}} td.num{{text-align:right;font-family:Consolas,monospace}}
 code{{background:#eee;padding:0 4px;border-radius:3px;font-size:12px}}
 .note{{font-size:12.5px;color:#555;line-height:1.75;margin-top:10px}}
 .ok{{background:#e6f4ea;color:#1e7a3c;border:1px solid #b7dfc4;padding:1px 6px;border-radius:3px;font-size:11px}}
 .no{{background:#fdecea;color:#a12a1c;border:1px solid #f2c0b8;padding:1px 6px;border-radius:3px;font-size:11px}}
 .warn{{background:#fff8e1;color:#8a6100;border:1px solid #f0dcab;padding:1px 6px;border-radius:3px;font-size:11px}}
</style></head><body>
<h1>sm_blinky · 画面布局 v3 —— 照 LVGL e-bike demo 改造</h1>
<div class="sub">
参照 <b>lvgl/lv_demos · src/ebike/</b>（本地 <code>3rd_party/lvgl/demos/</code> 里没有，联网取的原码）：它的 home 页是
<b>左=大环（lv_arc 660×660、弧宽 20、knob 透明）+ 环心大数字（两个 roller + font_ebike_130）+ 刻度（9 个 obj 靠 transform_rotation 旋转）</b>，
<b>右=电池条（lv_bar）+ 里程卡（圆角 12、turquoise 底、大/小 label）</b>，另有顶栏 3 个图标、页面点 bullets、Lottie 动画。<br>
<b>本版改造</b>：保留结构，按 240×320 纵向重排（环在上、卡片在下 —— demo 自己的纵向分支也是这么排的）；配色沿用 demo 的
<code>EBIKE_COLOR_TURQUOISE 0x55FFEB</code> / <code>LIME 0x91FF3B</code>。blinky 缩成顶栏一个 8×8 状态灯。
</div>

<h2>① 主屏（运行中 · 70%）</h2>
<div class="row">{screen(True, 70, "顶栏 + 大环 + 环心大数字 + 两张卡 + 底栏")}</div>

<h2>② 另一状态（停止 · 12%，同布局换色换值）</h2>
<div class="row">{screen(False, 12, "同一套控件；低电时 BAT 与环同步降")}</div>

<h2>③ 脏区（动画预算）</h2>
<div class="row">{screen(True, 70, "红框=各部分动画时要重绘的区域", dirty=True)}</div>
<table>
<tr><th>部件</th><th>每帧脏区</th><th>px</th><th>估算/帧</th><th>33 ms 周期</th><th>依据</th><th>结论</th></tr>
{rows}
</table>
<div class="note">
<b>这一版推翻了我上一版的悲观估算</b>：我原来按"弧动画要重绘整个 160×160"算（34.8 ms ✗），实测读源码发现
<code>lv_arc</code> 只有在角度增量 <b>&gt;180°</b> 时才整块失效（<code>lv_arc.c:175</code>），平滑动画走的是
<code>lv_obj_invalidate_area()</code> 只失效<b>变化扇区</b>（<code>lv_arc.c:900</code>）→ 每帧只有几十像素。<br>
结论：<b>e-bike 那种大环 + 大数字的观感，在这颗片子 240×320 上是能连续动画的</b>（弧扇区 + 大数字 + 进度条同时动 ≈ 5.7 ms ≈ 17%）。
唯一禁区是"跳变 &gt;180°"和整屏动画。
</div>

<h2>④ 结构映射：e-bike demo → 我们</h2>
<table>
<tr><th>demo 部件</th><th>demo 真实值</th><th>我们 240×320</th><th>可用性</th></tr>
<tr><td>速度环 <code>lv_arc</code></td><td>660×660 / 弧宽 20 / knob 透明 / 指示色 turquoise</td><td><b>160×160 @ (40,28)</b>，弧宽 12，扫角 240°</td><td><span class="ok">今天可用</span>（<code>LV_USE_ARC 1</code>）</td></tr>
<tr><td>刻度环</td><td><code>lv_image</code> 刻度图 + 9 个 obj 旋转（pivot 390，-240°+i×135°）</td><td>9 个 4×12 obj @ 半径 84，150°+i×30°</td><td><span class="ok">今天可用</span>（<code>lv_draw_sw_transform</code> 已在镜像里）</td></tr>
<tr><td>环心大数字</td><td>两个 <code>lv_roller</code>（个位/十位）+ <code>font_ebike_130</code></td><td>两个 label，48 px 字体</td><td><span class="no">要开</span> <code>LV_FONT_MONTSERRAT_48</code>（现在只有 <code>_14</code>）</td></tr>
<tr><td>电池条</td><td><code>lv_bar</code> 100%×100%、border 4、radius 12、indicator lime</td><td>卡片内 88×16 横条，indicator lime</td><td><span class="ok">今天可用</span>（BAR 未在 lv_conf.h 里写 0 → 默认 1）</td></tr>
<tr><td>里程卡</td><td>radius 12 + turquoise 底 + 大/小 label</td><td>104×92 卡，28 px 数字 + 14 px 单位</td><td><span class="warn">数字要大字号</span> → 同上的字体开关</td></tr>
<tr><td>顶栏 3 图标</td><td><code>lv_image</code> ×3</td><td>1 个 <code>lv_led</code>（blinky）+ 文字</td><td><span class="ok">今天可用</span>（LED 默认 1）；图标要 <code>LV_USE_IMAGE 1</code></td></tr>
<tr><td>页面点 bullets</td><td>5×5 obj、radius CIRCLE、选中 opacity COVER + 放大 2 px</td><td>3 个同款点</td><td><span class="ok">今天可用</span></td></tr>
<tr><td>数据刷新</td><td><code>lv_subject</code> 观察者（<code>lv_subject_add_observer</code>）</td><td>沿用 <code>ui_disp.c</code> 现有的"值变才刷"setter</td><td><span class="warn">不跟</span> <code>LV_USE_OBSERVER 0</code>，没必要为它开</td></tr>
<tr><td>Lottie 骑手动画</td><td>thorvg 矢量 + <code>LV_USE_LOTTIE</code></td><td><b>不做</b></td><td><span class="no">不开</span> 需要整套矢量图形栈</td></tr>
</table>

<h2>⑤ 要用非默认部件时必须开的开关</h2>
<table>
<tr><th>开关</th><th>现状</th><th>为什么</th><th>代价</th></tr>
<tr><td><code>LV_FONT_MONTSERRAT_48</code> / <code>_28</code></td><td>只有 <code>_14</code>（ELF 实测）</td><td>环心大数字、卡片数字</td><td>字体点阵进 flash，<b>要实测</b></td></tr>
<tr><td><code>LV_USE_IMAGE</code></td><td><b>0</b>（lv_conf.h:78）</td><td>刻度图/图标（若走图片路线）</td><td>解码器 + 资产</td></tr>
<tr><td><code>LV_USE_ROLLER</code></td><td><b>0</b>（lv_conf.h:84）</td><td>demo 的数字轮；用 label 可免</td><td>可省</td></tr>
<tr><td><code>LV_USE_SCALE</code></td><td><b>0</b>（lv_conf.h:85）</td><td>更省事的刻度控件；本版用 obj 旋转代替</td><td>可省</td></tr>
<tr><td><code>LV_USE_SLIDER/SWITCH/SPINNER/CHART</code></td><td><b>0</b></td><td>本版不用</td><td>——</td></tr>
</table>
<div class="note">
估算系数仍取自 charter §6 实测（渲染 1.2 µs/px、刷屏 0.16 µs/px），周期取 <code>LV_DEF_REFR_PERIOD = 33 ms</code>。
<span class="warn">待上板核定</span>：弧扇区失效的画法比"整块填充"更贵（遮罩 + 线），实机请按 <code>[TIME][BLK]</code>（RecID 41）复测这一行。
</div>
</body></html>'''
open('./screen-ebike.html','w',encoding='utf-8').write(html)
print("写出 ./screen-ebike.html")
