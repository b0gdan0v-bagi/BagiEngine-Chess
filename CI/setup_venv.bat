@echo off
setlocal

rem The venv is the engine's (engine\.venv): the engine's tools run from it. This script gets the
rem engine submodule, runs the engine's setup_venv.bat, then installs the game's own requirements.txt
rem (when there is one) into the same venv.

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
set "ENGINE_DIR=%REPO_ROOT%\engine"
set "ENGINE_SETUP=%ENGINE_DIR%\CI\setup_venv.bat"
set "REQUIREMENTS=%REPO_ROOT%\requirements.txt"

if not exist "%ENGINE_SETUP%" (
    echo [INFO] Fetching the engine submodule...
    git -C "%REPO_ROOT%" submodule update --init --recursive engine
    if not exist "%ENGINE_SETUP%" (
        echo [ERROR] Failed to fetch engine/ ^(git submodule update --init --recursive engine^)
        exit /b 1
    )
)

call "%ENGINE_SETUP%"
if %ERRORLEVEL% NEQ 0 exit /b 1

if exist "%REQUIREMENTS%" (
    echo [INFO] Installing the game's dependencies...
    uv pip install --python "%ENGINE_DIR%\.venv\Scripts\python.exe" -r "%REQUIREMENTS%"
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Failed to install the game's dependencies
        exit /b 1
    )
)

echo [OK] Game environment ready ^(engine\.venv^)
endlocal
