#include "ExportManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>

// PNG export is only available when compiled with wxWidgets.
// The presence of <wx/wx.h> or <wx/image.h> signals this.
#if defined(__WXMSW__) || defined(__WXGTK__) || defined(__WXOSX__)
#  define BP_HAS_WXIMAGE 1
#  include <wx/image.h>
#elif defined(wxVERSION_NUMBER)
#  define BP_HAS_WXIMAGE 1
#  include <wx/image.h>
#endif

#if defined(BP_HAVE_GDAL) || defined(USE_GDAL)
#  include <gdal.h>
#  include <gdal_priv.h>
#  include <ogr_spatialref.h>
#  define BP_USE_GDAL 1
#endif

namespace bp {

// ---------------------------------------------------------------------------
// CSV export — no external dependencies
// ---------------------------------------------------------------------------
std::string ExportManager::export_csv(const GridArray& layer,
                                       const std::string& path)
{
    if (layer.points.empty() || layer.values.empty())
        return "Layer is empty.";
    if (layer.points.size() != layer.values.size())
        return "Layer point/value size mismatch.";

    std::ofstream f(path);
    if (!f.is_open())
        return "Could not open file for writing: " + path;

    // Header
    std::string col_name = layer.layer_name.empty() ? "value" : layer.layer_name;
    f << "lat,lon," << col_name << "\n";
    f << std::fixed << std::setprecision(7);

    for (size_t i = 0; i < layer.points.size(); ++i) {
        f << layer.points[i].lat << ","
          << layer.points[i].lon << ","
          << std::setprecision(4) << layer.values[i]
          << "\n";
        if (!f) return "Write error at row " + std::to_string(i);
    }
    return {};  // success
}

// ---------------------------------------------------------------------------
// Colour ramp — same stops as grid.cpp::valueToColour()
// ---------------------------------------------------------------------------
namespace {

struct RGB { unsigned char r, g, b; };

RGB value_to_rgb(double v, double vmin, double vmax) {
    double t = (vmax > vmin) ? (v - vmin) / (vmax - vmin) : 0.5;
    t = std::max(0.0, std::min(1.0, t));

    double r, g, b;
    if (t < 1.0 / 3.0) {
        double s = t * 3.0;
        r = 0;  g = s;  b = 1.0 - s * 0.5;
    } else if (t < 2.0 / 3.0) {
        double s = (t - 1.0 / 3.0) * 3.0;
        r = s;  g = 1.0;  b = 0.5 - s * 0.5;
    } else {
        double s = (t - 2.0 / 3.0) * 3.0;
        r = 1.0;  g = 1.0 - s;  b = 0;
    }
    return {
        static_cast<unsigned char>(std::max(0, std::min(255, (int)(r * 255)))),
        static_cast<unsigned char>(std::max(0, std::min(255, (int)(g * 255)))),
        static_cast<unsigned char>(std::max(0, std::min(255, (int)(b * 255))))
    };
}

} // anonymous

// ---------------------------------------------------------------------------
// PNG export — requires wxImage
// ---------------------------------------------------------------------------
std::string ExportManager::export_png(const GridArray& layer,
                                       const std::string& path)
{
#if !defined(BP_HAS_WXIMAGE)
    (void)layer; (void)path;
    return "PNG export requires wxWidgets (not available in this build).";
#else
    if (layer.points.empty() || layer.values.empty())
        return "Layer is empty.";

    int w = layer.width;
    int h = layer.height;
    if (w <= 0 || h <= 0 || (size_t)(w * h) != layer.values.size())
        return "Layer dimensions do not match value count.";

    double vmin = *std::min_element(layer.values.begin(), layer.values.end());
    double vmax = *std::max_element(layer.values.begin(), layer.values.end());
    if (vmax == vmin) vmax = vmin + 1.0;

    wxImage img(w, h, false);
    unsigned char* data = img.GetData();

    for (int row = 0; row < h; ++row) {
        // Flip vertically: row 0 in GridArray is southernmost (lat_min),
        // but PNG row 0 is top (northernmost).
        int src_row = h - 1 - row;
        for (int col = 0; col < w; ++col) {
            size_t idx = static_cast<size_t>(src_row * w + col);
            auto rgb = value_to_rgb(layer.values[idx], vmin, vmax);
            size_t px = static_cast<size_t>(row * w + col) * 3;
            data[px + 0] = rgb.r;
            data[px + 1] = rgb.g;
            data[px + 2] = rgb.b;
        }
    }

    if (!img.SaveFile(wxString::FromUTF8(path), wxBITMAP_TYPE_PNG))
        return "Failed to save PNG: " + path;
    return {};
#endif
}

// ---------------------------------------------------------------------------
// GeoTIFF export — requires GDAL
// ---------------------------------------------------------------------------
std::string ExportManager::export_geotiff(const GridArray& layer,
                                           const std::string& path)
{
#ifndef BP_USE_GDAL
    (void)layer; (void)path;
    return "GeoTIFF export requires GDAL (not available in this build).";
#else
    if (layer.points.empty() || layer.values.empty())
        return "Layer is empty.";

    int w = layer.width;
    int h = layer.height;
    if (w <= 0 || h <= 0 || (size_t)(w * h) != layer.values.size())
        return "Layer dimensions do not match value count.";

    GDALAllRegister();

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!driver)
        return "GTiff GDAL driver not available.";

