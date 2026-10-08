#!/usr/bin/env python3
"""mermaid 序列图源码 -> 自包含交互式 HTML 查看器。

为什么不是直接在对话里贴 mermaid 源码：
OpenCode 把 mermaid 渲染成一张固定尺寸的图片贴进对话，横向能滚、纵向不能，
长序列图必然被裁。所以一律落成 HTML 文件，在浏览器里看。

用法:
    mmd2html.py seq.mmd                       # 输出到同目录 seq.html
    mmd2html.py seq.mmd -o /mnt/c/tmp_atk/x.html
    mmd2html.py seq.mmd --title "BSP 初始化时序"

mermaid.min.js 的查找顺序（找到即用，都不在则从 CDN 取一份并缓存）:
    1. --mermaid 指定的路径
    2. 输出目录里已有的 mermaid.min.js
    3. 缓存目录 ~/.cache/mermaid-master/mermaid.min.js
    4. https://cdn.jsdelivr.net/npm/mermaid@11.17.2/dist/mermaid.min.js
"""
import argparse
import os
import pathlib
import shutil
import sys
import urllib.request
import html as _html  # 插入模板前转义源码，见下方注释

MERMAID_VERSION = "11.17.2"
MERMAID_URL = (
    f"https://cdn.jsdelivr.net/npm/mermaid@{MERMAID_VERSION}"
    "/dist/mermaid.min.js"
)
CACHE_DIR = pathlib.Path.home() / ".cache" / "mermaid-master"
CACHE_FILE = CACHE_DIR / "mermaid.min.js"

HERE = pathlib.Path(__file__).resolve().parent
TEMPLATE = HERE / "assets" / "viewer.html.tpl"

# 紧凑配置：默认配置画出来是 2403x1984，这个配置是 2189x1625（实测，省 18% 高）。
# 写进图源码而不是只写进查看器的 JS —— 这样任何 mermaid 渲染器都吃到。
DIRECTIVE = (
    '%%{init: {"sequence": {"mirrorActors": false, "messageMargin": 22, '
    '"noteMargin": 5, "boxMargin": 5, "actorMargin": 36, '
    '"diagramMarginX": 6, "diagramMarginY": 6}}}%%'
)


def find_mermaid(explicit, out_dir):
    """返回 (路径, 是否已有)。都不在时下载到缓存并返回。"""
    if explicit:
        p = pathlib.Path(explicit).expanduser()
        if not p.is_file():
            sys.exit(f"[mmd2html] --mermaid 指向的文件不存在: {p}")
        return p, True

    beside = out_dir / "mermaid.min.js"
    if beside.is_file():
        return beside, True

    if CACHE_FILE.is_file():
        return CACHE_FILE, True

    print(f"[mmd2html] 本地没有 mermaid.min.js，从 CDN 取 v{MERMAID_VERSION} …")
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(MERMAID_URL, timeout=120) as r:
            data = r.read()
    except Exception as exc:  # noqa: BLE001 - 网络问题原样报给用户
        sys.exit(f"[mmd2html] 下载失败: {exc}\n"
                 f"           离线环境请先用 --mermaid 指定本地的 mermaid.min.js")
    CACHE_FILE.write_bytes(data)
    print(f"[mmd2html] 已缓存 {len(data) / 1048576:.1f} MB -> {CACHE_FILE}")
    return CACHE_FILE, True


def main():
    ap = argparse.ArgumentParser(
        description="mermaid 序列图 -> 交互式 HTML 查看器")
    ap.add_argument("source", help="mermaid 源码文件（.mmd / .mermaid / .txt）")
    ap.add_argument("-o", "--out", help="输出 HTML 路径，默认与源码同目录同名")
    ap.add_argument("-t", "--title", help="HTML 标题，默认取源码文件名")
    ap.add_argument("-m", "--mermaid", help="指定 mermaid.min.js 路径")
    args = ap.parse_args()

    src_path = pathlib.Path(args.source).expanduser()
    if not src_path.is_file():
        sys.exit(f"[mmd2html] 源码文件不存在: {src_path}")
    if not TEMPLATE.is_file():
        sys.exit(f"[mmd2html] 模板缺失: {TEMPLATE}")

    out_path = pathlib.Path(args.out).expanduser() if args.out \
        else src_path.with_suffix(".html")
    out_path.parent.mkdir(parents=True, exist_ok=True)

    text = src_path.read_text(encoding="utf-8").strip()
    if "%%{init:" not in text and "sequenceDiagram" in text:
        # 指令必须在 diagram 类型声明之前；只对序列图有意义
        text = DIRECTIVE + "\n" + text

    js_path, _ = find_mermaid(args.mermaid, out_path.parent)
    if js_path.resolve() != (out_path.parent / "mermaid.min.js").resolve():
        shutil.copyfile(js_path, out_path.parent / "mermaid.min.js")

    html = TEMPLATE.read_text(encoding="utf-8")
    # 必须转义：模板把源码放进 HTML 里，不转义时正文的 `<X>` 会被当标签吞掉
    # （会把整张图渲染成 Syntax error），多行 note 也会因此失效。
    html = html.replace("{{TITLE}}", _html.escape(args.title or src_path.stem))
    html = html.replace("{{SOURCE}}", _html.escape(text))
    out_path.write_text(html, encoding="utf-8")

    print(f"[mmd2html] {out_path}  ({out_path.stat().st_size / 1024:.1f} KB)")
    print(f"[mmd2html] mermaid.min.js -> {out_path.parent / 'mermaid.min.js'}")

    # 输出落在 /mnt/c/... 时给一条能从 WSL 直接打开的 Windows 命令
    parts = out_path.resolve().parts
    if len(parts) > 3 and parts[1] == "mnt" and len(parts[2]) == 1:
        win = f"{parts[2].upper()}:\\" + "\\".join(parts[3:])
        print(f'[mmd2html] 打开: /mnt/c/Windows/System32/cmd.exe /c start "" "{win}"')


if __name__ == "__main__":
    main()
