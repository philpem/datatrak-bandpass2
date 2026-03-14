# Linux system package dependencies
# Install with:
#   sudo apt-get install libwxgtk3.2-dev libwxgtk-webview3.2-dev \
#     libsqlite3-dev libcurl4-gnutls-dev libgeographiclib-dev \
#     nlohmann-json3-dev catch2 cmake ninja-build
# (toml++ is vendored in third_party/ — no package needed)

# BP_TESTS_ONLY: skip wxWidgets, SQLite, CURL when building only the test suite
option(BP_TESTS_ONLY "Build only tests (no wxWidgets/SQLite/CURL required)" OFF)

if(NOT BP_TESTS_ONLY)
    find_package(wxWidgets REQUIRED COMPONENTS webview aui adv core base net)
    include(${wxWidgets_USE_FILE})

    find_package(SQLite3 REQUIRED)
    find_package(CURL REQUIRED)
else()
    # Minimal stubs so src/CMakeLists.txt still compiles the test-only targets
    set(wxWidgets_FOUND FALSE)
    # Create empty interface libraries so target_link_libraries doesn't fail
    if(NOT TARGET SQLite::SQLite3)
        find_package(SQLite3 QUIET)
        if(NOT SQLite3_FOUND)
            add_library(SQLite3_stub INTERFACE)
            add_library(SQLite::SQLite3 ALIAS SQLite3_stub)
        endif()
    endif()
endif()

# GeographicLib ships a FindModule rather than a config file on Ubuntu
list(APPEND CMAKE_MODULE_PATH "/usr/share/cmake/geographiclib")
if(BP_TESTS_ONLY)
    find_package(GeographicLib QUIET)
else()
    find_package(GeographicLib REQUIRED)
endif()
