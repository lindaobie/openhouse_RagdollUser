# Static MSVC build with AVX2 for every dependency. Eigen's alignment changes with AVX
# (32-byte packets), so Ceres/Eigen must be built with the same /arch as liblh, or
# matrices passed between them corrupt the heap.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_C_FLAGS "/arch:AVX2")
set(VCPKG_CXX_FLAGS "/arch:AVX2")
