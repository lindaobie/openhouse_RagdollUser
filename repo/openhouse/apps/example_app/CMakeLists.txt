cmake_minimum_required(VERSION 3.20)
project(example_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where the openhouse source is. Defaults to this repository; point it at your own copy
# with -DOPENHOUSE_SOURCE_DIR=/path/to/openhouse.
set(OPENHOUSE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "openhouse source directory")

# Only the library is needed: skip openhouse's driver, tools, viewers and tests.
set(LH_BUILD_OPENVR_DRIVER OFF CACHE BOOL "" FORCE)
set(LH_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LH_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
set(LH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${OPENHOUSE_SOURCE_DIR} openhouse EXCLUDE_FROM_ALL)

add_executable(example_app main.cpp)
target_link_libraries(example_app PRIVATE lh::lh)
