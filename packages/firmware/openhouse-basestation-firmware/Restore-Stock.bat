@echo off
rem Restore the base station to a stock firmware image kept in backups\.
rem It lists what is available, then writes the one you pick.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore-Stock.ps1" %*
pause
