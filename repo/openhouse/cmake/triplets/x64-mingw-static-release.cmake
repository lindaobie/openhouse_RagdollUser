# Release-only static MinGW dependencies for the Windows package. Per-function
# sections let the final link drop the unused parts of Ceres/glog/hidapi.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED PATH)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_C_FLAGS "-ffunction-sections -fdata-sections")
set(VCPKG_CXX_FLAGS "-ffunction-sections -fdata-sections")
