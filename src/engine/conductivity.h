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

} // namespace bp
