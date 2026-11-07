@echo off
setlocal
cd /d "%~dp0"
py -3.12 "%~dp0tools\rumble_catalog_player.py"
if errorlevel 1 (
    echo.
    echo Rumble test could not start. Keep this window open to inspect the error.
    pause
    exit /b 1
)
endlocal
