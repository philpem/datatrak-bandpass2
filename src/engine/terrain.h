#pragma once
#include "../model/Scenario.h"
#include <string>
#include <vector>
#include <memory>

namespace bp {

// ---------------------------------------------------------------------------
// HeightPoint — a single point on a terrain profile.
// ---------------------------------------------------------------------------
struct HeightPoint {
    double lat;         // WGS84 latitude  [°]
    double lon;         // WGS84 longitude [°]
    double height_m;    // above WGS84 ellipsoid [m]; 0 if flat/unavailable
    double dist_km;     // cumulative ground distance from profile start [km]
};

// ---------------------------------------------------------------------------
// TerrainMap — provides height data along a great-circle path.
//
// Sources:
//   Flat  — all heights zero (default, always available)
//   SRTM  — 3-arc-second HGT tiles auto-loaded from terrain_dir
//   File  — single GDAL GeoTIFF; bilinearly interpolated
// ---------------------------------------------------------------------------
class TerrainMap {
public:
    virtual ~TerrainMap() = default;

    // Elevation at a single point [m above ellipsoid].
    virtual double height_at(double lat, double lon) const = 0;

    // Sample a terrain profile from (lat1,lon1) to (lat2,lon2) with
    // approximately nsamples equally-spaced points along the great circle.
    // The first and last points always coincide with the endpoints.
    std::vector<HeightPoint> profile(double lat1, double lon1,
                                     double lat2, double lon2,
                                     int nsamples = 50) const;
};

// Always returns 0 m.
class FlatTerrainMap : public TerrainMap {
public:
    double height_at(double /*lat*/, double /*lon*/) const override { return 0.0; }
};

// GDAL-based terrain map (SRTM HGT tile or GeoTIFF).
class GdalTerrainMap : public TerrainMap {
public:
    // For SRTM mode, path is the directory containing *.hgt files.
    // For File mode, path is the GeoTIFF filename.
    explicit GdalTerrainMap(const std::string& path, bool srtm_directory = false);
    ~GdalTerrainMap() override;

    double height_at(double lat, double lon) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Factory: creates the appropriate TerrainMap from scenario settings.
std::unique_ptr<TerrainMap> make_terrain_map(const Scenario& scenario);

} // namespace bp
