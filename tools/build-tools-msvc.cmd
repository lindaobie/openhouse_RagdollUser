@echo off
REM Build the tool apps (lhtool, lhcapture, openhouse-check) from our source tree.
setlocal
set SRC=J:\workspace\openhouse_FixedByRagdollUser\repo\openhouse

call "A:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set VCPKG_ROOT=J:\workspace\vcpkg-openhouse
set PATH=J:\app_code\C_cmake\bin;J:\app_code\C_Ninja;%PATH%

cmake --build "%SRC%\build-win64" --target lhtool lhcapture openhouse-check --parallel 12
if errorlevel 1 exit /b 1
echo === done ===
