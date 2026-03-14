#include "grid.h"
#include <cmath>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cstdint>

namespace bp {

GridBuildResult buildGrid(const GridDef& def,
                          const std::atomic<bool>& cancel) {
    GridBuildResult result;
    if (def.resolution_km <= 0.0) return result;

    // Approximate degrees per km at mid-latitude
    double mid_lat = (def.lat_min + def.lat_max) / 2.0;
    constexpr double DEG_PER_KM_LAT = 1.0 / 110.574;
    double deg_per_km_lon = 1.0 / (111.320 * std::cos(mid_lat * M_PI / 180.0));

    double dlat = def.resolution_km * DEG_PER_KM_LAT;
    double dlon = def.resolution_km * deg_per_km_lon;

    // Pre-compute dimensions
    int cols = 0;
    for (double lon = def.lon_min; lon <= def.lon_max + dlon * 0.5; lon += dlon)
        ++cols;
    int rows = 0;
    for (double lat = def.lat_min; lat <= def.lat_max + dlat * 0.5; lat += dlat)
        ++rows;

    result.width  = cols;
    result.height = rows;
    result.points.reserve((size_t)rows * cols);

    for (double lat = def.lat_min; lat <= def.lat_max + dlat * 0.5; lat += dlat) {
        if (cancel.load()) return {};
        for (double lon = def.lon_min; lon <= def.lon_max + dlon * 0.5; lon += dlon) {
            if (cancel.load()) return {};
            GridPoint p;
            p.lat = lat;
            p.lon = lon;
            result.points.push_back(p);
        }
    }
    return result;
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

    // Grid dimensions — use stored width/height if available, otherwise infer
    int cols = width;
    int rows = height;
    if (cols <= 0 || rows <= 0) {
        // Infer columns from first row: count points sharing the first latitude
        cols = 1;
        if (points.size() > 1) {
            double first_lat = points[0].lat;
            while (cols < (int)points.size() &&
                   std::abs(points[cols].lat - first_lat) < 1e-8)
                ++cols;
        }
        rows = (int)(points.size() / std::max(cols, 1));
    }

    // Base half-tile size in degrees
    double mid_lat = (lat_min + lat_max) / 2.0;
    double cos_lat = std::cos(mid_lat * M_PI / 180.0);
    double base_dlat = resolution_km / 110.574 / 2.0;
    double base_dlon = (cos_lat > 1e-6)
                     ? resolution_km / (111.320 * cos_lat) / 2.0
                     : base_dlat;

    // Subsample if the grid exceeds a practical rendering limit.
    // ~30 000 GeoJSON polygon features ≈ 8 MB — well within RunScript limits.
    static constexpr int MAX_RENDER_FEATURES = 30000;
    int stride = 1;
    while ((rows / stride) * (cols / stride) > MAX_RENDER_FEATURES)
        ++stride;

    // Scale tile half-size to cover the stride so there are no gaps
    double dlat = base_dlat * stride;
    double dlon = base_dlon * stride;

    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << R"({"type":"FeatureCollection","features":[)";

    bool first = true;
    for (int r = 0; r < rows; r += stride) {
        for (int c = 0; c < cols; c += stride) {
            size_t i = (size_t)r * cols + c;
            if (i >= points.size() || i >= values.size()) continue;
            if (!first) out << ',';
            first = false;
            const auto& p = points[i];
            double v = values[i];
            std::string col = valueToColour(v, vmin, vmax);

            out << R"({"type":"Feature","geometry":{"type":"Polygon","coordinates":[[)"
                << '[' << (p.lon - dlon) << ',' << (p.lat - dlat) << ']' << ','
                << '[' << (p.lon + dlon) << ',' << (p.lat - dlat) << ']' << ','
                << '[' << (p.lon + dlon) << ',' << (p.lat + dlat) << ']' << ','
                << '[' << (p.lon - dlon) << ',' << (p.lat + dlat) << ']' << ','
                << '[' << (p.lon - dlon) << ',' << (p.lat - dlat) << ']'
                << R"(]]},"properties":{"v":)" << v << R"(,"c":")" << col << R"("}})";
        }
    }

    out << "]}";
    return out.str();
}

// ── Image overlay rendering ──────────────────────────────────────────────────

static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t b  = (uint32_t)data[i] << 16;
        if (i + 1 < data.size()) b |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < data.size()) b |= (uint32_t)data[i + 2];
        out += chars[(b >> 18) & 0x3F];
        out += chars[(b >> 12) & 0x3F];
        out += (i + 1 < data.size()) ? chars[(b >>  6) & 0x3F] : '=';
        out += (i + 2 < data.size()) ? chars[(b      ) & 0x3F] : '=';
    }
    return out;
}

// Apply the same blue→cyan→yellow→red ramp as valueToColour, writing RGBA into out[].
static void colourRampRGBA(double t, uint8_t* out) {
    t = std::max(0.0, std::min(1.0, t));
    double r, g, b;
    if (t < 1.0 / 3.0) {
        double s = t * 3.0;
        r = 0;    g = s;    b = 1.0 - s * 0.5;
    } else if (t < 2.0 / 3.0) {
        double s = (t - 1.0 / 3.0) * 3.0;
        r = s;    g = 1.0;  b = 0.5 - s * 0.5;
    } else {
        double s = (t - 2.0 / 3.0) * 3.0;
        r = 1.0;  g = 1.0 - s;  b = 0;
    }
    out[0] = (uint8_t)std::max(0, std::min(255, (int)(r * 255)));
    out[1] = (uint8_t)std::max(0, std::min(255, (int)(g * 255)));
    out[2] = (uint8_t)std::max(0, std::min(255, (int)(b * 255)));
    out[3] = 255;  // fully opaque; opacity handled by Leaflet imageOverlay
}

GridImageData GridArray::to_image_data() const {
    GridImageData result;
    result.width   = width;
    result.height  = height;
    result.lat_min = lat_min;
    result.lat_max = lat_max;
    result.lon_min = lon_min;
    result.lon_max = lon_max;

    if (points.empty() || values.empty() || width <= 0 || height <= 0)
        return result;

    double vmin = *std::min_element(values.begin(), values.end());
    double vmax = *std::max_element(values.begin(), values.end());
    if (vmax == vmin) vmax = vmin + 1.0;

    // RGBA pixel buffer: row 0 = northernmost row (lat_max).
    // The grid is stored south→north (row 0 = lat_min), so flip vertically.
    std::vector<uint8_t> pixels((size_t)height * width * 4);
    for (int r = 0; r < height; ++r) {
        int grid_row = height - 1 - r;  // flip south→north to north→south
        for (int c = 0; c < width; ++c) {
            size_t vi = (size_t)grid_row * width + c;
            double t  = (vi < values.size())
                        ? (values[vi] - vmin) / (vmax - vmin)
                        : 0.0;
            colourRampRGBA(t, &pixels[((size_t)r * width + c) * 4]);
        }
    }

    result.base64_rgba = base64_encode(pixels);
    return result;
}

} // namespace bp
