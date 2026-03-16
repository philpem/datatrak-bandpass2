#include "conductivity.h"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <stdexcept>
#include <cmath>
#include <mutex>

namespace bp {

// ---------------------------------------------------------------------------
// BuiltInConductivityMap
//
// Simple land/sea heuristic for the region bounded by the North Atlantic and
// North Sea around the British Isles.  Sea cells return ITU median sea values;
// land cells return ITU median temperate land values.
//
// The heuristic checks a small set of bounding polygons that exclude the main
// land masses (UK + Ireland + continental Europe) from the sea mask.
// ---------------------------------------------------------------------------

// Returns true if (lat, lon) is most likely an open-sea cell for the purpose
// of setting conductivity.  The rule is deliberately liberal — if in doubt
// it is better to under-estimate attenuation (treat as sea) than to
// over-estimate it (treat as land in a sea cell), because sea propagation
// is a more favourable channel and the UI gives a conservative coverage view.
static bool is_sea_cell(double lat, double lon) {
    // Roughly the UK + Ireland mainland bounding box
    if (lat >= 49.8 && lat <= 61.0 && lon >= -10.5 && lon <= 2.0) {
        // Exclude the land mass using a simplified polygon chain.
        // The check is intentionally coarse — it just removes the main
        // overland cells from the sea category.
        // GB mainland: east of -6 and west of 2 between 50 and 61 N
        if (lon >= -6.0 && lon <= 2.0 && lat >= 50.0 && lat <= 61.0)
            return false;   // treat as land
        // Ireland: between -10.5 and -5.5, 51.3 to 55.5
        if (lon >= -10.5 && lon <= -5.5 && lat >= 51.3 && lat <= 55.5)
            return false;
    }
    // Continental Europe: east of 2°E and north of 43°N
    if (lon >= 2.0 && lat >= 43.0)
        return false;
    // North Africa: south of 37°N
    if (lat <= 37.0)
        return false;

    // Remaining cells in the North Atlantic / North Sea / English Channel
    // bounding box are treated as sea.  Extend west to -30° to cover the
    // full open-Atlantic area that UK LF networks can reach.
    if (lat >= 48.0 && lat <= 62.0 && lon >= -30.0 && lon <= 10.0)
        return true;

    return false;
}

GroundConstants BuiltInConductivityMap::lookup(double lat, double lon) const {
    if (is_sea_cell(lat, lon))
        return { 4.0, 70.0 };    // ITU sea: 4 S/m, εr 70
    return { 0.005, 15.0 };      // ITU temperate land: 5 mS/m, εr 15
}

// ---------------------------------------------------------------------------
// GdalConductivityMap — bilinear interpolation from a GeoTIFF
// ---------------------------------------------------------------------------

struct GdalConductivityMap::Impl {
    GDALDataset* ds = nullptr;
    double       geo[6] = {};      // GeoTransform: origin + pixel size
    double       inv[6] = {};      // Inverted GeoTransform
    int          raster_x = 0;
    int          raster_y = 0;
    bool         has_eps  = false; // second band available?
    mutable std::mutex mtx;        // GDAL is not thread-safe for same dataset

    // Read a single bilinearly-interpolated value from the given band (1-based).
    // Returns nodata_val if the position is outside the raster extent.
    double read_bilinear(int band_idx, double pixel_x, double pixel_y) const;

