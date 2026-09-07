#!/usr/bin/env python3
"""Build Windows .ico files from icons/logo.png for the exe and installer."""

from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "icons" / "logo.png"
SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]


def main() -> None:
    img = Image.open(SRC).convert("RGBA")
    w, h = img.size
    side = max(w, h)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(img, ((side - w) // 2, (side - h) // 2), img)

    targets = [ROOT / "appicon.ico", ROOT / "installer" / "emstudio.ico"]
    for out in targets:
        canvas.save(out, format="ICO", sizes=SIZES)
        print(f"wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
