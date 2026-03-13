#pragma once
#include <string>
#include "CoordSystem.h"

namespace bp {
namespace national_grid {

// Airy 1830 OSGB36 lat/lon <-> National Grid easting/northing
EastNorth latlon_to_en(LatLon osgb36);
LatLon    en_to_latlon(EastNorth en);   // returns OSGB36 lat/lon

// Grid reference formatting/parsing
// digits: 4 = 100m, 6 = 10m, 8 = 1m
std::string en_to_gridref(EastNorth en, int digits = 6);
EastNorth   gridref_to_en(const std::string& ref);

// Auto-detect format and return WGS84 lat/lon.
// Accepts: "TL 271 707", "271000 707000", "52.3247 -0.1848"
// Throws std::invalid_argument if format is unrecognised.
LatLon parse_coordinate(const std::string& input, bool use_ostn15 = false);

} // namespace national_grid
} // namespace bp