    GDALDataset* ds = driver->Create(path.c_str(), w, h, 1, GDT_Float32, nullptr);
    if (!ds)
        return "Could not create GeoTIFF: " + path;

    // Geotransform: [ulx, xres, xskew, uly, yskew, yres(negative)]
    // Grid points represent the centres of cells; extend half a cell in each direction.
    double xres = (w > 1) ? (layer.lon_max - layer.lon_min) / (w - 1) : layer.resolution_km / 111.0;
    double yres = (h > 1) ? (layer.lat_max - layer.lat_min) / (h - 1) : layer.resolution_km / 110.574;
    double gt[6] = {
        layer.lon_min - xres * 0.5,   // top-left corner lon
        xres,                           // pixel width (lon)
        0.0,
        layer.lat_max + yres * 0.5,   // top-left corner lat
        0.0,
        -yres                           // pixel height (negative = N→S)
    };
    ds->SetGeoTransform(gt);

    OGRSpatialReference srs;
    srs.SetWellKnownGeogCS("WGS84");
    char* wkt = nullptr;
    srs.exportToWkt(&wkt);
    if (wkt) {
        ds->SetProjection(wkt);
        CPLFree(wkt);
    }

    GDALRasterBand* band = ds->GetRasterBand(1);
    band->SetNoDataValue(-9999.0);

    // Write rows top-to-bottom (lat_max → lat_min)
    std::vector<float> row_buf(static_cast<size_t>(w));
    for (int row = 0; row < h; ++row) {
        int src_row = h - 1 - row;  // flip: row 0 of GridArray is lat_min
        for (int col = 0; col < w; ++col) {
            size_t idx = static_cast<size_t>(src_row * w + col);
            row_buf[static_cast<size_t>(col)] = static_cast<float>(layer.values[idx]);
        }
        CPLErr err = band->RasterIO(GF_Write, 0, row, w, 1,
                                     row_buf.data(), w, 1,
                                     GDT_Float32, 0, 0);
        if (err != CE_None) {
            GDALClose(ds);
            return "GDALRasterIO write error at row " + std::to_string(row);
        }
    }

    GDALClose(ds);
    return {};
#endif
}

