set(BP_SOURCES
    main.cpp
    model/toml_io.cpp
    coords/Osgb.cpp
    coords/NationalGrid.cpp
    engine/grid.cpp
    engine/groundwave.cpp
    engine/skywave.cpp
    engine/noise.cpp
    engine/snr.cpp
    engine/whdop.cpp
    engine/monteath.cpp
    engine/asf.cpp
    engine/conductivity.cpp
    engine/terrain.cpp
    engine/compute_manager.cpp
    ui/MainFrame.cpp
    ui/MapPanel.cpp
    ui/TileCache.cpp
    ui/NetworkConfigPanel.cpp
    ui/ParamEditor.cpp
    ui/LayerPanel.cpp
    ui/ReceiverPanel.cpp
    ui/ResultsPanel.cpp
    ui/ExportManager.cpp
    ui/PoRefDialog.cpp
    almanac/AlmanacExport.cpp
    almanac/ZonePatterns.cpp
    almanac/MonitorCalib.cpp
)

# Version header — regenerated at every build from git metadata
add_custom_target(bandpass2_version ALL
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DBINARY_DIR=${CMAKE_BINARY_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/version.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/src/ui/version_generated.h
    COMMENT "Generating version header from git"
)

add_executable(bandpass2 WIN32 ${BP_SOURCES})
add_dependencies(bandpass2 bandpass2_version)

target_include_directories(bandpass2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/src/ui
)

target_link_libraries(bandpass2 PRIVATE
    ${wxWidgets_LIBRARIES}
    SQLite::SQLite3
    CURL::libcurl
    tomlplusplus::tomlplusplus
    nlohmann_json::nlohmann_json
)

# GeographicLib uses a FindModule (not CONFIG) on Linux/macOS
if(GeographicLib_FOUND)
    target_include_directories(bandpass2 PRIVATE ${GeographicLib_INCLUDE_DIRS})
    target_link_libraries(bandpass2 PRIVATE ${GeographicLib_LIBRARIES})
endif()

if(GDAL_FOUND)
    target_link_libraries(bandpass2 PRIVATE GDAL::GDAL)
    target_compile_definitions(bandpass2 PRIVATE BP_HAVE_GDAL USE_GDAL)
endif()

if(WIN32)
    target_link_libraries(bandpass2 PRIVATE ws2_32)
endif()

# Copy web assets to build directory so the app can find them at runtime
add_custom_command(TARGET bandpass2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/web
        $<TARGET_FILE_DIR:bandpass2>/web
    COMMENT "Copying web assets"
)
