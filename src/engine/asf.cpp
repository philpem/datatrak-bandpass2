#include "asf.h"
#include "groundwave.h"
#include "skywave.h"
#include "noise.h"
#include "snr.h"
#include "whdop.h"
#include "../coords/Osgb.h"
#include <GeographicLib/Geodesic.hpp>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace bp {

// ---------------------------------------------------------------------------
// ASF (Additional Secondary Factor) computation
//
// The groundwave phase velocity over lossy ground is SLOWER than free-space.
// This introduces a phase delay (ASF) that accumulates along the path.
//
// Williams (2004) Eq. 11.1, following Monteath (1973):
//   ASF delay ≈ (d/c) × Re(1 - 1/n_eff)  [seconds]
//   → asf_ml = ASF_delay × freq_hz × 1000  [millilanes]
//
// For a surface wave over a ground with surface impedance η:
//   n_eff ≈ 1 + 0.5 × |η|²   (first-order approximation, real part only)
//
// Where η = 1/sqrt(εᵣ - j×σ/(ωε₀)) is the normalised surface impedance.
//
// This gives:
//   ASF_ml ≈ (dist_m × freq_hz / c) × 500 × |η|²  millilanes
//
// For typical land (σ=0.005, εᵣ=15) at 146 kHz:
//   |η|² ≈ 1/(εᵣ²+x²)^0.5 ≈ 1/614 ≈ 1.6×10⁻³ for x=σ/(ωε₀)≈614
//   → ~2 ml/km per station, accumulating over the path length
// ---------------------------------------------------------------------------
double asf_single_ml(double freq_hz, double dist_km, double sigma) {
    const double c   = 299'792'458.0;
    const double eps0 = 8.854187817e-12;
    const double eps_r = 15.0;  // typical land permittivity

    double omega = 2.0 * M_PI * freq_hz;
    double x     = sigma / (omega * eps0);
    // |η|² = 1/|ε_c| = 1/sqrt(εᵣ²+x²)
    double eta2  = 1.0 / std::sqrt(eps_r * eps_r + x * x);

    // ASF delay (seconds)
    double tau_asf = (dist_km * 1000.0 / c) * 0.5 * eta2;
    // Convert to millilanes
    return tau_asf * freq_hz * 1000.0;
}

// ---------------------------------------------------------------------------
// Virtual Locator least-squares position fix error
//
// Williams (2004) Eq. 9.9-9.12:
//   The VL uses weighted least-squares to estimate position from measured
//   pseudoranges.  The residual after the fix represents absolute accuracy.
//
//   Error = sqrt(trace((A^T W A)^{-1}) × sigma_asf²)
//   where sigma_asf is the RMS ASF error across selected stations.
//
// For Phase 4, sigma_asf is derived from the ASF gradient model.
// ---------------------------------------------------------------------------
double virtual_locator_error_m(
    const std::vector<double>& asf_ml,
    const std::vector<StationGeometry>& geom,
    const std::vector<int>& selected,
    const Frequencies& freq)
{
    if (selected.empty()) return 9999.0;

    const double c = 299'792'458.0;
    // Convert ASF from millilanes to metres
    double lane_m = c / freq.f1_hz;  // metres per lane

    // Compute RMS ASF in metres across selected stations
    double rms_asf_sq = 0.0;
    for (int idx : selected) {
        double asf_m = asf_ml[idx] * lane_m / 1000.0;  // ml → m
        rms_asf_sq += asf_m * asf_m;
    }
    double sigma_asf = std::sqrt(rms_asf_sq / selected.size());

    // Geometric dilution: WHDOP scales the position error
    // (reuse the matrix from whdop.h for the selected stations)
    double snr_sum = 0.0;
    for (int idx : selected) snr_sum += std::pow(10.0, geom[idx].snr_db / 10.0);
    if (snr_sum <= 0.0) return 9999.0;

    double awat00 = 0.0, awat01 = 0.0, awat11 = 0.0;
    for (int idx : selected) {
        double w   = std::pow(10.0, geom[idx].snr_db / 10.0) / snr_sum;
        double az  = geom[idx].azimuth_deg * M_PI / 180.0;
        double ca  = std::cos(az), sa = std::sin(az);
        awat00 += w * ca * ca;
        awat01 += w * ca * sa;
        awat11 += w * sa * sa;
    }
    double det = awat00 * awat11 - awat01 * awat01;
    if (std::abs(det) < 1e-30) return 9999.0;
    double whdop = std::sqrt((awat11 + awat00) / det);

    return sigma_asf * whdop;
}

