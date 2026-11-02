@echo off
setlocal
cd /d "%~dp0"
set "SAVAGEWIND_DATA_ROOT=%~dp0"
"%~dp0build\savagewind.exe" %*
endlocal
