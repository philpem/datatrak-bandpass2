# Windows dependencies via vcpkg
# vcpkg packages: wxwidgets[webview], gdal, sqlite3, curl, geographiclib
# These are found automatically when the vcpkg toolchain is active.

find_package(wxWidgets REQUIRED COMPONENTS webview aui adv core base net)
include(${wxWidgets_USE_FILE})

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)
find_package(GeographicLib REQUIRED)
