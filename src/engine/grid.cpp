#include "grid.h"
#include "../coords/NationalGrid.h"
#include "../coords/Osgb.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace bp {

std::vector<GridPoint> buildGrid(const GridDef& def) {
    // Approximate degrees per km at mid-latitude
    double mid_lat = (def.lat_min + def.lat_max) / 2.0;
    constexpr double DEG_PER_KM_LAT = 1.0 / 110.574;
    double deg_per_km_lon = 1.0 / (111.320 * std::cos(mid_lat * M_PI / 180.0));

    double dlat = def.resolution_km * DEG_PER_KM_LAT;
    double dlon = def.resolution_km * deg_per_km_lon;

    std::vector<GridPoint> pts;
    for (double lat = def.lat_min; lat <= def.lat_max + dlat * 0.5; lat += dlat) {
        for (double lon = def.lon_min; lon <= def.lon_max + dlon * 0.5; lon += dlon) {
            GridPoint p;
            p.lat = lat;
            p.lon = lon;
            // Compute OSGB easting/northing
            try {
                LatLon osgb36 = osgb::wgs84_to_osgb36({lat, lon});
                EastNorth en  = national_grid::latlon_to_en(osgb36);
                p.easting     = en.easting;
                p.northing    = en.northing;
            } catch (...) {
                p.easting  = 0.0;
                p.northing = 0.0;
            }
            pts.push_back(p);
        }
    }
    return pts;
}

// Simple colour ramp: value → hex colour
// Phase 1: all values are 0 (empty pipeline), so we just output transparent tiles
static std::string valueToColour(double /*v*/, double /*vmin*/, double /*vmax*/) {
    return "#4444ff";
}

std::string GridArray::to_geojson() const {
    if (points.empty() || values.empty()) {
        return R"({"type":"FeatureCollection","features":[]})";
    }

    double vmin = *std::min_element(values.begin(), values.end());
    double vmax = *std::max_element(values.begin(), values.end());
    if (vmax == vmin) vmax = vmin + 1.0;

    // Approximate tile size in degrees
    double dlat = 0.0, dlon = 0.0;
    if (width > 1 && height > 1) {
        dlon = (lon_max - lon_min) / (width  - 1) / 2.0;
        dlat = (lat_max - lat_min) / (height - 1) / 2.0;
    } else {
        dlon = dlat = resolution_km / 111.0;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << R"({"type":"FeatureCollection","features":[)";

    bool first = true;
    for (size_t i = 0; i < points.size(); ++i) {
        if (!first) out << ',';
        first = false;
        const auto& p = points[i];
        double v = (i < values.size()) ? values[i] : 0.0;
        std::string col = valueToColour(v, vmin, vmax);

        out << R"({"type":"Feature","geometry":{"type":"Polygon","coordinates":[[)"
            << '[' << (p.lon - dlon) << ',' << (p.lat - dlat) << ']' << ','
            << '[' << (p.lon + dlon) << ',' << (p.lat - dlat) << ']' << ','
            << '[' << (p.lon + dlon) << ',' << (p.lat + dlat) << ']' << ','
            << '[' << (p.lon - dlon) << ',' << (p.lat + dlat) << ']' << ','
            << '[' << (p.lon - dlon) << ',' << (p.lat - dlat) << ']'
            << R"(]]},"properties":{"v":)" << v << R"(,"c":")" << col << R"("}})";
    }

    out << "]}";
    return out.str();
}

} // namespace bp
