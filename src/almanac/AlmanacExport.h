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

// ---------------------------------------------------------------------------
// Pattern offset computation from a reference point (P5-10)
//
// For each non-master transmitter in scenario.transmitters, computes the
// Monteath ASF at (ref_lat, ref_lon) and returns a PatternOffset per pattern.
// Pattern string: "{slave_slot},{master_slot}".
//
// Mode 1 (baseline midpoint): pass lat/lon = NaN → midpoint between each
//   slave and its master is used automatically.
//
// Mode 2 (user marker): pass the surveyed marker coordinates explicitly.
//   Multiple markers → call this function once per marker and average results
//   (caller responsibility).
//
// nsamples: Monteath integration sample count (20 = grid quality, 50 = precise).
// ---------------------------------------------------------------------------
std::vector<PatternOffset> compute_po_at_point(
    const Scenario& scenario,
    double          ref_lat,
    double          ref_lon,
    int             nsamples = 50);

// Convenience: compute po using Mode 1 (baseline midpoint per pattern).
std::vector<PatternOffset> compute_po_mode1(const Scenario& scenario,
                                             int nsamples = 50);

} // namespace almanac
} // namespace bp
