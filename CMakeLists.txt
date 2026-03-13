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

# Common vcpkg/system packages
find_package(tomlplusplus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

# Optional GDAL (Phase 5 — not required for Phase 1)
find_package(GDAL QUIET)
if(GDAL_FOUND)
    message(STATUS "GDAL found: ${GDAL_VERSION}")
else()
    message(STATUS "GDAL not found; export-to-GeoTIFF will be unavailable")
endif()

add_subdirectory(src)
add_subdirectory(tests)

# Install web assets alongside the binary
install(DIRECTORY src/web DESTINATION share/bandpass2)
