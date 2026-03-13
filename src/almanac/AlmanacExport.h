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
// Sg  — station grid (OSGB easting/northing per station)
// Stxs — slot→station assignment
// Po  — pattern offsets (ASF corrections in millilanes)
//
// The Zp (zone patterns) table requires the 32-zone polygon dataset
// which is bundled in data/zones/uk_32zone.geojson; it is omitted
// from this function if the GridData is empty.
//
// Returns the full text block for serial entry or file save.
std::string generate_almanac(const Scenario& scenario,
                              const GridData&  grid_data,
                              FirmwareFormat   fmt = FirmwareFormat::V7);

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
