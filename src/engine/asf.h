#pragma once
#include "grid.h"
#include "whdop.h"
#include "snr.h"
#include "monteath.h"
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
//   tau_path  = actual propagation time (groundwave + Monteath SF delay)
//   tau_free  = free-space propagation time = dist/c
//   f         = carrier frequency (Hz)
//
// The "absolute accuracy" map is derived from the error of the Virtual Locator
// (VL) weighted least-squares fix: a simulated receiver at each grid point
// uses the Monteath-ASF-biased pseudoranges and the Airy 1830 ellipsoid
// (matching Mk4 firmware) to fix its position.  The absolute accuracy is the
// distance between the VL fix and the true grid point position.
// ---------------------------------------------------------------------------

// Compute ASF and absolute accuracy GridArrays.
void computeASF(GridData& data, const Scenario& scenario,
                const std::atomic<bool>& cancel);

// Compute per-slot phase at a single point (virtual receiver).
// Williams Eq. 11.1 — used for the ReceiverPanel and simulator export.
std::vector<SlotPhaseResult> computeAtPoint(
    double lat_rx, double lon_rx,
    const Scenario& scenario);

} // namespace bp
