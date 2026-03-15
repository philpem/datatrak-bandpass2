#pragma once
#include <string>
#include <vector>

namespace bp {

// ---------------------------------------------------------------------------
// DataPaths — resolve data file/directory paths across standard locations
//
// Search order (mirrors OSTN15 logic in main.cpp):
//   1. Executable directory
//   2. data/ subdirectory next to executable
//   3. Platform user-data directory (~/.local/share/bandpass2 on Linux,
//      ~/Library/Application Support/bandpass2 on macOS,
//      %APPDATA%/bandpass2 on Windows)
//   4. Walk up from executable (up to 4 levels) looking for data/ subdirectory
//      (handles build trees like build/src/bandpass2 -> ../../data/)
//
// Paths stored in scenario TOML files should be relative basenames when the
// file lives in one of these standard directories.  resolve_data_path()
// expands a relative name back to an absolute path at load time.
// ---------------------------------------------------------------------------

/// Return the ordered list of directories to search for data files.
/// Each entry is an absolute directory path.
std::vector<std::string> data_search_dirs();

/// If `path` is already absolute and exists, return it unchanged.
/// Otherwise treat it as a relative filename/path and search the standard
/// data directories.  Returns the first match, or the original path if
/// nothing is found (so the caller's existing error handling still applies).
std::string resolve_data_path(const std::string& path);

/// Same as resolve_data_path but checks for a directory instead of a file.
std::string resolve_data_dir(const std::string& path);

/// If `abs_path` is inside one of the standard data directories, return
/// just the relative portion (basename or relative subpath).  Otherwise
/// return the full absolute path unchanged.
/// This is used when saving to TOML so that scenario files stay portable.
std::string make_relative_data_path(const std::string& abs_path);

} // namespace bp
