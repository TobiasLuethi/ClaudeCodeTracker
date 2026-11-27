@echo off
rem Doppelklick: entfernt das Claude Touch Gadget wieder.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall.ps1"
echo.
pause
