#pragma once
#include <filesystem>
#include <stdexcept>
#include "Scenario.h"

namespace bp {
namespace toml_io {

// Load a scenario from a TOML file.
// Throws std::runtime_error on parse error or I/O failure.
Scenario load(const std::filesystem::path& path);

// Save a scenario to a TOML file.
// Throws std::runtime_error on I/O failure.
void save(const Scenario& s, const std::filesystem::path& path);

} // namespace toml_io
} // namespace bp
