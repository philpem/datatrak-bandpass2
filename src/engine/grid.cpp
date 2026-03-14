#include "grid.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace bp {

std::vector<GridPoint> buildGrid(const GridDef& def,
                                  const std::atomic<bool>& cancel) {
    if (def.resolution_km <= 0.0) return {};   // guard: zero/negative → infinite loop

    // Approximate degrees per km at mid-latitude
    double mid_lat = (def.lat_min + def.lat_max) / 2.0;
    constexpr double DEG_PER_KM_LAT = 1.0 / 110.574;
    double deg_per_km_lon = 1.0 / (111.320 * std::cos(mid_lat * M_PI / 180.0));

    double dlat = def.resolution_km * DEG_PER_KM_LAT;
    double dlon = def.resolution_km * deg_per_km_lon;

    std::vector<GridPoint> pts;
    for (double lat = def.lat_min; lat <= def.lat_max + dlat * 0.5; lat += dlat) {
        if (cancel.load()) return {};
        for (double lon = def.lon_min; lon <= def.lon_max + dlon * 0.5; lon += dlon) {
            if (cancel.load()) return {};   // check cancel per row, not just per latitude band
            GridPoint p;
            p.lat = lat;
            p.lon = lon;
            // easting/northing left as 0 — computed on demand in display code
            pts.push_back(p);
        }
    }
    return pts;
}

// Colour ramp: blue → cyan → green → yellow → red  (0.0 → 1.0)
static std::string valueToColour(double v, double vmin, double vmax) {
    double t = (vmax > vmin) ? (v - vmin) / (vmax - vmin) : 0.5;
    t = std::max(0.0, std::min(1.0, t));

    // Four-stop ramp: blue(0)→cyan(0.33)→yellow(0.67)→red(1)
    double r, g, b;
    if (t < 1.0/3.0) {
        double s = t * 3.0;
        r = 0;  g = s;  b = 1.0 - s * 0.5;
    } else if (t < 2.0/3.0) {
        double s = (t - 1.0/3.0) * 3.0;
        r = s;  g = 1.0;  b = 0.5 - s * 0.5;
    } else {
        double s = (t - 2.0/3.0) * 3.0;
        r = 1.0;  g = 1.0 - s;  b = 0;
    }

    // Clamp to [0,255] as explicit ints so GCC can prove format-truncation is safe
    int ri = std::max(0, std::min(255, (int)(r * 255)));
    int gi = std::max(0, std::min(255, (int)(g * 255)));
    int bi = std::max(0, std::min(255, (int)(b * 255)));
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", ri, gi, bi);
    return buf;
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
        double mid_lat = (lat_min + lat_max) / 2.0;
        double cos_lat = std::cos(mid_lat * M_PI / 180.0);
        dlat = resolution_km / 110.574 / 2.0;
        dlon = (cos_lat > 1e-6)
             ? resolution_km / (111.320 * cos_lat) / 2.0
             : dlat;
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
