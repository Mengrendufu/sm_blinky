# -*- coding: utf-8 -*-
"""生成存储布局草图（纯 HTML/CSS，比例条 + 精确表）。
mermaid 的 packet-beta 是纵向逐 32 位渲染的，画内存图会变成 47 万 px 高，所以这里手写。"""
KB, MB = 1024, 1024*1024

MAPS = [
 ("① NOR W25Q128（16 MB）分区", "提案 · 数字待定（docs/charter.md:108）", 16*MB, 0x000000,
  [("meta", 4*KB, "#8c8c8c"), ("update A", 256*KB, "#5b8ff9"), ("update B", 256*KB, "#7aa5ff"),
   ("assets", 8*MB, "#f6a623"), ("config", 128*KB, "#5ad8a6"), ("factory", 4*KB, "#7262fd"),
   ("保留", 7725056, "#d9d9d9")],
  "assets 占整片一半；3 张整屏图只用 450 KB = assets 的 5.5%。"),

 ("② 外挂 SRAM IS62WV51216（1 MB @ 0x68000000）", "已核（bsp/devices/bsp_sram.h:14-15）", 1*MB, 0x68000000,
  [("资产区：3 张整屏图", 450*KB, "#f6a623"),
   ("未分配：LVGL 池 / DMA 缓冲（待定）", 1048576-450*KB, "#d9d9d9")],
  "今天只被 bsp.c:54（init）与 blinky.c:154（bench）碰过，基本空闲。一张整屏图 = 240×320×2 = 153,600 B。"),

 ("③ 内部 SRAM（64 KB）对照", "数字待重测（charter.md §6）", 65536, 0x20000000,
  [("已用 bss 58,096 B", 58096, "#e8684a"), ("剩 7,440 B", 7440, "#5ad8a6")],
  "剩 7,440 B，而一张整屏图要 153,600 B = 剩余空间的 20 倍 —— 这就是必须放外挂 SRAM 的全部理由。"),

 ("④ 资产区内部 · 方案 A：固定偏移（最简）", "3 个常量地址，无头部无校验", 460800, None,
  [("图 1", 153600, "#f6a623"), ("图 2", 153600, "#f2c14e"), ("图 3", 153600, "#efd07a")],
  "读失败就只能整块降级为纯色/渐变。总占用 460,800 B。"),

 ("⑤ 资产区内部 · 方案 B：目录表（可扩展）", "头部 + 条目表，4 KB 对齐", 464896, None,
  [("头 64 B", 64, "#8c8c8c"), ("条目表 + 对齐", 4032, "#7262fd"),
   ("图 1", 153600, "#f6a623"), ("图 2", 153600, "#f2c14e"), ("图 3", 153600, "#efd07a")],
  "换来可增删条目、可 CRC 校验、升级不改固件地址；代价比方案 A 多 4 KB。总占用 464,896 B。"),
]

def human(n):
    if n % MB == 0: return f"{n//MB} MB"
    if n >= MB:     return f"{n/MB:.1f} MB"
    if n % KB == 0: return f"{n//KB} KB"
    if n >= KB:     return f"{n/KB:.1f} KB"
    return f"{n} B"

out = ["""<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>sm_blinky · flash 资源加载：存储布局草图</title>
<style>
 body{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#fafafa;color:#222;margin:0;padding:24px 32px 64px}
 h1{font-size:20px;margin:0 0 4px} .sub{color:#666;font-size:13px;margin-bottom:8px}
 h2{font-size:15px;margin:26px 0 4px}
 .meta{font-size:12px;color:#666;margin-bottom:8px}
 .bar{display:flex;height:40px;border-radius:4px;overflow:hidden;border:1px solid #ddd;background:#fff}
 .seg{display:flex;align-items:center;justify-content:center;font-size:11px;color:#fff;
      white-space:nowrap;overflow:hidden;min-width:2px}
 .note{font-size:12.5px;color:#555;margin:8px 0 0;line-height:1.6}
 table{border-collapse:collapse;font-size:12.5px;margin-top:8px;width:100%}
 th,td{border:1px solid #e3e3e3;padding:3px 8px;text-align:left}
 th{background:#f2f2f2;font-weight:600}
 td.num{font-family:Consolas,monospace;text-align:right}
 .sw{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px;vertical-align:-1px}
 .tag{display:inline-block;font-size:11px;padding:1px 6px;border-radius:3px;margin-left:6px}
 .ok{background:#e6f4ea;color:#1e7a3c;border:1px solid #b7dfc4}
 .prop{background:#fff4e5;color:#8a5300;border:1px solid #f0d5ab}
</style></head><body>
<h1>sm_blinky · flash 资源加载 —— 存储布局草图</h1>
<div class="sub">判据来自源码与 docs/charter.md；提案项已标注。用于判断"位置合不合适"，不是最终规格。</div>"""]

for title, meta, total, base, segs, note in MAPS:
    assert sum(s[1] for s in segs) == total, (title, sum(s[1] for s in segs), total)
    out.append(f'<h2>{title}</h2>\n<div class="meta">{meta}</div>')
    out.append('<div class="bar">')
    for name, size, color in segs:
        pct = size/total*100
        label = name if pct >= 9 else ""
        out.append(f'  <div class="seg" style="width:{pct:.4f}%;background:{color}" title="{name} {human(size)}">{label}</div>')
    out.append('</div>')
    out.append('<table><tr><th>段</th><th>起始</th><th>大小</th><th>字节</th><th>占比</th></tr>')
    off = 0
    for name, size, color in segs:
        addr = f"0x{base+off:08X}" if base is not None else f"+0x{off:X}"
        out.append(f'<tr><td><span class="sw" style="background:{color}"></span>{name}</td>'
                   f'<td class="num">{addr}</td><td class="num">{human(size)}</td><td class="num">{size:,}</td>'
                   f'<td class="num">{size/total*100:.2f}%</td></tr>')
        off += size
    out.append(f'<tr><td><b>合计</b></td><td class="num">—</td><td class="num"><b>{human(total)}</b></td>'
               f'<td class="num"><b>{total:,}</b></td><td class="num">100.00%</td></tr></table>')
    out.append(f'<div class="note">{note}</div>')

out.append('</body></html>')
open('./layout.html','w',encoding='utf-8').write("\n".join(out))
print("写出 ./layout.html")
