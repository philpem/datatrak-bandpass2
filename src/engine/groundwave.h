#pragma once
#include "grid.h"
#include "../model/Scenario.h"
#include <atomic>
#include <functional>
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

// Homogeneous-path groundwave field strength [dBμV/m].
//
// Uses a single conductivity lookup at the path midpoint and applies the
// ITU P.368 empirical polynomial.  Much faster than Millington (~20x) but
// ignores land/sea transitions.
double homogeneous_field_dbuvm(double freq_hz,
                                double lat_tx, double lon_tx,
                                double lat_rx, double lon_rx,
                                const ConductivityMap& cond,
                                double power_w);

// Overload accepting a pre-computed WGS84 distance (km), avoiding the
// GeographicLib Inverse() call.
double homogeneous_field_dbuvm(double freq_hz,
                                double lat_tx, double lon_tx,
                                double lat_rx, double lon_rx,
                                const ConductivityMap& cond,
                                double power_w,
                                double precomputed_dist_km);

// Per-segment field strength function type.
// Signature matches groundwave_field_dbuvm(): (freq_hz, dist_km, gc, power_w) -> dBµV/m.
using SegmentFieldFn = double(*)(double, double, const GroundConstants&, double);

// Millington mixed-path with a user-chosen per-segment field function.
// When seg_fn == groundwave_field_dbuvm, this is identical to millington_field_dbuvm().
// When seg_fn == grwave_field_dbuvm, this uses full P.368 GRWAVE per segment.
double millington_with(double freq_hz,
                       double lat_tx, double lon_tx,
                       double lat_rx, double lon_rx,
                       const ConductivityMap& cond,
                       double power_w,
                       SegmentFieldFn seg_fn,
                       int nsamples = 20);

// Dispatching function: selects propagation model based on scenario setting.
//   Homogeneous:  single midpoint, P.368 polynomial
//   Millington:   Millington averaging, P.368 polynomial
//   GRWAVE:       Millington averaging, P.368 GRWAVE residue series
double groundwave_for_model(double freq_hz,
                            double lat_tx, double lon_tx,
                            double lat_rx, double lon_rx,
                            const ConductivityMap& cond,
                            double power_w,
                            Scenario::PropagationModel model,
                            int nsamples = 20);

// Compute groundwave GridArray for one transmitter over the full grid.
// Writes into data->layers["groundwave_<tx_slot>"].
// Also accumulates total groundwave RSS into data->layers["groundwave"].
//
// progress_fn, if provided, is called with a value 0-100 as work progresses.
// It is throttled to at most one call per percent to avoid event flooding.
void computeGroundwave(GridData&              data,
                       const Scenario&        scenario,
                       const std::atomic<bool>& cancel,
                       const std::function<void(int)>& progress_fn = {});

} // namespace bp
