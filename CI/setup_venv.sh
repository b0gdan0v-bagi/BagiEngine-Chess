#!/bin/bash
# Setup the Python virtual environment of the game (macOS/Linux).
# The venv is the engine's (engine/.venv): the engine's tools run from it. This script gets the
# engine submodule, runs the engine's setup_venv.sh, then installs the game's own requirements.txt
# (when there is one) into the same venv.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
ENGINE_DIR="$REPO_ROOT/engine"
ENGINE_SETUP="$ENGINE_DIR/CI/setup_venv.sh"
REQUIREMENTS="$REPO_ROOT/requirements.txt"

if [ ! -f "$ENGINE_SETUP" ]; then
    echo "[INFO] Fetching the engine submodule..."
    git -C "$REPO_ROOT" submodule update --init --recursive engine
    if [ $? -ne 0 ] || [ ! -f "$ENGINE_SETUP" ]; then
        echo "[ERROR] Failed to fetch engine/ (git submodule update --init --recursive engine)"
        exit 1
    fi
fi

bash "$ENGINE_SETUP"
if [ $? -ne 0 ]; then
    exit 1
fi

if [ -f "$REQUIREMENTS" ]; then
    echo "[INFO] Installing the game's dependencies..."
    uv pip install --python "$ENGINE_DIR/.venv/bin/python" -r "$REQUIREMENTS"
    if [ $? -ne 0 ]; then
        echo "[ERROR] Failed to install the game's dependencies"
        exit 1
    fi
fi

echo "[OK] Game environment ready (engine/.venv)"
