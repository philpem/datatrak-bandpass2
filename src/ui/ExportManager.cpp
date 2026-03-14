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

} // namespace bp