    ~Impl() { if (ds) GDALClose(ds); }
};

static void invert_geotransform(const double* gt, double* inv) {
    // Standard 2×2 inversion for GeoTransform without rotation
    double det = gt[1] * gt[5] - gt[2] * gt[4];
    if (std::abs(det) < 1e-30) {
        // Degenerate — return identity
        inv[0] = 0; inv[1] = 1; inv[2] = 0;
        inv[3] = 0; inv[4] = 0; inv[5] = 1;
        return;
    }
    inv[1] =  gt[5] / det;
    inv[2] = -gt[2] / det;
    inv[4] = -gt[4] / det;
    inv[5] =  gt[1] / det;
    inv[0] = -gt[0] * inv[1] - gt[3] * inv[2];
    inv[3] = -gt[0] * inv[4] - gt[3] * inv[5];
}

double GdalConductivityMap::Impl::read_bilinear(int band_idx, double px, double py) const {
    // Bilinear interpolation at (px, py) in pixel coordinates.
    int x0 = (int)std::floor(px - 0.5);
    int y0 = (int)std::floor(py - 0.5);
    double fx = (px - 0.5) - x0;   // fractional part in [0,1)
    double fy = (py - 0.5) - y0;

    auto read_px = [&](int xi, int yi) -> double {
        xi = std::max(0, std::min(raster_x - 1, xi));
        yi = std::max(0, std::min(raster_y - 1, yi));
        float val = 0.0f;
        GDALRasterBand* rb = ds->GetRasterBand(band_idx);
        if (rb->RasterIO(GF_Read, xi, yi, 1, 1, &val, 1, 1, GDT_Float32, 0, 0) != CE_None)
            return (band_idx == 1) ? 0.005 : 15.0;  // read error → land default
        // Replace nodata with land default
        int ok = 0;
        double nd = rb->GetNoDataValue(&ok);
        if (ok && std::abs((double)val - nd) < 1e-6) return (band_idx == 1) ? 0.005 : 15.0;
        return (double)val;
    };

    double v00 = read_px(x0,   y0);
    double v10 = read_px(x0+1, y0);
    double v01 = read_px(x0,   y0+1);
    double v11 = read_px(x0+1, y0+1);

    return v00 * (1-fx)*(1-fy) + v10 * fx*(1-fy)
         + v01 * (1-fx)*fy     + v11 * fx*fy;
}

GdalConductivityMap::GdalConductivityMap(const std::string& path)
    : impl_(std::make_unique<Impl>())
{
    GDALAllRegister();
    impl_->ds = (GDALDataset*)GDALOpen(path.c_str(), GA_ReadOnly);
    if (!impl_->ds)
        throw std::runtime_error("ConductivityMap: cannot open " + path);

    impl_->ds->GetGeoTransform(impl_->geo);
    invert_geotransform(impl_->geo, impl_->inv);
    impl_->raster_x = impl_->ds->GetRasterXSize();
    impl_->raster_y = impl_->ds->GetRasterYSize();
    impl_->has_eps  = (impl_->ds->GetRasterCount() >= 2);
}

GdalConductivityMap::~GdalConductivityMap() = default;

GroundConstants GdalConductivityMap::lookup(double lat, double lon) const {
    std::lock_guard<std::mutex> lock(impl_->mtx);

    // Convert WGS84 lon/lat → pixel coordinates using inverted GeoTransform
    double px = impl_->inv[0] + impl_->inv[1]*lon + impl_->inv[2]*lat;
    double py = impl_->inv[3] + impl_->inv[4]*lon + impl_->inv[5]*lat;

    double sigma = impl_->read_bilinear(1, px, py);
    if (sigma <= 0.0) sigma = 0.005;    // nodata → land default

    double eps_r = 15.0;
    if (impl_->has_eps) {
        eps_r = impl_->read_bilinear(2, px, py);
        if (eps_r <= 0.0) eps_r = 15.0;
    }
    return { sigma, eps_r };
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<ConductivityMap> make_conductivity_map(const Scenario& scenario) {
    using CS = Scenario::ConductivitySource;

    if (scenario.conductivity_source == CS::BuiltIn ||
        scenario.conductivity_file.empty()) {
        return std::make_unique<BuiltInConductivityMap>();
    }

    try {
        return std::make_unique<GdalConductivityMap>(scenario.conductivity_file);
    } catch (...) {
        // Fall back to built-in if file cannot be opened
        return std::make_unique<BuiltInConductivityMap>();
    }
}

// ---------------------------------------------------------------------------
// CachedConductivityMap
// ---------------------------------------------------------------------------
CachedConductivityMap::CachedConductivityMap(
    std::unique_ptr<ConductivityMap> source,
    double lat_min, double lat_max,
    double lon_min, double lon_max,
    double resolution_deg)
    : source_(std::move(source))
    , lat_min_(lat_min)
    , lon_min_(lon_min)
    , inv_res_(1.0 / resolution_deg)
{
    rows_ = std::max(1, (int)std::ceil((lat_max - lat_min) * inv_res_) + 1);
    cols_ = std::max(1, (int)std::ceil((lon_max - lon_min) * inv_res_) + 1);
    grid_.resize((size_t)rows_ * cols_);

    for (int r = 0; r < rows_; ++r) {
        double lat = lat_min + r / inv_res_;
        for (int c = 0; c < cols_; ++c) {
            double lon = lon_min + c / inv_res_;
            grid_[(size_t)r * cols_ + c] = source_->lookup(lat, lon);
        }
    }
}

GroundConstants CachedConductivityMap::lookup(double lat, double lon) const {
    double fr = (lat - lat_min_) * inv_res_;
    double fc = (lon - lon_min_) * inv_res_;

    // Clamp to cache bounds — avoids falling through to raw GDAL which is
    // not thread-safe.  The cache already extends 2° beyond the grid, so
    // edge values are a safe approximation for the rare out-of-bounds lookup.
    fr = std::max(0.0, std::min(fr, (double)(rows_ - 1) - 1e-9));
    fc = std::max(0.0, std::min(fc, (double)(cols_ - 1) - 1e-9));

    int r0 = (int)fr;
    int c0 = (int)fc;
    double tr = fr - r0;
    double tc = fc - c0;

    // Bilinear interpolation
    const auto& v00 = grid_[(size_t)r0       * cols_ + c0    ];
    const auto& v10 = grid_[(size_t)r0       * cols_ + c0 + 1];
    const auto& v01 = grid_[(size_t)(r0 + 1) * cols_ + c0    ];
    const auto& v11 = grid_[(size_t)(r0 + 1) * cols_ + c0 + 1];

    GroundConstants result;
    result.sigma = v00.sigma * (1-tr)*(1-tc) + v10.sigma * (1-tr)*tc
                 + v01.sigma * tr*(1-tc)     + v11.sigma * tr*tc;
    result.eps_r = v00.eps_r * (1-tr)*(1-tc) + v10.eps_r * (1-tr)*tc
                 + v01.eps_r * tr*(1-tc)     + v11.eps_r * tr*tc;
    return result;
}

std::unique_ptr<ConductivityMap> make_cached_conductivity_map(const Scenario& scenario) {
    auto base = make_conductivity_map(scenario);

    // Extend grid bounds by 2° to cover Millington/Monteath segment midpoints
    double lat_min = scenario.grid.lat_min - 2.0;
    double lat_max = scenario.grid.lat_max + 2.0;
    double lon_min = scenario.grid.lon_min - 2.0;
    double lon_max = scenario.grid.lon_max + 2.0;

    return std::make_unique<CachedConductivityMap>(
        std::move(base), lat_min, lat_max, lon_min, lon_max, 0.02);
}

} // namespace bp
