# macOS dependencies via Homebrew
# Install with:
#   brew install wxwidgets gdal sqlite curl geographiclib

find_package(wxWidgets REQUIRED COMPONENTS webview aui adv core base net)
include(${wxWidgets_USE_FILE})

find_package(SQLite3 REQUIRED)
find_package(CURL REQUIRED)

# GeographicLib may install cmake module into homebrew prefix
list(APPEND CMAKE_PREFIX_PATH "/usr/local" "/opt/homebrew")
find_package(GeographicLib REQUIRED)
