#!/usr/bin/env python3
"""Generate InkWyrm 1-bit logo headers from a source PNG.

Packing convention (reverse-engineered from the original headers and verified
by decode: see docs/REBASE-1.4.1.md phase 2):
  - The stored bitmap is the upright art rotated 90 degrees counter-clockwise
    (drawIcon() rotates it back for the portrait display).
  - Rows packed MSB-first, stride = ceil(W/8), bit 1 = white, bit 0 = ink.

Usage:
  python3 scripts/generate_logo_header.py <source.png>

Reads the full lockup (wordmark + emblem) from <source.png>, derives the
emblem-only mark by splitting on the largest horizontal whitespace gap, and
writes src/images/InkWyrmLogo.h (360x328 logical) and src/images/InkWyrmMark.h
(240x288 logical) plus decoded preview PNGs next to the source for visual
verification.
"""

import sys
from pathlib import Path

from PIL import Image

LOGO_CANVAS = (360, 328)  # upright W x H
MARK_CANVAS = (240, 288)
THRESHOLD = 176  # px < THRESHOLD counts as ink after resize


def flatten_to_bw(path: Path) -> Image.Image:
    img = Image.open(path)
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
        img = Image.alpha_composite(bg, img)
    img = img.convert("L")
    # Scrub near-white background (fake transparency checkerboards render at
    # ~240-250) to pure white so trim/gap detection see a clean page; keep
    # darker anti-aliased edge pixels for high-quality LANCZOS downscale.
    return img.point(lambda p: 255 if p >= 200 else p)


def trim(img: Image.Image) -> Image.Image:
    inv = img.point(lambda p: 255 - p)
    bbox = inv.getbbox()
    return img.crop(bbox) if bbox else img


def split_wordmark_emblem(img: Image.Image) -> Image.Image:
    """Return the emblem-only crop (below the largest all-white row gap)."""
    w, h = img.size
    px = img.load()
    white_rows = []
    for y in range(h):
        white_rows.append(all(px[x, y] >= 250 for x in range(0, w, 2)))
    # find largest run of white rows in the top half
    best = (0, 0)  # (length, end)
    run = 0
    for y in range(h // 2):
        run = run + 1 if white_rows[y] else 0
        if run > best[0]:
            best = (run, y)
    if best[0] < 5:
        return img  # no gap found; use full art
    return trim(img.crop((0, best[1] + 1, w, h)))


def fit_on_canvas(art: Image.Image, canvas: tuple[int, int], pad: int = 6) -> Image.Image:
    cw, ch = canvas
    aw, ah = art.size
    scale = min((cw - 2 * pad) / aw, (ch - 2 * pad) / ah)
    nw, nh = max(1, round(aw * scale)), max(1, round(ah * scale))
    resized = art.resize((nw, nh), Image.LANCZOS)
    out = Image.new("L", canvas, 255)
    out.paste(resized, ((cw - nw) // 2, (ch - nh) // 2))
    return out


def pack(upright: Image.Image) -> tuple[bytes, int, int]:
    stored = upright.transpose(Image.ROTATE_90)  # 90 deg CCW
    w, h = stored.size
    stride = (w + 7) // 8
    px = stored.load()
    out = bytearray()
    for y in range(h):
        for bx in range(stride):
            b = 0
            for i in range(8):
                x = bx * 8 + i
                white = x >= w or px[x, y] >= THRESHOLD
                b |= (1 if white else 0) << (7 - i)
            out.append(b)
    return bytes(out), w, h


def decode_preview(data: bytes, w: int, h: int) -> Image.Image:
    stride = (w + 7) // 8
    img = Image.new("L", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = 255 if (data[y * stride + x // 8] >> (7 - (x % 8))) & 1 else 0
    return img.transpose(Image.ROTATE_270)  # back to upright for eyeballs


def emit_header(path: Path, name: str, const_prefix: str, data: bytes,
                logical: tuple[int, int], source: str) -> None:
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        f"// Generated from {source} by scripts/generate_logo_header.py.",
        f"// Logical bitmap: {logical[0]}x{logical[1]}, rotated for portrait display via drawIcon().",
        f"constexpr int {const_prefix}_WIDTH = {logical[0]};",
        f"constexpr int {const_prefix}_HEIGHT = {logical[1]};",
        f"static const uint8_t {name}[] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    path.write_text("\n".join(lines) + "\n")


def main() -> None:
    src = Path(sys.argv[1])
    repo = Path(__file__).resolve().parent.parent
    art = trim(flatten_to_bw(src))
    emblem = split_wordmark_emblem(art)

    for canvas, name, prefix, out_name in (
        (LOGO_CANVAS, "InkWyrmLogo", "INKWYRM_LOGO", "InkWyrmLogo.h"),
        (MARK_CANVAS, "InkWyrmMark", "INKWYRM_MARK", "InkWyrmMark.h"),
    ):
        upright = fit_on_canvas(art if name == "InkWyrmLogo" else emblem, canvas)
        data, sw, sh = pack(upright)
        emit_header(repo / "src/images" / out_name, name, prefix, data, canvas,
                    "src/images/inkwyrm/" + src.name)
        decode_preview(data, sw, sh).save(src.parent / f"preview-{name}.png")
        print(f"{out_name}: {len(data)} bytes, stored {sw}x{sh}, preview written")


if __name__ == "__main__":
    main()
