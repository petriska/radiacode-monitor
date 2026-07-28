#!/usr/bin/env python3
"""Make Radiacode Monitor app-icon canvas transparent.

The source art is a rounded app tile centered on a dark full-bleed canvas.
We keep the tile (and a small outer margin for soft shadow / glow) and set
everything outside a rounded rectangle to alpha=0.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

REPO = Path(__file__).resolve().parents[1]
ICONS = REPO / "icons"
MACOS = REPO / "macos"
SOURCE_CANDIDATES = [
    ICONS / "radiacode-monitor-1024.png",
    ICONS / "radiacode-monitor.png",
    ICONS / "radiacode-monitor-256.png",
]
SIZES = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def detect_tile_rect(im: Image.Image) -> tuple[int, int, int, int]:
    """Bounding box of the beveled rim of the app tile (left, top, right, bottom)."""
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    xs: list[int] = []
    ys: list[int] = []
    # Sample beveled rim: mid brightness, low–medium saturation
    step = max(1, w // 512)
    for y in range(0, h, step):
        for x in range(0, w, step):
            r, g, b, _ = px[x, y]
            mx = max(r, g, b)
            mn = min(r, g, b)
            sat = mx - mn
            if 78 <= mx <= 200 and sat <= 55 and r >= 55:
                xs.append(x)
                ys.append(y)
    if not xs:
        # Fallback: centered 70% square
        m = int(min(w, h) * 0.15)
        return m, m, w - m, h - m
    return min(xs), min(ys), max(xs), max(ys)


def make_transparent(im: Image.Image) -> Image.Image:
    im = im.convert("RGBA")
    w, h = im.size
    left, top, right, bottom = detect_tile_rect(im)
    # Force a square plate (the art is a rounded square; auto-bbox is often short
    # on the bottom because spectrum bars are not rim-coloured).
    cx = 0.5 * (left + right)
    cy = 0.5 * (top + bottom)
    side = max(right - left, bottom - top)
    left = int(round(cx - side / 2))
    right = int(round(cx + side / 2))
    top = int(round(cy - side / 2))
    bottom = int(round(cy + side / 2))

    # Expand slightly so soft drop-shadow / outer glow stays with the tile
    pad = max(4, int(min(w, h) * 0.012))
    left = max(0, left - pad)
    top = max(0, top - pad)
    right = min(w, right + pad)
    bottom = min(h, bottom + pad)

    tw, th = right - left, bottom - top
    # App-icon corner radius (~22% of side — matches the art)
    radius = max(8, int(min(tw, th) * 0.22))

    print(f"  tile rect=({left},{top})-({right},{bottom}) radius={radius}")

    mask = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle([left, top, right - 1, bottom - 1], radius=radius, fill=255)
    # Soften mask edge so the cut isn't jagged after downscale
    mask = mask.filter(ImageFilter.GaussianBlur(radius=max(0.6, w / 1024.0 * 1.2)))

    out = im.copy()
    out.putalpha(mask)
    return out


def main() -> int:
    src_path = next((p for p in SOURCE_CANDIDATES if p.is_file()), None)
    if not src_path:
        print("No source icon found", file=sys.stderr)
        return 1

    # Always start from git-clean full-bleed source when possible
    print(f"Source: {src_path}")
    src = Image.open(src_path).convert("RGBA")
    # If already transparent (re-run), refuse — user should restore opaque masters
    alpha = src.split()[3]
    if sum(1 for v in alpha.getdata() if v == 0) > src.size[0] * src.size[1] * 0.1:
        print(
            "WARNING: source already mostly transparent. "
            "Restore original opaque PNGs (git checkout icons/) for best results.",
            file=sys.stderr,
        )

    cleaned = make_transparent(src)
    master = (
        cleaned.resize((1024, 1024), Image.Resampling.LANCZOS)
        if cleaned.size != (1024, 1024)
        else cleaned
    )

    master.save(ICONS / "radiacode-monitor.png", "PNG")
    print(f"Wrote {ICONS / 'radiacode-monitor.png'}")

    for size in SIZES:
        out = master.resize((size, size), Image.Resampling.LANCZOS)
        path = ICONS / f"radiacode-monitor-{size}.png"
        out.save(path, "PNG")
        print(f"Wrote {path}")

    ico_images = [master.resize((s, s), Image.Resampling.LANCZOS) for s in ICO_SIZES]
    ico_path = ICONS / "radiacode-monitor.ico"
    ico_images[0].save(
        ico_path,
        format="ICO",
        sizes=[(s, s) for s in ICO_SIZES],
        append_images=ico_images[1:],
    )
    print(f"Wrote {ico_path}")

    if MACOS.is_dir():
        mac_png = MACOS / "radiacode-monitor-1024.png"
        master.save(mac_png, "PNG")
        print(f"Wrote {mac_png}")
        print("  Note: on macOS rebuild radiacode-monitor.icns from this PNG (iconutil).")

    check = Image.open(ICONS / "radiacode-monitor-256.png").convert("RGBA")
    for p in [(0, 0), (255, 0), (0, 255), (255, 255), (128, 128)]:
        print(f"  pixel {p}: {check.getpixel(p)}")
    print(f"  content bbox (256): {check.getbbox()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
