@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0openhouse-driver.ps1" Uninstall %*
pause
