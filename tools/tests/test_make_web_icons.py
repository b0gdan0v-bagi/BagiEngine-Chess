"""tools/make_web_icons.py: the king's rect follows the atlas config, and the icons of web/icons have the sizes web/app.json declares.
Skipped without Pillow.

    python -m unittest discover -s tools/tests -p "test_make_web_icons.py"
"""

import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import make_web_icons

ROOT = Path(__file__).resolve().parents[2]
PNG_IHDR_OFFSET = 16


def png_size(path: Path) -> tuple[int, int]:
    return struct.unpack(">II", path.read_bytes()[PNG_IHDR_OFFSET:PNG_IHDR_OFFSET + 8])


@unittest.skipIf(make_web_icons.Image is None, "Pillow is not installed")
class MakeWebIconsTest(unittest.TestCase):
    def test_the_king_rect_is_the_sprite_of_the_texture_library(self):
        text = (ROOT / "config" / "TextureLibrary.xml").read_text(encoding="utf-8")
        found = re.search(r'<sprite name="ClassicWhiteKing"[^>]*>\s*<srcRect x="([\d.]+)" y="([\d.]+)" w="([\d.]+)" h="([\d.]+)"', text)
        self.assertIsNotNone(found)
        rect = tuple(float(value) for value in found.groups())
        self.assertEqual(rect, (make_web_icons.KING_X, make_web_icons.KING_Y, make_web_icons.KING_W, make_web_icons.KING_H))

    def test_the_tool_writes_square_icons_of_both_sizes(self):
        with tempfile.TemporaryDirectory() as folder:
            written = make_web_icons.make_icons(make_web_icons.ATLAS, Path(folder))
            self.assertEqual([png_size(path) for path in written], [(side, side) for side in make_web_icons.SIDES])

    def test_the_committed_icons_have_the_declared_sizes(self):
        for side in make_web_icons.SIDES:
            self.assertEqual(png_size(make_web_icons.DEFAULT_OUT / make_web_icons.icon_name(side)), (side, side))


if __name__ == "__main__":
    unittest.main()
