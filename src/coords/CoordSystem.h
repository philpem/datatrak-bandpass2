#pragma once

namespace bp {

enum class CoordSystem {
    WGS84_LatLon,
    OSGB36_LatLon,
    OSGB36_NationalGrid
};

struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

struct EastNorth {
    double easting  = 0.0;
    double northing = 0.0;
};

} // namespace bp
