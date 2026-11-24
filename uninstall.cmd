@echo off
rem Double-click to remove the H3 Overlay and restore VCMI settings.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1" %*
if errorlevel 1 (echo. & echo Uninstall failed - see the message above.)
echo.
pause
