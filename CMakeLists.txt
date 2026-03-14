cmake_minimum_required(VERSION 3.20)
project(bandpass2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

# Platform-specific dependency discovery
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/deps_linux.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(cmake/deps_macos.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    include(cmake/deps_windows.cmake)
endif()

# toml++ — vendored single-header, no external package needed
add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus)
add_library(tomlplusplus::tomlplusplus ALIAS tomlplusplus)

# Common packages
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

# GDAL — mandatory; used for conductivity raster (P2-03), terrain (P2-04),
# and GeoTIFF export (P5-01). Install: apt libgdal-dev / brew gdal / vcpkg gdal
find_package(GDAL REQUIRED)
message(STATUS "GDAL found: ${GDAL_VERSION}")

# MSVC does not expose M_PI from <cmath> by default; it requires _USE_MATH_DEFINES.
# Set it globally so every source file in the project gets it without per-file changes.
if(MSVC)
    add_compile_definitions(_USE_MATH_DEFINES)
endif()

if(NOT BP_TESTS_ONLY)
    add_subdirectory(src)
endif()
add_subdirectory(tests)

# Install web assets alongside the binary
install(DIRECTORY src/web DESTINATION share/bandpass2)
# Install zone data alongside the binary
install(DIRECTORY data/zones DESTINATION share/bandpass2)
# Install desktop file (Linux)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES data/bandpass2.desktop
            DESTINATION share/applications)
endif()
