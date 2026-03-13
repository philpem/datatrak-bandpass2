#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "../model/Scenario.h"

namespace bp {

struct GridPoint {
    double lat      = 0.0;
    double lon      = 0.0;
    double easting  = 0.0;
    double northing = 0.0;
};

struct GridArray {
    std::string            layer_name;
    std::vector<GridPoint> points;
    std::vector<double>    values;
    int                    width  = 0;
    int                    height = 0;
    double                 lat_min = 0, lat_max = 0;
    double                 lon_min = 0, lon_max = 0;
    double                 resolution_km = 0;

    // Produce a GeoJSON FeatureCollection colour-ramp for Leaflet
    std::string to_geojson() const;
};

struct GridData {
    std::map<std::string, GridArray> layers;
    uint64_t request_id = 0;
};

// Build the grid point array from a GridDef
std::vector<GridPoint> buildGrid(const GridDef& def);

} // namespace bp
