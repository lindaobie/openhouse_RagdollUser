@echo off
rem Read-only check: compares the firmware inside the base station with the image in
rem this folder. Writes nothing. Unplug the base, hold its rear button, plug in the
rem micro-USB, then the power, then let go; when "CRP DISABLD" appears, run this.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Firmware.ps1" -Verify
pause
