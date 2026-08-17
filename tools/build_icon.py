from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "app-icon-v1.4-source.png"
PNG = ROOT / "assets" / "app-icon-v1.4.png"
ICO = ROOT / "assets" / "XiaoChuang.ico"
PREVIEW = ROOT / "assets" / "app-icon-small-preview.png"
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)


def main() -> None:
    source = Image.open(SOURCE).convert("RGBA")
    normalized = source.resize((1024, 1024), Image.Resampling.LANCZOS)
    normalized.save(PNG, optimize=True)
    normalized.save(ICO, format="ICO", sizes=[(size, size) for size in SIZES])

    preview = Image.new("RGBA", (640, 190), (30, 32, 42, 255))
    draw = ImageDraw.Draw(preview)
    x = 24
    for size in (16, 20, 24, 32, 40, 48, 64):
        reduced = normalized.resize((size, size), Image.Resampling.LANCZOS)
        if size <= 32:
            reduced = reduced.filter(ImageFilter.UnsharpMask(radius=0.6, percent=80, threshold=2))
        scale = max(1, 96 // size)
        shown = reduced.resize((size * scale, size * scale), Image.Resampling.NEAREST)
        preview.alpha_composite(shown, (x, 18))
        draw.text((x, 132), f"{size}px", fill=(235, 237, 246, 255))
        x += max(82, size * scale + 22)
    preview.save(PREVIEW)


if __name__ == "__main__":
    main()
