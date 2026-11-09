@echo off
rem Grab the live sync-flash layout into flashes.txt. Close SteamVR first.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Flashes.ps1"
pause
