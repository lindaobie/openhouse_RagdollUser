@echo off
REM Build the Windows driver natively with MSVC + Ninja + vcpkg.
REM Usage: build-driver-msvc.cmd [target]   (default: driver_lighthouse)
setlocal
set SRC=J:\workspace\openhouse_FixedByRagdollUser\repo\openhouse
set TARGET=%~1
if "%TARGET%"=="" set TARGET=driver_lighthouse

REM vcvars64 overwrites VCPKG_ROOT with the build-tools copy, so set ours after it.
call "A:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set VCPKG_ROOT=J:\workspace\vcpkg-openhouse
set PATH=J:\app_code\C_cmake\bin;J:\app_code\C_Ninja;%PATH%

REM Dependencies are installed by hand; manifest mode would go online again.
REM OpenVR headers are taken from a sparse clone of ValveSoftware/openvr.
echo === configure ===
cmake -S "%SRC%" -B "%SRC%\build-win64" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM=J:\app_code\C_Ninja\ninja.exe ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DVCPKG_MANIFEST_MODE=OFF ^
  -DOPENVR_INCLUDE_DIR=J:\workspace\openvr-headers\headers ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DLH_BUILD_TESTS=OFF -DLH_BUILD_VIEWER=OFF -DLH_BUILD_TOOLS=ON
if errorlevel 1 exit /b 1

echo === build %TARGET% ===
cmake --build "%SRC%\build-win64" --target %TARGET% --parallel 12
if errorlevel 1 exit /b 1

echo === done ===
dir /b "%SRC%\build-win64\openvr\lighthouse\bin\win64"