// ---------------------------------------------------------------------------
// Grid computation
// ---------------------------------------------------------------------------
void computeASF(GridData& data, const Scenario& scenario,
                const std::atomic<bool>& cancel)
{
    auto it_asf = data.layers.find("asf");
    auto it_abs = data.layers.find("absolute_accuracy");
    auto it_gw  = data.layers.find("groundwave");
    if (it_gw == data.layers.end()) return;

    const auto& pts = it_gw->second.points;
    size_t n = pts.size();
    if (n == 0) return;

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    const double veh_noise   = vehicle_noise_dbuvm(scenario.receiver.vehicle_noise_dbuvpm);
    const double atm_noise_v = atm_noise_dbuvm(scenario.frequencies.f1_hz);

    GroundConstants gc { 0.005, 15.0 };

    for (size_t i = 0; i < n; ++i) {
        if (cancel.load()) return;

        // Build station geometry for this grid point
        std::vector<StationGeometry> stations;
        std::vector<double>          asf_values;

        for (const auto& tx : scenario.transmitters) {
            if (tx.power_w <= 0.0) continue;

            double dist_m = 0.0, az1 = 0.0, az2 = 0.0;
            geod.Inverse(pts[i].lat, pts[i].lon, tx.lat, tx.lon,
                         dist_m, az1, az2);
            double dist_km = std::max(dist_m / 1000.0, 0.1);

            double e_gw = groundwave_field_dbuvm(
                scenario.frequencies.f1_hz, dist_km, gc, tx.power_w);
            double snr = compute_snr_db(e_gw, atm_noise_v, veh_noise);

            StationGeometry sg;
            sg.slot         = tx.slot;
            sg.lat_tx       = tx.lat;
            sg.lon_tx       = tx.lon;
            sg.dist_km      = dist_km;
            sg.azimuth_deg  = az1;
            sg.snr_db       = snr;
            sg.usable       = (snr > 0.0 && dist_km <= scenario.receiver.max_range_km);
            sg.sigma_phi_ml = phase_uncertainty_ml(snr, scenario.frequencies);

            stations.push_back(sg);
            asf_values.push_back(asf_single_ml(scenario.frequencies.f1_hz,
                                                dist_km, gc.sigma));
        }

        // Average ASF across usable stations
        double asf_sum = 0.0;
        int asf_count  = 0;
        std::vector<int> selected;
        for (int k = 0; k < (int)stations.size(); ++k) {
            if (stations[k].usable) { asf_sum += asf_values[k]; ++asf_count; }
        }
        double asf_mean = (asf_count > 0) ? asf_sum / asf_count : 0.0;

        if (it_asf != data.layers.end())
            it_asf->second.values[i] = asf_mean;

        // Absolute accuracy from VL error model
        if (it_abs != data.layers.end()) {
            double whdop_local = compute_whdop(stations,
                                               scenario.receiver.min_stations,
                                               scenario.receiver.max_range_km,
                                               selected);
            if (whdop_local >= 9000.0) {
                it_abs->second.values[i] = 9999.0;
            } else {
                double err = virtual_locator_error_m(
                    asf_values, stations, selected, scenario.frequencies);
                it_abs->second.values[i] = err;
            }
        }
    }

    // Stage 11: Confidence factor (residues from VL fix).
    // confidence(err) = 1 / (1 + (err/50m)^2)
    //   → 1.0 at 0 m absolute error (perfect coverage)
    //   → 0.5 at 50 m error
    //   → 0.0 where no fix is possible (err >= 9000)
    auto it_conf     = data.layers.find("confidence");
    auto it_abs_acc  = data.layers.find("absolute_accuracy");
    if (it_conf != data.layers.end() && it_abs_acc != data.layers.end()) {
        const auto& abs_vals  = it_abs_acc->second.values;
        auto&       conf_vals = it_conf->second.values;
        for (size_t i = 0; i < abs_vals.size(); ++i) {
            double err  = abs_vals[i];
            double conf = (err >= 9000.0) ? 0.0
                         : 1.0 / (1.0 + (err / 50.0) * (err / 50.0));
            conf_vals[i] = conf;
        }
    }
}

