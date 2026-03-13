#include "snr.h"
#include "noise.h"
#include <cmath>
#include <algorithm>

namespace bp {

// ---------------------------------------------------------------------------
// Helper: RSS two power levels given in dB
static double rss_db(double a_db, double b_db) {
    double a = std::pow(10.0, a_db / 10.0);
    double b = std::pow(10.0, b_db / 10.0);
    return 10.0 * std::log10(a + b);
}

// ---------------------------------------------------------------------------
double compute_snr_db(double E_gw_dbuvm,
                       double E_atm_dbuvm,
                       double veh_noise_dbuvm)
{
    // Total noise = RSS of atmospheric + vehicle noise
    double E_noise = rss_db(E_atm_dbuvm, veh_noise_dbuvm);
    return E_gw_dbuvm - E_noise;
}

double compute_gdr_db(double E_gw_dbuvm,
                       double E_sky_dbuvm,
                       double E_atm_dbuvm,
                       double veh_noise_dbuvm)
{
    // Disturbance = RSS of skywave + noise
    double E_noise = rss_db(E_atm_dbuvm, veh_noise_dbuvm);
    double E_dist;
    if (E_sky_dbuvm > -199.0) {
        E_dist = rss_db(E_sky_dbuvm, E_noise);
    } else {
        E_dist = E_noise;
    }
    return E_gw_dbuvm - E_dist;
}

double compute_sgr_db(double E_gw_dbuvm, double E_sky_dbuvm) {
    if (E_sky_dbuvm <= -199.0 || E_gw_dbuvm <= -199.0) return -200.0;
    return E_sky_dbuvm - E_gw_dbuvm;
}

// ---------------------------------------------------------------------------
// Williams (2004) Eq. 9.7-9.8: phase uncertainty from SNR
//
// The standard Datatrak phase measurement uncertainty (1-sigma) in millilanes
// follows from the SNR of the correlation measurement.  The relationship is
// approximately:
//   sigma_phi_ml = k / sqrt(SNR_linear)
//
// where k is a system constant (~150 ml at the reference SNR).  For a more
// complete model, Williams derives:
//   sigma_phi = (1 / (2*pi*sqrt(2*SNR))) * 1000  millilanes
//   (for SNR >> 1, coherent measurement over one slot)
//
// This implements Eq. 9.7:  sigma_phi_ml = 1000 / (2*pi*sqrt(2*SNR))
// ---------------------------------------------------------------------------
double phase_uncertainty_ml(double snr_db, const Frequencies& /*freq*/) {
    if (snr_db < -30.0) return 500.0;  // effectively half-lane uncertainty

    double snr_linear = std::pow(10.0, snr_db / 10.0);
    // Williams Eq. 9.7: sigma_phi (cycles) = 1/(2*pi*sqrt(2*snr))
    // Convert to millilanes (* 1000)
    double sigma_ml = 1000.0 / (2.0 * M_PI * std::sqrt(2.0 * snr_linear));
    return std::min(sigma_ml, 500.0);
}

// ---------------------------------------------------------------------------
void computeSNR(GridData& data, const Scenario& scenario,
                const std::atomic<bool>& cancel)
{
    auto it_gw  = data.layers.find("groundwave");
    auto it_sky = data.layers.find("skywave");
    auto it_atm = data.layers.find("atm_noise");
    auto it_snr = data.layers.find("snr");
    auto it_gdr = data.layers.find("gdr");
    auto it_sgr = data.layers.find("sgr");

    if (it_gw == data.layers.end()) return;

    const auto& pts = it_gw->second.points;
    size_t n = pts.size();
    if (n == 0) return;

    double veh_noise = vehicle_noise_dbuvm(scenario.receiver.vehicle_noise_dbuvpm);

    for (size_t i = 0; i < n; ++i) {
        if (cancel.load()) return;

        double E_gw  = it_gw->second.values[i];
        double E_sky = (it_sky != data.layers.end()) ? it_sky->second.values[i] : -200.0;
        double E_atm = (it_atm != data.layers.end()) ? it_atm->second.values[i]
                                                      : atm_noise_dbuvm(scenario.frequencies.f1_hz);

        if (it_snr != data.layers.end())
            it_snr->second.values[i] = compute_snr_db(E_gw, E_atm, veh_noise);

        if (it_gdr != data.layers.end())
            it_gdr->second.values[i] = compute_gdr_db(E_gw, E_sky, E_atm, veh_noise);

        if (it_sgr != data.layers.end())
            it_sgr->second.values[i] = compute_sgr_db(E_gw, E_sky);
    }
}

} // namespace bp
