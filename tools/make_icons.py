#!/usr/bin/env python3
"""Draw the app icons into web/icons/ (needs Pillow). The PNGs are committed; run this only to change the artwork."""
import pathlib

from PIL import Image, ImageDraw

OUT = pathlib.Path(__file__).resolve().parent.parent / "web" / "icons"
BG = (14, 124, 102, 255)
FG = (255, 255, 255, 255)
SCALE = 4  # supersampling for smooth edges


def draw(size: int) -> Image.Image:
    s = size * SCALE
    img = Image.new("RGBA", (s, s), BG)
    d = ImageDraw.Draw(img)
    u = s / 100.0
    # pointer arrow, kept inside the central 60% so maskable crops never clip it
    pts = [(44, 30), (44, 76), (55, 65), (63, 83), (70, 80), (62, 62), (77, 62)]
    d.polygon([(x * u, y * u) for x, y in pts], fill=FG)
    # two motion arcs: the "jiggle"
    for r, w in ((11, 3), (19, 3)):
        box = [(44 - r) * u, (30 - r) * u, (44 + r) * u, (30 + r) * u]
        d.arc(box, start=195, end=255, fill=FG, width=int(w * u))
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name, size in (("icon-192.png", 192), ("icon-512.png", 512), ("apple-touch-icon.png", 180)):
        draw(size).convert("RGB").save(OUT / name, optimize=True)
        print("wrote", OUT / name)


if __name__ == "__main__":
    main()
