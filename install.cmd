@echo off
rem Double-click to install the H3 Overlay into VCMI.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
if errorlevel 1 (echo. & echo Install failed - see the message above.)
echo.
pause
