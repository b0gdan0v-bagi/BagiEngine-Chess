import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[1]
ENGINE_CONTROL_DIR = TOOLS_DIR.parent / "engine" / "tools" / "control"

# stockfish_bridge.py is here; bagi_control_client, game_process and the fake control server are the engine's
sys.path.insert(0, str(TOOLS_DIR))
sys.path.insert(0, str(ENGINE_CONTROL_DIR))
sys.path.insert(0, str(ENGINE_CONTROL_DIR / "tests"))
