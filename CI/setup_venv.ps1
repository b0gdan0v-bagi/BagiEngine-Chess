# Setup the Python virtual environment of the game (PowerShell).
# The venv is the engine's (engine\.venv): the engine's tools run from it. This script gets the
# engine submodule, runs the engine's setup_venv.ps1, then installs the game's own requirements.txt
# (when there is one) into the same venv.

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$EngineDir = Join-Path $RepoRoot "engine"
$EngineSetup = Join-Path $EngineDir "CI\setup_venv.ps1"
$Requirements = Join-Path $RepoRoot "requirements.txt"

if (-not (Test-Path $EngineSetup)) {
    Write-Host "[INFO] Fetching the engine submodule..." -ForegroundColor Cyan
    git -C $RepoRoot submodule update --init --recursive engine
    if (($LASTEXITCODE -ne 0) -or (-not (Test-Path $EngineSetup))) {
        Write-Host "[ERROR] Failed to fetch engine/ (git submodule update --init --recursive engine)" -ForegroundColor Red
        exit 1
    }
}

& $EngineSetup
if ($LASTEXITCODE -ne 0) {
    exit 1
}

if (Test-Path $Requirements) {
    Write-Host "[INFO] Installing the game's dependencies..." -ForegroundColor Cyan
    uv pip install --python (Join-Path $EngineDir ".venv\Scripts\python.exe") -r $Requirements
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Failed to install the game's dependencies" -ForegroundColor Red
        exit 1
    }
}

Write-Host "[OK] Game environment ready (engine\.venv)" -ForegroundColor Green
