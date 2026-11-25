"""What the game hands to the engine's web deploy: web/app.json and its icons through the engine's package.py, the Railway partial, and that
web/ holds no secret or address. Standard library only; ctest runs it as BagiChessWebDeployFiles. Skipped when the engine's tools/web is missing.

    python -m unittest discover -s tests -p "test_web_deploy.py"
"""

import json
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
APP_FILE = WEB / "app.json"
RAILWAY_FILE = WEB / ".railway" / "railway.ts"
ENGINE_WEB = ROOT / "engine" / "tools" / "web"

if ENGINE_WEB.is_dir():
    sys.path.insert(0, str(ENGINE_WEB))
    sys.path.insert(0, str(ENGINE_WEB / "tests"))
    import build_and_deploy
    import package
    import web_fixtures

ADDRESS = re.compile(r"https?://")
SECRET = re.compile(r"""(token|password|secret)\s*[=:]\s*["'][^"']+["']""", re.IGNORECASE)
# Railway stops reading railway.json and railway.toml once the service has settings of its own
RAILWAY_FILES_IGNORED = ("railway.json", "railway.toml")


def declared_size(size: str) -> tuple[int, int]:
    width, height = size.split("x")
    return int(width), int(height)


@unittest.skipUnless(ENGINE_WEB.is_dir(), "the engine's tools/web is not there")
class WebDeployFilesTest(unittest.TestCase):
    def setUp(self):
        self.app = json.loads(APP_FILE.read_text(encoding="utf-8"))

    def test_the_icons_of_the_app_file_are_pngs_of_the_declared_size_inside_web(self):
        self.assertTrue(self.app["icons"])
        for icon in self.app["icons"]:
            path = (WEB / icon["src"]).resolve()
            self.assertTrue(path.is_relative_to(WEB.resolve()), icon["src"])
            self.assertEqual(web_fixtures.png_header_size(path.read_bytes()), declared_size(icon["sizes"]), icon["src"])

    def test_package_py_takes_the_app_file_and_writes_the_manifest_and_the_icons(self):
        with tempfile.TemporaryDirectory() as folder:
            build = web_fixtures.write_build_output(Path(folder) / "build")
            out = Path(folder) / "dist"
            package.package(build, out, brotli_module=None, app_file=APP_FILE)
            manifest = json.loads((out / "manifest.webmanifest").read_text(encoding="utf-8"))
            self.assertEqual(manifest["name"], "Chess")
            self.assertEqual(manifest["orientation"], "any")
            self.assertEqual(sorted(icon["sizes"] for icon in manifest["icons"]), ["192x192", "512x512"])
            for icon in manifest["icons"]:
                self.assertEqual((out / icon["src"]).read_bytes(), (WEB / icon["src"]).read_bytes(), "the generated flat icon was written instead")
            self.assertEqual(web_fixtures.png_header_size((out / "icons" / "apple-touch-icon.png").read_bytes()), (512, 512))

    def test_build_and_deploy_finds_the_app_file_by_itself(self):
        self.assertEqual(ROOT / build_and_deploy.APP_FILE, APP_FILE)
        self.assertTrue(APP_FILE.is_file())

    def test_the_railway_partial_names_the_health_check_and_the_dockerfile(self):
        text = RAILWAY_FILE.read_text(encoding="utf-8")
        self.assertIn("/healthz", text)
        self.assertIn("DOCKERFILE", text)

    def test_web_holds_no_address_and_no_secret(self):
        files = [path for path in WEB.rglob("*") if path.is_file() and path.suffix in (".ts", ".json")]
        self.assertIn(RAILWAY_FILE, files)
        for path in files:
            text = path.read_text(encoding="utf-8")
            self.assertIsNone(ADDRESS.search(text), f"{path} names an address")
            self.assertIsNone(SECRET.search(text), f"{path} holds a secret")

    def test_no_railway_json_or_toml(self):
        for folder in (ROOT, WEB):
            for name in RAILWAY_FILES_IGNORED:
                self.assertFalse((folder / name).exists(), f"{folder / name}")


if __name__ == "__main__":
    unittest.main()
