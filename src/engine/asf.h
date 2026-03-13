#pragma once
#include "grid.h"
#include "whdop.h"
#include "snr.h"
#include "../model/Scenario.h"
#include "../model/SlotPhaseResult.h"
#include <atomic>
#include <vector>

namespace bp {

// ---------------------------------------------------------------------------
// ASF (Additional Secondary Factor) — the phase delay error introduced by
// the non-free-space propagation path.  Williams (2004) Eq. 11.1.
//
// ASF at grid point P from transmitter TX:
//   asf_ml = (tau_path - tau_free) * f * 1000  millilanes
//
// where:
//   tau_path  = actual propagation time (groundwave + terrain SF delay)
//   tau_free  = free-space propagation time = dist/c
//   f         = carrier frequency (Hz)
//
// For the simplified Phase 4 implementation, we model the SF delay using
// Monteath's formula for the difference in phase velocity between the
// surface wave and free-space wave.
//
// The "absolute accuracy" map is derived from the ASF residuals after the
// Virtual Locator (VL) least-squares fix removes the systematic offset.
// ---------------------------------------------------------------------------

// Compute ASF for one TX-to-point path (millilanes at frequency freq_hz).
// dist_km     — ground distance
// sigma       — ground conductivity (S/m)
// freq_hz     — carrier frequency (from scenario.frequencies)
double asf_single_ml(double freq_hz, double dist_km, double sigma);

// Virtual Locator fix at a single point (Williams Eq. 9.9-9.12).
// Takes the set of usable stations (with their ASF values) and computes
// the weighted-least-squares position fix error in metres.
// Returns absolute position error (m, 1-sigma).
double virtual_locator_error_m(
    const std::vector<double>& asf_ml,       // ASF per station at this point
    const std::vector<StationGeometry>& geom, // geometry (azimuth, dist, SNR)
    const std::vector<int>& selected,         // indices of selected stations
    const Frequencies& freq);

// Compute ASF and absolute accuracy GridArrays.
void computeASF(GridData& data, const Scenario& scenario,
                const std::atomic<bool>& cancel);

// Compute per-slot phase at a single point (virtual receiver).
// Williams Eq. 11.1 — used for the ReceiverPanel and simulator export.
std::vector<SlotPhaseResult> computeAtPoint(
    double lat_rx, double lon_rx,
    const Scenario& scenario);

} // namespace bp