// ---------------------------------------------------------------------------
// Per-slot phase at a single point (virtual receiver)
// ---------------------------------------------------------------------------
std::vector<SlotPhaseResult> computeAtPoint(
    double lat_rx, double lon_rx,
    const Scenario& scenario)
{
    const double c     = 299'792'458.0;
    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    GroundConstants gc { 0.005, 15.0 };
    const double atm_n = atm_noise_dbuvm(scenario.frequencies.f1_hz);
    const double veh_n = vehicle_noise_dbuvm(scenario.receiver.vehicle_noise_dbuvpm);

    std::vector<SlotPhaseResult> results;

    for (const auto& tx : scenario.transmitters) {
        double dist_m = 0.0;
        geod.Inverse(lat_rx, lon_rx, tx.lat, tx.lon, dist_m);
        double dist_km = std::max(dist_m / 1000.0, 0.01);

        // Propagation delay (surface wave + ASF)
        double tau_free = dist_m / c;  // free-space delay (s)
        double asf_s    = asf_single_ml(scenario.frequencies.f1_hz,
                                         dist_km, gc.sigma)
                          / (scenario.frequencies.f1_hz * 1000.0);
        double tau_total = tau_free + asf_s;

        // Pseudorange (m)
        double pr_m = tau_total * c;

        // Per-frequency fractional phase and lane
        auto lane_phase = [&](double f_hz) -> std::pair<int,double> {
            double lane_m  = c / f_hz;
            double lanes   = pr_m / lane_m;
            int    lane_n  = (int)std::floor(lanes);
            double frac    = lanes - lane_n;
            return { lane_n, frac };
        };

        auto [l1p, p1p] = lane_phase(scenario.frequencies.f1_hz);
        auto [l1m, p1m] = lane_phase(scenario.frequencies.f1_hz);  // f1- same freq
        auto [l2p, p2p] = lane_phase(scenario.frequencies.f2_hz);
        auto [l2m, p2m] = lane_phase(scenario.frequencies.f2_hz);

        double e_gw = groundwave_field_dbuvm(scenario.frequencies.f1_hz,
                                              dist_km, gc, tx.power_w);
        double e_sky = skywave_field_dbuvm(scenario.frequencies.f1_hz,
                                            dist_km, tx.power_w,
                                            tx.lat, lat_rx);
        double snr  = compute_snr_db(e_gw, atm_n, veh_n);
        double gdr  = compute_gdr_db(e_gw, e_sky, atm_n, veh_n);

        SlotPhaseResult r;
        r.slot          = tx.slot;
        r.pseudorange_m = pr_m;
        r.f1plus_phase  = p1p;  r.f1plus_lane  = l1p;
        r.f1minus_phase = p1m;  r.f1minus_lane = l1m;
        r.f2plus_phase  = p2p;  r.f2plus_lane  = l2p;
        r.f2minus_phase = p2m;  r.f2minus_lane = l2m;
        r.snr_db        = snr;
        r.gdr_db        = gdr;
        results.push_back(r);
    }

    return results;
}

} // namespace bp
