#pragma once
#include "grid.h"
#include "../model/Scenario.h"
#include <atomic>
#include <vector>

namespace bp {

// Ground conductivity and permittivity for a surface type
struct GroundConstants {
    double sigma   = 0.005;  // conductivity S/m   (land default)
    double eps_r   = 15.0;   // relative permittivity (land default)
};

// ITU-R P.368-9 groundwave field strength for a single path.
// Returns field strength in dBμV/m.
// power_w  — ERP in watts (effective radiated power)
// dist_km  — ground distance transmitter → point (km)
// freq_hz  — carrier frequency (Hz) — from scenario.frequencies
double groundwave_field_dbuvm(double freq_hz,
                               double dist_km,
                               const GroundConstants& gc,
                               double power_w);

// Forward declaration — defined in conductivity.h
class ConductivityMap;

// Millington (1949) mixed-path groundwave field strength [dBμV/m].
//
// Implements the Millington interpolation formula for paths that cross
// conductivity boundaries (e.g. land/sea transitions).  For a homogeneous
// path (all segments the same conductivity) the result is identical to
// groundwave_field_dbuvm().
//
// Algorithm:
//   1. Sample nsamples segments along the great-circle path TX→RX.
//   2. Look up ConductivityMap at each segment midpoint.
//   3. Forward pass: E_fwd using segment conductivities in TX→RX order.
//   4. Backward pass: E_back using segment conductivities in RX→TX order.
//   5. Return the linear average: 20 log10(0.5*(lin(E_fwd)+lin(E_back))).
//
// nsamples: number of segments (default 20 for grid; use 50 for single-point).
double millington_field_dbuvm(double freq_hz,
                               double lat_tx, double lon_tx,
                               double lat_rx, double lon_rx,
                               const ConductivityMap& cond,
                               double power_w,
                               int nsamples = 20);

// Compute groundwave GridArray for one transmitter over the full grid.
// Writes into data->layers["groundwave_<tx_slot>"].
// Also accumulates total groundwave RSS into data->layers["groundwave"].
void computeGroundwave(GridData&              data,
                       const Scenario&        scenario,
                       const std::atomic<bool>& cancel);

} // namespace bp
