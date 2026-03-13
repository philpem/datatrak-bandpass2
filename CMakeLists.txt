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

# GDAL — required for conductivity raster (P2-03) and terrain (P2-04)
find_package(GDAL QUIET)
if(GDAL_FOUND)
    message(STATUS "GDAL found: ${GDAL_VERSION}")
    set(BP_HAVE_GDAL TRUE)
else()
    message(STATUS "GDAL not found; conductivity/terrain rasters and GeoTIFF export will be unavailable")
    set(BP_HAVE_GDAL FALSE)
endif()

add_subdirectory(src)
add_subdirectory(tests)

# Install web assets alongside the binary
install(DIRECTORY src/web DESTINATION share/bandpass2)
