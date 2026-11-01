#!/usr/bin/env python3
"""Compose the exported Figma artwork into the two RGB565 page slots.

Requires ffmpeg. Text, arcs and progress bars remain dynamic LVGL objects.
"""
from pathlib import Path
import math
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "bg" / "figma"
WIDTH, HEIGHT = 240, 320
SHADE = (5, 31, 48)


def pixels(name, width, height):
    scale = (f"scale={width}:{height}:force_original_aspect_ratio=increase,"
             f"crop={width}:{height}")
    return subprocess.check_output([
        "ffmpeg", "-v", "error", "-i", str(ART / name), "-vf", scale,
        "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"
    ])


def card(canvas, name, x, y, width, height, border=False):
    src = pixels(name, width, height)
    radius = 12
    for row in range(height):
        for col in range(width):
            # Antialias the round corners against the page background.
            dx = max(radius - col - 0.5, 0, col + 0.5 - (width - radius))
            dy = max(radius - row - 0.5, 0, row + 0.5 - (height - radius))
            alpha = max(0.0, min(1.0, radius + 0.5 - math.hypot(dx, dy)))
            edge = min(col + 0.5, width - col - 0.5,
                       row + 0.5, height - row - 0.5)
            distance = min(edge, radius - math.hypot(dx, dy))
            stroke = min(1.0, max(0.0, 1.5 - distance)) if border else 0
            src_i = (row * width + col) * 3
            dst_i = ((y + row) * WIDTH + x + col) * 3
            for channel in range(3):
                color = src[src_i + channel] * 0.58 + SHADE[channel] * 0.42
                color = color * (1 - stroke) + (30, 41, 59)[channel] * stroke
                canvas[dst_i + channel] = round(
                    canvas[dst_i + channel] * (1 - alpha) + color * alpha)


def save(name, canvas):
    raw = bytearray()
    for i in range(0, len(canvas), 3):
        r, g, b = canvas[i:i + 3]
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        raw.extend((value & 0xff, value >> 8))
    (ART / (name + ".rgb565")).write_bytes(raw)
    subprocess.run([
        "ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pixel_format",
        "rgb24", "-video_size", "240x320", "-i", "-", "-frames:v", "1",
        str(ART / (name + ".png"))
    ], input=canvas, check=True)


def main():
    sys = bytearray(pixels("sys-screen.png", WIDTH, HEIGHT))
    card(sys, "sys-progress.png", 2, 59, 236, 113, border=True)
    card(sys, "sys-gauge.png", 2, 197, 236, 103, border=True)
    save("sys-page", sys)

    live = bytearray((16, 43, 66) * (WIDTH * HEIGHT))
    card(live, "live-gauge.png", 8, 58, 224, 129)
    card(live, "live-info.png", 8, 205, 224, 92)
    save("live-page", live)


if __name__ == "__main__":
    main()
