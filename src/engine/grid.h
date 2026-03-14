#pragma once
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <cstdint>
#include <atomic>
#include "../model/Scenario.h"

namespace bp {

// Colour-ramp scale mode for layer rendering.
// Log: percentile-clipped 2–98th range mapped through log₁₀; improves
// visual detail on layers with large dynamic range (WHDOP, accuracy, ASF gradient).
enum class ScaleMode { Linear, Log };

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
    // Actual (non-log) data range used for the colour ramp — use for legend labels.
    double      display_vmin = 0, display_vmax = 0;
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

    // Produce a GeoJSON FeatureCollection colour-ramp for Leaflet (legacy / small grids).
    // NaN values in the data are treated as "no data" (omitted from output).
    std::string to_geojson(ScaleMode scale = ScaleMode::Linear) const;

    // Produce raw RGBA pixel data (base64-encoded, row 0 = north) for canvas image overlay.
    // NaN values are rendered fully transparent.
    // Returns an empty base64_rgba string if the grid has no structured dimensions.
    GridImageData to_image_data(ScaleMode scale = ScaleMode::Linear) const;

    // Compute the display range [vmin, vmax] used for the colour ramp.
    // NaN values are excluded from the computation.
    // Linear: simple min/max.  Log: 2nd–98th percentile of positive values.
    std::pair<double, double> display_range(ScaleMode scale) const;
};

struct GridData {
    std::map<std::string, GridArray> layers;
    uint64_t request_id = 0;
};

struct GridBuildResult {
    std::vector<GridPoint> points;
    int    width         = 0;   // columns (longitude steps)
    int    height        = 0;   // rows    (latitude steps)
    double resolution_km = 0.0; // actual cell spacing used (derived from max_points or GridDef)
};

// Build the grid point array from a GridDef.
// Passes cancel through so the caller can abort a large grid build.
GridBuildResult buildGrid(const GridDef& def,
                          const std::atomic<bool>& cancel);

} // namespace bp
