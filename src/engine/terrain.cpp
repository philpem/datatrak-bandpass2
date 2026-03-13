#include "terrain.h"
#include <GeographicLib/Geodesic.hpp>
#include <GeographicLib/GeodesicLine.hpp>
#include <stdexcept>
#include <cmath>
#include <cstdio>

#ifdef USE_GDAL
#include <gdal_priv.h>
#include <gdal.h>
#include <filesystem>
#include <unordered_map>
#endif

namespace bp {

// ---------------------------------------------------------------------------
// TerrainMap::profile  — shared implementation
// ---------------------------------------------------------------------------
std::vector<HeightPoint> TerrainMap::profile(double lat1, double lon1,
                                              double lat2, double lon2,
                                              int nsamples) const {
    if (nsamples < 2) nsamples = 2;
    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();

    double total_dist_m = 0.0;
    geod.Inverse(lat1, lon1, lat2, lon2, total_dist_m);
    double total_km = total_dist_m / 1000.0;

    GeographicLib::GeodesicLine line =
        geod.InverseLine(lat1, lon1, lat2, lon2);

    std::vector<HeightPoint> pts;
    pts.reserve(nsamples);

    for (int i = 0; i < nsamples; ++i) {
        double frac = (nsamples == 1) ? 0.0 : (double)i / (nsamples - 1);
        double dist_m = frac * total_dist_m;
        double lat = 0.0, lon = 0.0;
        line.Position(dist_m, lat, lon);
        HeightPoint hp;
        hp.lat      = lat;
        hp.lon      = lon;
        hp.height_m = height_at(lat, lon);
        hp.dist_km  = frac * total_km;
        pts.push_back(hp);
    }
    return pts;
}

// ---------------------------------------------------------------------------
// GdalTerrainMap
// ---------------------------------------------------------------------------

#ifdef USE_GDAL

struct GdalTerrainMap::Impl {
    // For GeoTIFF mode: single open dataset
    GDALDataset* ds = nullptr;
    double geo[6]   = {};
    double inv[6]   = {};
    int    rx = 0, ry = 0;

    // For SRTM directory mode: cache of open HGT datasets keyed by filename
    bool srtm_dir = false;
    std::string dir_path;
    mutable std::unordered_map<std::string, GDALDataset*> tile_cache;

    ~Impl() {
        if (ds) GDALClose(ds);
        for (auto& kv : tile_cache) GDALClose(kv.second);
    }

    // Build SRTM filename for given 1°×1° tile (lower-left corner)
    static std::string srtm_filename(int lat_floor, int lon_floor) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%c%02d%c%03d.hgt",
                      lat_floor >= 0 ? 'N' : 'S', std::abs(lat_floor),
                      lon_floor >= 0 ? 'E' : 'W', std::abs(lon_floor));
        return buf;
    }
};

static void invert_gt(const double* gt, double* inv) {
    double det = gt[1]*gt[5] - gt[2]*gt[4];
    if (std::abs(det) < 1e-30) {
        inv[0]=0; inv[1]=1; inv[2]=0; inv[3]=0; inv[4]=0; inv[5]=1; return;
    }
    inv[1] =  gt[5]/det;  inv[2] = -gt[2]/det;
    inv[4] = -gt[4]/det;  inv[5] =  gt[1]/det;
    inv[0] = -gt[0]*inv[1] - gt[3]*inv[2];
    inv[3] = -gt[0]*inv[4] - gt[3]*inv[5];
}

static double bilinear_read(GDALDataset* ds, const double* inv,
                             int rx, int ry, double lon, double lat) {
    double px = inv[0] + inv[1]*lon + inv[2]*lat;
    double py = inv[3] + inv[4]*lon + inv[5]*lat;

    int x0 = (int)std::floor(px - 0.5);
    int y0 = (int)std::floor(py - 0.5);
    double fx = (px-0.5) - x0;
    double fy = (py-0.5) - y0;

    GDALRasterBand* rb = ds->GetRasterBand(1);
    int ok = 0;
    double nodata = rb->GetNoDataValue(&ok);

    auto read_px = [&](int xi, int yi) -> double {
        xi = std::max(0, std::min(rx-1, xi));
        yi = std::max(0, std::min(ry-1, yi));
        float v = 0.f;
        rb->RasterIO(GF_Read, xi, yi, 1, 1, &v, 1, 1, GDT_Float32, 0, 0);
        if (ok && std::abs((double)v - nodata) < 0.5) return 0.0;
        return (double)v;
    };

    return read_px(x0,   y0)   * (1-fx)*(1-fy)
         + read_px(x0+1, y0)   * fx*(1-fy)
         + read_px(x0,   y0+1) * (1-fx)*fy
         + read_px(x0+1, y0+1) * fx*fy;
}

