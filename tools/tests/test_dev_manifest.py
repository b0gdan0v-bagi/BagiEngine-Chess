"""dev.json of this repository, read back with the engine's tools/dev/dev.py: the shape, the placeholders and the paths it names."""

import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "engine" / "tools" / "dev"))

import dev  # noqa: E402

PRODUCTS = {"game", "uci", "tests", "web"}
KINDS = {"desktop", "web", None}
PRESET = "windows-clang-debug"
ROOT_PREFIX = "{root}/"
OUTPUT_PATHS = {"{root}/dist"}  # written by the web product, not in the repository


def all_commands(manifest: dict) -> list[list[str]]:
    commands = []
    for product in manifest["products"]:
        commands.append(product.get("run", []))
        commands.extend(product.get("after", []))
    commands.extend(tool["command"] for tool in manifest["tools"])
    return commands


@pytest.fixture(scope="module")
def manifest() -> dict:
    return dev.load_manifest(ROOT)


def test_every_product_is_described(manifest: dict):
    names = [product["name"] for product in manifest["products"]]
    assert len(names) == len(set(names))
    assert PRODUCTS <= set(names)
    for product in manifest["products"]:
        assert product["label"]
        assert "target" in product or "targets" in product
        assert product.get("kind") in KINDS


def test_every_tool_builds_a_known_product(manifest: dict):
    names = {product["name"] for product in manifest["products"]}
    for tool in manifest["tools"]:
        assert tool.get("build") is None or tool["build"] in names


def test_every_command_expands_with_the_five_placeholders(manifest: dict):
    for command in all_commands(manifest):
        assert command
        dev.expand(command, ROOT, PRESET)


def test_the_paths_under_root_exist(manifest: dict):
    for command in all_commands(manifest):
        for part in command:
            if part.startswith(ROOT_PREFIX) and part not in OUTPUT_PATHS and not part.endswith("tools/ui_shots.py"):  # WP 2 writes it
                assert (ROOT / part[len(ROOT_PREFIX):]).exists(), part


def test_info_names_the_products(manifest: dict, capsys):
    assert dev.main(["--root", str(ROOT), "info", "--json"]) == 0
    info = json.loads(capsys.readouterr().out)
    assert PRODUCTS <= {product["name"] for product in info["products"]}


def test_every_flag_has_a_name_and_a_label(manifest: dict):
    for flag in manifest["flags"]:
        assert flag["name"]
        assert flag["label"]
