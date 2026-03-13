# Linux system package dependencies
# Install with:
#   sudo apt-get install libwxgtk3.2-dev libwxgtk-webview3.2-dev \
#     libsqlite3-dev libcurl4-gnutls-dev libgeographiclib-dev \
#     nlohmann-json3-dev libtomlplusplus-dev catch2 cmake ninja-build

find_package(wxWidgets REQUIRED COMPONENTS webview aui adv core base net)
include(${wxWidgets_USE_FILE})

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

# GeographicLib ships a FindModule rather than a config file on Ubuntu
list(APPEND CMAKE_MODULE_PATH "/usr/share/cmake/geographiclib")
find_package(GeographicLib REQUIRED)
