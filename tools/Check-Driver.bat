@echo off
rem Read-only: says whether the driver SteamVR loads is openhouse's or the stock one.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-Driver.ps1" %*
pause
