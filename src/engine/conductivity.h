#pragma once
#include "groundwave.h"
#include "../model/Scenario.h"
#include <string>
#include <vector>
#include <memory>

namespace bp {

// ---------------------------------------------------------------------------
// ConductivityMap — provides GroundConstants at any WGS84 lat/lon.
//
// Sources (in priority order when constructed via make_conductivity_map):
//   BuiltIn  — synthetic land/sea constants based on a simple heuristic
//   ItuP832  — raster loaded from ITU-R P.832 digital dataset (GeoTIFF)
//   BGS      — British Geological Survey conductivity map (GeoTIFF)
//   File     — user-supplied GeoTIFF with conductivity [S/m] in band 1
//              and relative permittivity in band 2 (or fixed eps_r = 15 if
//              only one band is present)
//
// All GeoTIFF sources are loaded via GDAL and bilinearly interpolated.
// ---------------------------------------------------------------------------
class ConductivityMap {
public:
    virtual ~ConductivityMap() = default;

    // Returns GroundConstants at the given WGS84 position.
    virtual GroundConstants lookup(double lat, double lon) const = 0;
};

// Built-in map: uses a simple conductivity heuristic.
// Returns sea constants (4.0 S/m, εr 70) for the main open-sea areas
// surrounding the British Isles, land values (0.005 S/m, εr 15) otherwise.
// This is a rough approximation adequate for the initial UI smoke-test;
// the ITU P.832 or GDAL rasters provide metre-level accuracy.
class BuiltInConductivityMap : public ConductivityMap {
public:
    GroundConstants lookup(double lat, double lon) const override;
};

// GeoTIFF-based conductivity map loaded via GDAL.
// Band 1: conductivity [S/m]
// Band 2 (optional): relative permittivity (dimensionless); defaults to 15.0
class GdalConductivityMap : public ConductivityMap {
public:
    // Throws std::runtime_error on GDAL open/read failure.
    explicit GdalConductivityMap(const std::string& path);
    ~GdalConductivityMap() override;

    GroundConstants lookup(double lat, double lon) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Factory: creates the appropriate ConductivityMap from scenario settings.
// Returns a BuiltInConductivityMap if the file source cannot be opened.
std::unique_ptr<ConductivityMap> make_conductivity_map(const Scenario& scenario);

// ---------------------------------------------------------------------------
// CachedConductivityMap — wraps any ConductivityMap with an in-memory tile
// cache that pre-reads a rectangular region at a fixed resolution.
//
// Lookups within the cached region use fast bilinear interpolation from the
// in-memory grid (~1 ns per call).  Lookups outside the region fall through
// to the underlying map.
//
// Typical usage: wrap the GdalConductivityMap covering the scenario grid bounds
// extended by 2 degrees (to cover Millington/Monteath segment midpoints that
// may lie outside the grid).
//
// Memory: ~16 bytes per cell × (lat_steps × lon_steps).  At 0.01° resolution
// over a 15°×12° area: 1500 × 1200 = 1.8M cells = ~29 MB.  At 0.02° res:
// ~7 MB.
// ---------------------------------------------------------------------------
class CachedConductivityMap : public ConductivityMap {
public:
    // Preloads the region [lat_min, lat_max] × [lon_min, lon_max] from `source`
    // at the given resolution (degrees).
    CachedConductivityMap(std::unique_ptr<ConductivityMap> source,
                          double lat_min, double lat_max,
                          double lon_min, double lon_max,
                          double resolution_deg = 0.02);

    GroundConstants lookup(double lat, double lon) const override;

private:
    std::unique_ptr<ConductivityMap> source_;
    double lat_min_, lon_min_;
    double inv_res_;           // 1.0 / resolution_deg
    int    cols_, rows_;
    std::vector<GroundConstants> grid_;  // row-major, row 0 = lat_min
};

// Factory: creates a CachedConductivityMap wrapping the scenario's source,
// pre-reading the grid bounds extended by 2° in each direction.
std::unique_ptr<ConductivityMap> make_cached_conductivity_map(const Scenario& scenario);

} // namespace bp
