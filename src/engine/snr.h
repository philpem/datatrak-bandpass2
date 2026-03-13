#pragma once
#include "grid.h"
#include "../model/Scenario.h"
#include <atomic>

namespace bp {

// SNR per station per grid point (dB).
// Combines groundwave field strength with noise floor.
// snr_db = E_gw - E_noise  (both in dBuV/m)
double compute_snr_db(double E_groundwave_dbuvm,
                       double E_atm_noise_dbuvm,
                       double vehicle_noise_dbuvm);

// GDR (Groundwave-to-Disturbance Ratio, dB).
// Disturbance = RSS of skywave + vehicle noise.
double compute_gdr_db(double E_groundwave_dbuvm,
                       double E_skywave_dbuvm,
                       double E_atm_noise_dbuvm,
                       double vehicle_noise_dbuvm);

// SGR: skywave-to-groundwave ratio (dB). Used for fade assessment.
double compute_sgr_db(double E_groundwave_dbuvm, double E_skywave_dbuvm);

// Phase/range uncertainty from SNR (Williams Eq. 9.7-9.8).
// Returns one-sigma phase uncertainty in millilanes.
double phase_uncertainty_ml(double snr_db, const Frequencies& freq);

// Compute SNR, GDR, SGR GridArrays from existing groundwave/skywave/noise layers.
void computeSNR(GridData& data, const Scenario& scenario, const std::atomic<bool>& cancel);

} // namespace bp
