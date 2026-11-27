@echo off
rem Doppelklick: installiert das Claude Touch Gadget fuer den angemeldeten Benutzer.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
echo.
pause
