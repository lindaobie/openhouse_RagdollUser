@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Firmware.ps1" %*
pause
