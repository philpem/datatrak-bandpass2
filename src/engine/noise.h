#pragma once
#include "grid.h"
#include "../model/Scenario.h"
#include <atomic>

namespace bp {

// ITU-R P.372-15 atmospheric noise floor (dBμV/m) at frequency freq_hz.
// Returns the median annual atmospheric noise field strength.
// Based on the Fam table (atmospheric radio noise) interpolated at freq.
double atm_noise_dbuvm(double freq_hz);

// Vehicle/man-made noise model (empirical, configurable).
// Returns noise field strength in dBμV/m.
double vehicle_noise_dbuvm(double noise_floor_dbuvpm);

// Compute atmospheric noise GridArray (uniform across grid — P.372 depends only on freq).
void computeAtmNoise(GridData& data, const Scenario& scenario, const std::atomic<bool>& cancel);

} // namespace bp
