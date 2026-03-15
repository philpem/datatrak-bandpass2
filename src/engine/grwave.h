#pragma once
#include "groundwave.h"

namespace bp {

// ITU-R P.368 GRWAVE groundwave field strength [dBµV/m].
//
// Full Sommerfeld/Wait/Norton computation for a smooth spherical Earth:
//   - Short distances: Norton flat-earth attenuation with curvature correction
//   - Long distances: spherical-earth residue series (Airy function roots)
//
// Uses the same reference field as groundwave_field_dbuvm():
//   E0 = 300 mV/m at d = 1 km for P = 1 kW (ITU-R P.368 short monopole)
//
// Parameters are identical to groundwave_field_dbuvm() for drop-in use.
// Vertical polarisation assumed (correct for Datatrak LF ground wave).
// Ground-level antennas assumed (h_tx = h_rx = 0).
//
// Reference: NTIA Report 99-368 (DeMinco), ITU-R P.368-9.
double grwave_field_dbuvm(double freq_hz,
                           double dist_km,
                           const GroundConstants& gc,
                           double power_w);

} // namespace bp