GdalTerrainMap::GdalTerrainMap(const std::string& path, bool srtm_directory)
    : impl_(std::make_unique<Impl>())
{
    GDALAllRegister();
    impl_->srtm_dir = srtm_directory;

    if (srtm_directory) {
        impl_->dir_path = path;
        // Verify directory exists
        if (!std::filesystem::is_directory(path))
            throw std::runtime_error("TerrainMap: SRTM directory not found: " + path);
    } else {
        impl_->ds = (GDALDataset*)GDALOpen(path.c_str(), GA_ReadOnly);
        if (!impl_->ds)
            throw std::runtime_error("TerrainMap: cannot open " + path);
        impl_->ds->GetGeoTransform(impl_->geo);
        invert_gt(impl_->geo, impl_->inv);
        impl_->rx = impl_->ds->GetRasterXSize();
        impl_->ry = impl_->ds->GetRasterYSize();
    }
}

GdalTerrainMap::~GdalTerrainMap() = default;

double GdalTerrainMap::height_at(double lat, double lon) const {
    if (!impl_->srtm_dir) {
        if (!impl_->ds) return 0.0;
        return bilinear_read(impl_->ds, impl_->inv, impl_->rx, impl_->ry, lon, lat);
    }

    // SRTM directory mode: find the correct 1°×1° tile
    int lat_fl = (int)std::floor(lat);
    int lon_fl = (int)std::floor(lon);
    std::string fname = Impl::srtm_filename(lat_fl, lon_fl);
    std::string fpath = impl_->dir_path + "/" + fname;

    auto it = impl_->tile_cache.find(fname);
    if (it == impl_->tile_cache.end()) {
        GDALDataset* td = (GDALDataset*)GDALOpen(fpath.c_str(), GA_ReadOnly);
        impl_->tile_cache[fname] = td;   // may be nullptr if tile absent
        if (!td) return 0.0;
        it = impl_->tile_cache.find(fname);
    }
    GDALDataset* td = it->second;
    if (!td) return 0.0;

    // HGT: 1201×1201 or 3601×3601 samples; compute geo transform on the fly
    int nx = td->GetRasterXSize();
    int ny = td->GetRasterYSize();
    double step = 1.0 / (nx - 1);
    // HGT geo: top-left = (lat_fl+1, lon_fl), pixel spacing = step
    double geo[6] = { (double)lon_fl, step, 0,
                      (double)(lat_fl + 1), 0, -step };
    double inv[6];
    invert_gt(geo, inv);
    return bilinear_read(td, inv, nx, ny, lon, lat);
}

#else // !USE_GDAL

struct GdalTerrainMap::Impl {};

GdalTerrainMap::GdalTerrainMap(const std::string& path, bool)
    : impl_(std::make_unique<Impl>())
{
    (void)path;
    throw std::runtime_error("TerrainMap: GDAL support not compiled in");
}

GdalTerrainMap::~GdalTerrainMap() = default;

double GdalTerrainMap::height_at(double /*lat*/, double /*lon*/) const {
    return 0.0;
}

#endif // USE_GDAL

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<TerrainMap> make_terrain_map(const Scenario& scenario) {
    using TS = Scenario::TerrainSource;

    if (scenario.terrain_source == TS::Flat || scenario.terrain_file.empty())
        return std::make_unique<FlatTerrainMap>();

    bool srtm_dir = (scenario.terrain_source == TS::SRTM);
    try {
        return std::make_unique<GdalTerrainMap>(scenario.terrain_file, srtm_dir);
    } catch (...) {
        return std::make_unique<FlatTerrainMap>();
    }
}

} // namespace bp
