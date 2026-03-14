include(CTest)

# Catch2 config is found via find_package in top-level CMakeLists.txt.
# The catch_discover_tests function lives in Catch.cmake.
list(APPEND CMAKE_MODULE_PATH "/usr/lib/cmake/Catch2")
include(Catch)

# Helper to add a Catch2 test target
function(bp_add_test target)
    # All remaining args are source files
    set(sources ${ARGN})
    add_executable(${target} ${sources})
    target_include_directories(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(${target} PRIVATE
        Catch2::Catch2WithMain
        tomlplusplus::tomlplusplus
        nlohmann_json::nlohmann_json
        SQLite::SQLite3
    )
    if(GeographicLib_FOUND)
        target_include_directories(${target} PRIVATE ${GeographicLib_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE ${GeographicLib_LIBRARIES})
    endif()
    if(GDAL_FOUND)
        target_link_libraries(${target} PRIVATE GDAL::GDAL)
        target_compile_definitions(${target} PRIVATE USE_GDAL)
    endif()
    catch_discover_tests(${target})
endfunction()

# Model tests
bp_add_test(test_toml_io
    model/test_toml_io.cpp
    ${CMAKE_SOURCE_DIR}/src/model/toml_io.cpp
)

# Coordinate tests
bp_add_test(test_osgb
    coords/test_osgb.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/Osgb.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/NationalGrid.cpp
)

# Engine tests (grid only — no UI/wx dependency)
bp_add_test(test_compute_manager
    engine/test_compute_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/grid.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/Osgb.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/NationalGrid.cpp
)

# Groundwave / noise / skywave tests (no UI/wx dependency)
bp_add_test(test_groundwave
    engine/test_groundwave.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/groundwave.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/conductivity.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/terrain.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/noise.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/skywave.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/grid.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/Osgb.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/NationalGrid.cpp
)

# SNR / WHDOP tests
bp_add_test(test_snr_whdop
    engine/test_snr_whdop.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/snr.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/whdop.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/groundwave.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/conductivity.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/terrain.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/noise.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/grid.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/Osgb.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/NationalGrid.cpp
)

# ASF / Monteath / Virtual Locator tests (P2-05, P4-01, P4-02, P4-04)
bp_add_test(test_asf
    engine/test_asf.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/asf.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/monteath.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/groundwave.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/conductivity.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/terrain.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/noise.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/skywave.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/snr.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/whdop.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/grid.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/Osgb.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/NationalGrid.cpp
)

# Almanac export tests
bp_add_test(test_almanac_export
    almanac/test_almanac_export.cpp
    ${CMAKE_SOURCE_DIR}/src/almanac/AlmanacExport.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/Osgb.cpp
    ${CMAKE_SOURCE_DIR}/src/coords/NationalGrid.cpp
)
