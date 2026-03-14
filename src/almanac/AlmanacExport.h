#pragma once
#include <string>
#include "../model/Scenario.h"
#include "../engine/grid.h"

namespace bp {
namespace almanac {

// V7/V16 almanac text format selection
enum class FirmwareFormat { V7, V16 };

// Generate the four almanac tables as plain ASCII text.
//
// Sg   — station grid (OSGB easting/northing per station)
// Stxs — slot→station assignment
// Zp   — zone patterns (requires geojson_path to uk_32zone.geojson)
// Po   — pattern offsets (ASF corrections in millilanes)
//
// Returns the full text block for serial entry or file save.
std::string generate_almanac(const Scenario&    scenario,
                              const GridData&    grid_data,
                              FirmwareFormat     fmt          = FirmwareFormat::V7,
                              const std::string& geojson_path = "");

// Generate just the Sg commands
std::string generate_sg(const Scenario& scenario);

// Generate Stxs commands
std::string generate_stxs(const Scenario& scenario);

// Generate Po commands from scenario.pattern_offsets
// If pattern_offsets is empty, uses ASF from grid_data as a fallback.
std::string generate_po(const Scenario& scenario,
                         const GridData& grid_data,
                         FirmwareFormat  fmt = FirmwareFormat::V7);

} // namespace almanac
} // namespace bp
