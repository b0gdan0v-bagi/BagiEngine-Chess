#!/usr/bin/env python3
"""Makes web/icons/icon-192.png and web/icons/icon-512.png, the icons of the web app that web/app.json lists. Needs Pillow (`pip install pillow`);
the icons are committed, so nobody needs this tool to build.

    python tools/make_web_icons.py
    python tools/make_web_icons.py --out /tmp/icons

The icon is the white king of the Classic atlas (the sprite ClassicWhiteKing of config/TextureLibrary.xml) on a square of the Classic dark board
color. The king is cut out of assets/chess_test.png by its cell, trimmed to what is drawn and centred, no taller than KING_SHARE of the side:
the icons are declared `any maskable`, and a launcher may crop a maskable icon to a circle that keeps the middle 80 percent.
Exit code: 0 written, 1 Pillow is missing or the atlas is not there.
"""

import argparse
import math
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None

ROOT = Path(__file__).resolve().parent.parent
ATLAS = ROOT / "assets" / "chess_test.png"
DEFAULT_OUT = ROOT / "web" / "icons"

# The srcRect of ClassicWhiteKing in config/TextureLibrary.xml (tools/tests/test_make_web_icons.py holds the two together)
KING_X = 0.0
KING_Y = 0.0
KING_W = 116.67
KING_H = 190.5

# boardDark of the theme `classic` in config/UIThemes.xml
BACKGROUND = (181, 136, 99, 255)
KING_SHARE = 0.6
SIDES = (192, 512)


def icon_name(side: int) -> str:
    return f"icon-{side}.png"


def cut_king(atlas):
    """The king of the atlas cell, trimmed to the pixels that are drawn (RGBA)."""
    cell = atlas.convert("RGBA").crop((math.floor(KING_X), math.floor(KING_Y), math.ceil(KING_X + KING_W), math.ceil(KING_Y + KING_H)))
    bounds = cell.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError("the king's cell of the atlas is empty")
    return cell.crop(bounds)


def render(king, side: int):
    """A square icon of `side` pixels: the king centred on the background, its height KING_SHARE of the side."""
    height = round(side * KING_SHARE)
    width = max(1, round(king.width * height / king.height))
    scaled = king.resize((width, height), Image.LANCZOS)
    icon = Image.new("RGBA", (side, side), BACKGROUND)
    icon.alpha_composite(scaled, ((side - width) // 2, (side - height) // 2))
    return icon


def make_icons(atlas_path: Path, out_dir: Path) -> list[Path]:
    """Writes every icon of SIDES into `out_dir` and returns their paths."""
    with Image.open(atlas_path) as atlas:
        king = cut_king(atlas)
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for side in SIDES:
        path = out_dir / icon_name(side)
        render(king, side).save(path, optimize=True)
        written.append(path)
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description="Makes the icons of the web app from the Classic white king.")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"the folder of the icons (default: {DEFAULT_OUT})")
    args = parser.parse_args()
    if Image is None:
        print("Pillow is missing: pip install pillow", file=sys.stderr)
        return 1
    if not ATLAS.is_file():
        print(f"the atlas {ATLAS} is not there", file=sys.stderr)
        return 1
    for path in make_icons(ATLAS, args.out):
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
