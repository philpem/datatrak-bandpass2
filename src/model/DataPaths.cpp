#include "DataPaths.h"
#include <filesystem>
#include <mutex>

namespace bp {

static std::vector<std::string> s_dirs;
static std::once_flag s_once;

void init_data_search_dirs(std::vector<std::string> dirs) {
    s_dirs = std::move(dirs);
}

const std::vector<std::string>& data_search_dirs() {
    return s_dirs;
}

std::string resolve_data_path(const std::string& path) {
    if (path.empty()) return path;

    // Already absolute — return as-is (caller handles missing files)
    if (std::filesystem::path(path).is_absolute())
        return path;

    // Relative: search standard directories
    for (const auto& dir : s_dirs) {
        auto candidate = std::filesystem::path(dir) / path;
        if (std::filesystem::exists(candidate))
            return candidate.string();
    }

    // Not found — return original so caller's error path still fires
    return path;
}

std::string resolve_data_dir(const std::string& path) {
    if (path.empty()) return path;

    if (std::filesystem::path(path).is_absolute())
        return path;

    for (const auto& dir : s_dirs) {
        auto candidate = std::filesystem::path(dir) / path;
        if (std::filesystem::is_directory(candidate))
            return candidate.string();
    }

    return path;
}

std::string make_relative_data_path(const std::string& abs_path) {
    if (abs_path.empty()) return abs_path;

    std::filesystem::path p(abs_path);
    if (!p.is_absolute()) return abs_path;  // already relative

    for (const auto& dir : s_dirs) {
        std::filesystem::path d(dir);
        auto rel = p.lexically_relative(d);
        if (!rel.empty() && *rel.begin() != "..") {
            return rel.string();
        }
    }

    // Not inside any known data directory — keep absolute
    return abs_path;
}

} // namespace bp
