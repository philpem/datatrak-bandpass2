#pragma once
#include <string>
#include "CoordSystem.h"

namespace bp {
namespace osgb {

// Helmert 7-parameter transform (±5 m accuracy)
LatLon wgs84_to_osgb36(LatLon wgs84);
LatLon osgb36_to_wgs84(LatLon osgb36);

// OSTN15 (±0.1 m) — requires grid loaded via load_ostn15(); falls back to Helmert if not loaded.
LatLon wgs84_to_osgb36_ostn15(LatLon wgs84);
LatLon osgb36_to_wgs84_ostn15(LatLon osgb36);

// Load OSTN15 binary grid from file.  Returns true on success.
// The grid must be in the OSTN15 binary format produced by tools/ostn15_download.py.
// Safe to call multiple times; a successful load replaces the previous grid.
bool load_ostn15(const std::string& path);

// Returns true if a valid OSTN15 grid has been loaded.
bool ostn15_loaded();

} // namespace osgb
} // namespace bp
