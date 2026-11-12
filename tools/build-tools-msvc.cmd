@echo off
REM Build the tool apps (lhtool, lhcapture, openhouse-check) from our source tree.
setlocal
set SRC=J:\workspace\openhouse_FixedByRagdollUser\repo\openhouse
set BUILD=%SRC%\build-win64

REM The Ninja backend here writes no depfile, so a header edit does not invalidate the
REM object that includes it: CMake reports "no work to do" and the old binary survives.
REM Wiping the app objects costs a few seconds and removes that whole class of mistake.
for %%T in (lhcapture lhtool openhouse-check) do (
  if exist "%BUILD%\CMakeFiles\%%T.dir" del /q /s "%BUILD%\CMakeFiles\%%T.dir\*.obj" >nul 2>&1
)

call "A:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set VCPKG_ROOT=J:\workspace\vcpkg-openhouse
set PATH=J:\app_code\C_cmake\bin;J:\app_code\C_Ninja;%PATH%

cmake --build "%BUILD%" --target lhtool lhcapture openhouse-check --parallel 12
if errorlevel 1 exit /b 1
echo === done ===
