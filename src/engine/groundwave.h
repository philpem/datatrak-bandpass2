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

// Compute groundwave GridArray for one transmitter over the full grid.
// Writes into data->layers["groundwave_<tx_slot>"].
// Also accumulates total groundwave RSS into data->layers["groundwave"].
void computeGroundwave(GridData&              data,
                       const Scenario&        scenario,
                       const std::atomic<bool>& cancel);

} // namespace bp