// ---------------------------------------------------------------------------
// HTML report — self-contained, no external dependencies
// ---------------------------------------------------------------------------
std::string ExportManager::export_html(const GridData& data,
                                       const Scenario& scenario,
                                       const std::string& path)
{
    std::ofstream f(path);
    if (!f) return "Cannot open file for writing: " + path;

    const double c = 299'792'458.0;
    double f1_khz = scenario.frequencies.f1_hz / 1000.0;
    double f2_khz = scenario.frequencies.f2_hz / 1000.0;
    double lw1_m  = c / scenario.frequencies.f1_hz;
    double lw2_m  = c / scenario.frequencies.f2_hz;

    // Layer descriptions
    static const std::pair<const char*, const char*> LAYER_DESC[] = {
        {"groundwave",                "Groundwave field strength [dBµV/m] — ITU P.368 + Millington"},
        {"skywave",                   "Median night-time skywave field strength [dBµV/m] — ITU P.684"},
        {"atm_noise",                 "Atmospheric noise floor [dBµV/m] — ITU P.372"},
        {"snr",                       "Signal-to-noise ratio per transmitter [dB]"},
        {"sgr",                       "Skywave-to-groundwave ratio [dB]"},
        {"gdr",                       "Groundwave-to-disturbance ratio [dB]"},
        {"whdop",                     "Weighted HDOP — geometry quality (lower = better)"},
        {"repeatable",                "Repeatable accuracy 1-sigma [m]  (σ_p = σ_d × WHDOP)"},
        {"asf",                       "Additional Secondary Factor — mean ASF [millilanes]"},
        {"asf_gradient",              "ASF spatial gradient magnitude [ml/km] — monitor siting aid"},
        {"absolute_accuracy",         "Absolute position error [m] from uncorrected VL fix"},
        {"absolute_accuracy_corrected","Absolute position error [m] with monitor po corrections applied"},
        {"absolute_accuracy_delta",   "Accuracy improvement from corrections [m]  (positive = better)"},
        {"confidence",                "Confidence factor [0–1]  (1.0 = best, 0 = no fix)"},
        {"coverage",                  "Binary coverage: all criteria satisfied"},
    };

    auto layer_desc = [&](const std::string& name) -> std::string {
        for (auto& p : LAYER_DESC)
            if (p.first == name) return p.second;
        return name;
    };

    std::ostringstream ss;

    ss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
       << "<meta charset=\"UTF-8\">\n"
       << "<title>BANDPASS II Report — " << scenario.name << "</title>\n"
       << "<style>\n"
       << "body{font-family:sans-serif;margin:2em;color:#222;}\n"
       << "h1{color:#1a5276;}h2{color:#2471a3;border-bottom:1px solid #aed6f1;}\n"
       << "table{border-collapse:collapse;margin-bottom:1.5em;width:100%;}\n"
       << "th,td{border:1px solid #aaa;padding:0.4em 0.8em;text-align:left;}\n"
       << "th{background:#d6eaf8;}tr:nth-child(even){background:#f2f9ff;}\n"
       << ".warn{color:#a94442;font-weight:bold;}\n"
       << ".stat-table td:nth-child(n+2){text-align:right;font-family:monospace;}\n"
       << "</style>\n</head>\n<body>\n";

    ss << "<h1>BANDPASS II — Scenario Report</h1>\n";

    // ---- Scenario parameters
    ss << "<h2>Scenario Parameters</h2>\n"
       << "<table><tr><th>Parameter</th><th>Value</th></tr>\n"
       << "<tr><td>Scenario name</td><td>" << scenario.name << "</td></tr>\n"
       << "<tr><td>F1 frequency</td><td>" << std::fixed << std::setprecision(4)
       << f1_khz << " kHz  (lane width " << std::setprecision(2) << lw1_m << " m)</td></tr>\n"
       << "<tr><td>F2 frequency</td><td>" << std::setprecision(4)
       << f2_khz << " kHz  (lane width " << std::setprecision(2) << lw2_m << " m)</td></tr>\n";
    if (!scenario.frequencies.is_standard()) {
        ss << "<tr><td colspan=\"2\" class=\"warn\">"
           << "Non-standard frequencies — po values are in configured millilanes."
           << "</td></tr>\n";
    }
    ss << "<tr><td>Grid</td><td>"
       << std::setprecision(2)
       << scenario.grid.lat_min << "°N – " << scenario.grid.lat_max << "°N, "
       << scenario.grid.lon_min << "°E – " << scenario.grid.lon_max << "°E, "
       << scenario.grid.resolution_km << " km resolution"
       << "</td></tr>\n"
       << "<tr><td>Transmitters</td><td>" << scenario.transmitters.size() << "</td></tr>\n"
       << "<tr><td>Monitor stations</td><td>" << scenario.monitor_stations.size() << "</td></tr>\n"
       << "<tr><td>Pattern offsets</td><td>" << scenario.pattern_offsets.size() << "</td></tr>\n"
       << "</table>\n";

    // ---- Transmitter table
    if (!scenario.transmitters.empty()) {
        ss << "<h2>Transmitters</h2>\n"
           << "<table><tr><th>Name</th><th>Slot</th><th>Lat</th><th>Lon</th>"
           << "<th>Power (W)</th><th>Master?</th></tr>\n";
        for (const auto& tx : scenario.transmitters) {
            ss << "<tr><td>" << tx.name << "</td><td>" << tx.slot << "</td>"
               << "<td>" << std::setprecision(5) << tx.lat << "</td>"
               << "<td>" << tx.lon << "</td>"
               << "<td>" << std::setprecision(1) << tx.power_w << "</td>"
               << "<td>" << (tx.is_master ? "Yes" : "") << "</td></tr>\n";
        }
        ss << "</table>\n";
    }

    // ---- Layer statistics
    if (!data.layers.empty()) {
        ss << "<h2>Layer Statistics</h2>\n"
           << "<table class=\"stat-table\">"
           << "<tr><th>Layer</th><th>Min</th><th>Max</th><th>Mean</th>"
           << "<th>Points</th><th>Description</th></tr>\n";

        for (const auto& [name, arr] : data.layers) {
            if (arr.values.empty()) continue;
            double vmin =  1e30, vmax = -1e30, vsum = 0.0;
            int    count = 0;
            for (double v : arr.values) {
                if (v >= 9000.0) continue;  // sentinel
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
                vsum += v;
                ++count;
            }
            ss << "<tr><td>" << name << "</td>";
            if (count > 0) {
                ss << std::fixed << std::setprecision(2)
                   << "<td>" << vmin << "</td>"
                   << "<td>" << vmax << "</td>"
                   << "<td>" << vsum / count << "</td>";
            } else {
                ss << "<td>—</td><td>—</td><td>—</td>";
            }
            ss << "<td>" << count << " / " << arr.values.size() << "</td>"
               << "<td>" << layer_desc(name) << "</td></tr>\n";
        }
        ss << "</table>\n";
    } else {
        ss << "<p><em>No computed layer data available.</em></p>\n";
    }

    // ---- Pattern offsets
    if (!scenario.pattern_offsets.empty()) {
        ss << "<h2>Pattern Offsets (po)</h2>\n"
           << "<table><tr><th>Pattern</th><th>F1+ (ml)</th><th>F1− (ml)</th>"
           << "<th>F2+ (ml)</th><th>F2− (ml)</th></tr>\n";
        for (const auto& po : scenario.pattern_offsets) {
            ss << "<tr><td>" << po.pattern << "</td>"
               << "<td>" << po.f1plus_ml  << "</td>"
               << "<td>" << po.f1minus_ml << "</td>"
               << "<td>" << po.f2plus_ml  << "</td>"
               << "<td>" << po.f2minus_ml << "</td></tr>\n";
        }
        ss << "</table>\n";
    }

    ss << "<p style=\"color:#888;font-size:0.85em;\">Generated by BANDPASS II.</p>\n"
       << "</body>\n</html>\n";

    f << ss.str();
    if (!f) return "Write error: " + path;
    return {};
}

} // namespace bp
