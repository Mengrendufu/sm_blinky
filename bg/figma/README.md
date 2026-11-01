# Figma background artwork

Source: local sm_blinky.fig, 2026-11-01, default 24:98 / liveShow 81:493.
Only exported illustration fills are baked into these backgrounds.
Labels, counters, dots, bars, switches and arcs are native LVGL objects.

| File | Figma image hash | Size |
|---|---|---|
| sys-screen.png | c16e84d351e35cccae39407e9df2ba25802dcbb5 | 240×320 |
| sys-progress.png | 029f535cc97fe55b6ae5dd2bb436d73e36f4f3cf | 236×113 |
| sys-gauge.png | 86bd281997ca7e31fedcb4bf5d566183217f25dd | 236×103 |
| live-gauge.png | 7522364761de8ed3767759e24f261671f7ccef7d | 236×117 |
| live-info.png | 9b3d1a6721129127cd547259d46b1603761c2905 | 236×125 |

Regenerate from the repository root:

```sh
python3 tools/figma_backgrounds.py
```

This uses ffmpeg to scale/crop each image with FILL, rounds card corners
at 12 px, overlays #051F30 at 42%, and adds the Sys cards' 1 px inside
#1E293B border. The Live screen base is #102B42. Output is 240×320,
RGB565 little-endian, 153,600 bytes per page, plus PNG previews.

Concatenate sys-page.rgb565, live-page.rgb565, and bg/bg-logo.rgb565
in that order for tools/burn-asset.py. It builds the AST2 package with
per-image CRCs; no format or slot-order change is needed.
