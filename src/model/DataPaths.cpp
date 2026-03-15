#include "DataPaths.h"
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <filesystem>

namespace bp {

std::vector<std::string> data_search_dirs() {
    std::vector<std::string> dirs;

    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());

    // 1. Executable directory
    dirs.push_back(exe.GetPath().ToStdString());

    // 2. data/ subdirectory next to executable
    {
        wxFileName exe_data(exe);
        exe_data.AppendDir("data");
        dirs.push_back(exe_data.GetPath().ToStdString());
    }

    // 3. Platform user-data directory
    dirs.push_back(
        wxStandardPaths::Get().GetUserDataDir().ToStdString());

    // 4. Walk up from executable (up to 4 levels) looking for data/
    {
        wxFileName walk(wxStandardPaths::Get().GetExecutablePath());
        for (int i = 0; i < 4 && walk.GetDirCount() > 0; ++i) {
            walk.RemoveLastDir();
            wxFileName dev(walk);
            dev.AppendDir("data");
            dirs.push_back(dev.GetPath().ToStdString());
        }
    }

    return dirs;
}

std::string resolve_data_path(const std::string& path) {
    if (path.empty()) return path;

    // Already absolute and exists — use as-is
    if (std::filesystem::path(path).is_absolute()) {
        if (std::filesystem::exists(path))
            return path;
        // Absolute but missing — still return it (caller handles errors)
        return path;
    }

    // Relative: search standard directories
    for (const auto& dir : data_search_dirs()) {
        std::filesystem::path candidate = std::filesystem::path(dir) / path;
        if (std::filesystem::exists(candidate))
            return candidate.string();
    }

    // Not found — return original so caller's error path still fires
    return path;
}

std::string resolve_data_dir(const std::string& path) {
    if (path.empty()) return path;

    if (std::filesystem::path(path).is_absolute()) {
        if (std::filesystem::is_directory(path))
            return path;
        return path;
    }

    for (const auto& dir : data_search_dirs()) {
        std::filesystem::path candidate = std::filesystem::path(dir) / path;
        if (std::filesystem::is_directory(candidate))
            return candidate.string();
    }

    return path;
}

std::string make_relative_data_path(const std::string& abs_path) {
    if (abs_path.empty()) return abs_path;

    std::filesystem::path p(abs_path);
    if (!p.is_absolute()) return abs_path;  // already relative

    for (const auto& dir : data_search_dirs()) {
        std::filesystem::path d(dir);
        // Check if abs_path starts with this directory
        auto rel = p.lexically_relative(d);
        if (!rel.empty() && *rel.begin() != "..") {
            // It's inside this data directory — return relative portion
            return rel.string();
        }
    }

    // Not inside any known data directory — keep absolute
    return abs_path;
}

} // namespace bp
