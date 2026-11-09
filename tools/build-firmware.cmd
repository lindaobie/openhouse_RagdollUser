@echo off
rem Build the lhbs base station firmware from repo\openhouse.
rem   build-firmware.cmd          build
rem   build-firmware.cmd clean    clean
rem The toolchain paths below are this machine's; edit them on another one.
setlocal
set "PATH=%~dp0bin;J:\app_code\C_arm-gnu-toolchain-13.2\13.2 Rel1\bin;J:\app_code\C_make_4.4.1\bin;A:\Program Files\HanaAgent\resources\git\usr\bin;%PATH%"
pushd "%~dp0..\repo\openhouse"
make -C basestation_firmware %*
set RC=%ERRORLEVEL%
if exist "basestation_firmware\build\firmware.bin" (
  echo.
  echo 产物: repo\openhouse\basestation_firmware\build\firmware.bin
)
popd
exit /b %RC%
