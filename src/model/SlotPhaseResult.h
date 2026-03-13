#pragma once

namespace bp {

struct SlotPhaseResult {
    int slot = 0;

    double pseudorange_m = 0.0;

    // Fractional phase [cycles 0.0–1.0] and lane number per component
    double f1plus_phase  = 0.0;
    int    f1plus_lane   = 0;
    double f1minus_phase = 0.0;
    int    f1minus_lane  = 0;
    double f2plus_phase  = 0.0;
    int    f2plus_lane   = 0;
    double f2minus_phase = 0.0;
    int    f2minus_lane  = 0;

    double snr_db = 0.0;
    double gdr_db = 0.0;
};

} // namespace bp
