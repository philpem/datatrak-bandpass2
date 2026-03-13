#pragma once
#include "grid.h"
#include "../model/Scenario.h"
#include <atomic>

namespace bp {

// ITU-R P.684-6 skywave field strength (dBμV/m) for LF/MF.
// Computes median night-time sky-wave field for one transmitter–point path.
//
// freq_hz  — carrier frequency (Hz)
// dist_km  — ground distance (km)
// power_w  — ERP (W)
// lat_tx   — transmitter latitude (WGS84 degrees)
// lat_rx   — receiver latitude (WGS84 degrees)
double skywave_field_dbuvm(double freq_hz,
                            double dist_km,
                            double power_w,
                            double lat_tx,
                            double lat_rx);

// Compute skywave GridArray for all transmitters.
void computeSkywave(GridData& data, const Scenario& scenario,
                    const std::atomic<bool>& cancel);

} // namespace bp
