#pragma once
#include <string>
#include "../engine/grid.h"
#include "../model/Scenario.h"

namespace bp {

// ExportManager: non-UI layer that writes GridArray data to files.
//
// Three export formats are supported:
//   CSV     — lat, lon, value  (one row per grid point, no wxWidgets needed)
//   PNG     — colour-ramp image using the same ramp as the Leaflet display
//             (uses wxImage; only available when compiled with wxWidgets)
//   GeoTIFF — single-band float32 with WGS84 CRS via GDAL
//             (only available when BP_HAVE_GDAL is defined)
//
// The PNG/GeoTIFF functions are declared here but implemented conditionally
// in ExportManager.cpp.  If the required library is absent, the functions
// return a non-empty error string.

class ExportManager {
public:
    // Export a GridArray as a CSV file.
    // Format:  lat,lon,value\n  (header: lat,lon,<layer_name>)
    // Returns empty string on success, or an error message.
    static std::string export_csv(const GridArray& layer,
                                   const std::string& path);

    // Export a GridArray as a PNG colour-ramp image.
    // grid_width / grid_height give the pixel dimensions (usually layer.width /
    // layer.height).  Returns empty string on success, or an error message.
    // Requires wxWidgets; returns error if not available.
    static std::string export_png(const GridArray& layer,
                                   const std::string& path);

    // Export a GridArray as a single-band GeoTIFF (float32, WGS84).
    // Requires GDAL (BP_HAVE_GDAL).  Returns empty string on success, or an
    // error message (including "GDAL not available" when compiled without it).
    static std::string export_geotiff(const GridArray& layer,
                                       const std::string& path);

    // Export a self-contained HTML report for all computed layers.
    // Includes: scenario parameters, per-layer statistics and descriptions.
    // No external dependencies (no CDN, no embedded images beyond ASCII art).
    static std::string export_html(const GridData& data,
                                   const Scenario& scenario,
                                   const std::string& path);
};

} // namespace bp
