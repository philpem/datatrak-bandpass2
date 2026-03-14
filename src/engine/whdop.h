#pragma once
#include "grid.h"
#include "snr.h"
#include "../model/Scenario.h"
#include <vector>
#include <atomic>

namespace bp {

// Per-station SNR and geometry data for one grid point
struct StationGeometry {
    int    slot       = 0;
    double lat_tx     = 0.0;
    double lon_tx     = 0.0;
    double dist_km    = 0.0;
    double azimuth_deg = 0.0;  // bearing from point to station
    double snr_db     = -200.0;
    double gdr_db     = -200.0;
    double sigma_phi_ml = 500.0;  // phase uncertainty (ml)
    bool   usable     = false;
};

// Select usable stations and compute WHDOP at one grid point.
// Returns WHDOP (dimensionless; < 3 = good geometry, > 5 = poor).
// Sentinel values:
//   -infinity : fewer than min_stations usable stations in range ("no coverage")
//   NaN       : direction-cosine matrix singular (stations geometrically collinear)
// selected_out receives the subset of stations that were used.
double compute_whdop(const std::vector<StationGeometry>& all_stations,
                     int min_stations,
                     double max_range_km,
                     std::vector<int>& selected_indices_out);

// Compute WHDOP and repeatable accuracy GridArrays.
void computeWHDOP(GridData& data, const Scenario& scenario,
                  const std::atomic<bool>& cancel);

} // namespace bp
