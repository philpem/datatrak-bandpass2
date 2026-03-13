#include "noise.h"
#include <cmath>
#include <algorithm>

namespace bp {

// ---------------------------------------------------------------------------
// ITU-R P.372-15 atmospheric noise
//
// The Fam figure (atmospheric noise factor, dB above kT₀b) is interpolated
// from Table 1 of P.372.  For the LF band the dominant term is the median
// annual world-wide thunderstorm noise.
//
// Approximation valid 10 kHz – 30 MHz (covers our 30–300 kHz range):
//   Fam ≈ A + B·log10(f_MHz)   (from P.372 Fig.24 / NTIA curve fits)
//
// For the temperate-zone ground receiver (curve D, median):
//   Fam = 76.8 − 27.7·log10(f_MHz)   for 0.01–10 MHz  (LF–HF)
//
// Convert to field strength in dBμV/m for a short monopole in a 1 Hz BW:
//   E_n(dBμV/m) = Fam + 10·log10(kT₀) + 10·log10(f) + noise_BW_correction
//
// Practical formula (Fam + BW-normalised to 1 Hz receiver):
//   E_noise = Fam - 204 + 20·log10(f_Hz)   [dBμV/m in 1 Hz BW]
// ---------------------------------------------------------------------------
double atm_noise_dbuvm(double freq_hz) {
    double f_mhz = freq_hz / 1e6;
    // P.372 curve D (global median, ground station)
    double Fam = 76.8 - 27.7 * std::log10(f_mhz);
    // Convert: E_noise(dBμV/m) = Fam + 20·log10(f) − 204
    double E_noise = Fam + 20.0 * std::log10(freq_hz) - 204.0;
    return E_noise;
}

double vehicle_noise_dbuvm(double noise_floor_dbuvpm) {
    return noise_floor_dbuvpm;
}

void computeAtmNoise(GridData& data, const Scenario& scenario,
                     const std::atomic<bool>& cancel)
{
    auto it = data.layers.find("atm_noise");
    if (it == data.layers.end()) return;

    double e_noise = atm_noise_dbuvm(scenario.frequencies.f1_hz);
    auto& arr = it->second;
    for (auto& v : arr.values) {
        if (cancel.load()) return;
        v = e_noise;
    }
}

} // namespace bp
