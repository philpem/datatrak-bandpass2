#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <atomic>
#include "../model/Scenario.h"

namespace bp {

struct GridPoint {
    double lat      = 0.0;
    double lon      = 0.0;
    double easting  = 0.0;
    double northing = 0.0;
};

// Rendered image data for a layer: RGBA pixels base64-encoded, row 0 = northernmost row.
struct GridImageData {
    std::string base64_rgba;  // raw RGBA bytes (width × height × 4), base64-encoded
    int         width  = 0;
    int         height = 0;
    double      lat_min = 0, lat_max = 0;
    double      lon_min = 0, lon_max = 0;
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

    // Produce a GeoJSON FeatureCollection colour-ramp for Leaflet (legacy / small grids)
    std::string to_geojson() const;

    // Produce raw RGBA pixel data (base64-encoded, row 0 = north) for canvas image overlay.
    // Returns an empty base64_rgba string if the grid has no structured dimensions.
    GridImageData to_image_data() const;
};

struct GridData {
    std::map<std::string, GridArray> layers;
    uint64_t request_id = 0;
};

struct GridBuildResult {
    std::vector<GridPoint> points;
    int width  = 0;   // columns (longitude steps)
    int height = 0;   // rows    (latitude steps)
};

// Build the grid point array from a GridDef.
// Passes cancel through so the caller can abort a large grid build.
GridBuildResult buildGrid(const GridDef& def,
                          const std::atomic<bool>& cancel);

} // namespace bp
