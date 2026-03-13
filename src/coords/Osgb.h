#pragma once
#include "CoordSystem.h"

namespace bp {
namespace osgb {

// Helmert 7-parameter transform (±5 m accuracy)
LatLon wgs84_to_osgb36(LatLon wgs84);
LatLon osgb36_to_wgs84(LatLon osgb36);

// OSTN15 (±0.1 m) — stub; falls back to Helmert if grid not loaded
LatLon wgs84_to_osgb36_ostn15(LatLon wgs84);
LatLon osgb36_to_wgs84_ostn15(LatLon osgb36);

} // namespace osgb
} // namespace bp
