@echo off
REM Build the *upstream* master driver with MSVC + Ninja + vcpkg, alongside our own build.
setlocal
set SRC=J:\workspace\openhouse-upstream

call "A:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set VCPKG_ROOT=J:\workspace\vcpkg-openhouse
set PATH=J:\app_code\C_cmake\bin;J:\app_code\C_Ninja;%PATH%

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

echo === build driver_lighthouse ===
cmake --build "%SRC%\build-win64" --target driver_lighthouse --parallel 12
if errorlevel 1 exit /b 1

echo === done ===
dir /b "%SRC%\build-win64\openvr\lighthouse\bin\win64"
