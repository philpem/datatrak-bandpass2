#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../model/Scenario.h"
#include "../engine/grid.h"

namespace bp {
namespace almanac {

// One zone's pattern assignment result
struct ZoneResult {
    int         zone_id   = 0;
    std::string zone_name;
    double      centroid_lat = 0.0;
    double      centroid_lon = 0.0;

    // Up to 4 pattern-pair sets: each is "slot_a,slot_b" (or "0,0" = no pair)
    std::string set1 = "0,0";
    std::string set2 = "0,0";
    std::string set3 = "0,0";
    std::string set4 = "0,0";

    // Number of viable pattern pairs found at this zone centroid
    int viable_count = 0;

    // True if fewer than 3 viable pairs exist — coverage gap
    bool is_gap = false;
};

// Compute zone pattern assignments from the given scenario and precomputed
// grid data.  For each zone centroid in uk_32zone.geojson:
//   1. Compute per-station SNR at the centroid.
//   2. Enumerate all station pairs (i,j) with i ≠ j.
//   3. Filter: both stations must exceed receiver SNR threshold (SNR > 0 dB).
//   4. Rank surviving pairs by WHDOP (lowest = best geometry).
//   5. Assign top-3 pairs as sets 1-3; set 4 = "0,0" if < 4 viable pairs.
//   6. Flag zone as a coverage gap if fewer than 3 viable pairs.
//
// geojson_path — path to uk_32zone.geojson (data/zones/uk_32zone.geojson).
//               If empty or file not found, an empty vector is returned.
std::vector<ZoneResult> compute_zone_patterns(
    const Scenario&  scenario,
    const GridData&  grid_data,
    const std::string& geojson_path);

// Generate the Zp almanac command block from zone results.
// Format per DTM-170:
//   Zp zone set pat1,pat2  (pat1 and pat2 are slot numbers)
std::string generate_zp(const std::vector<ZoneResult>& zones);

} // namespace almanac
} // namespace bp
