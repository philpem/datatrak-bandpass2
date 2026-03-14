#include "whdop.h"
#include "groundwave.h"
#include "conductivity.h"
#include "noise.h"
#include <GeographicLib/Geodesic.hpp>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace bp {

// ---------------------------------------------------------------------------
// Directional cosine matrix and WHDOP
//
// Williams (2004) Eq. 9.12-9.16 (after Appendix K station selection):
//
//   For N selected stations, build the design matrix A (2×N):
//     A[:,i] = [cos(theta_i), sin(theta_i)]^T
//   where theta_i is the azimuth from the receiver to station i.
//
//   The SNR-based weight: w_i = SNR_i / sum(SNR)  (normalised)
//   Weight matrix W = diag(w_i)
//
//   WHDOP = sqrt( trace( (A W A^T)^{-1} ) )
//
//   Repeatable accuracy: sigma_r = sigma_phi_ml × WHDOP  (millilanes)
// ---------------------------------------------------------------------------

namespace {

// Solve a 2×2 symmetric system via direct inversion
bool invert2x2(double a00, double a01, double a11,
               double& i00, double& i01, double& i11) {
    double det = a00 * a11 - a01 * a01;
    if (std::abs(det) < 1e-30) return false;
    double inv = 1.0 / det;
    i00 =  a11 * inv;
    i01 = -a01 * inv;
    i11 =  a00 * inv;
    return true;
}

} // anonymous

double compute_whdop(const std::vector<StationGeometry>& all_stations,
                     int min_stations,
                     double max_range_km,
                     std::vector<int>& selected_out)
{
    selected_out.clear();

    // Filter: usable + within range + SNR > 0
    std::vector<int> candidates;
    for (int i = 0; i < (int)all_stations.size(); ++i) {
        const auto& s = all_stations[i];
        if (!s.usable) continue;
        if (s.dist_km > max_range_km) continue;
        if (s.snr_db < 0.0) continue;
        candidates.push_back(i);
    }

    if ((int)candidates.size() < min_stations) return 9999.0;

    // Sort by SNR descending; pick top-8 (Appendix K rule: at most 8 slots)
    std::sort(candidates.begin(), candidates.end(), [&](int a, int b){
        return all_stations[a].snr_db > all_stations[b].snr_db;
    });
    if ((int)candidates.size() > 8) candidates.resize(8);

    // Build weight matrix elements
    double snr_sum = 0.0;
    for (int idx : candidates) {
        snr_sum += std::pow(10.0, all_stations[idx].snr_db / 10.0);
    }
    if (snr_sum <= 0.0) return 9999.0;

    // AWAWT = A × W × A^T (2×2 symmetric)
    double awat00 = 0.0, awat01 = 0.0, awat11 = 0.0;
    for (int idx : candidates) {
        const auto& s = all_stations[idx];
        double snr_lin = std::pow(10.0, s.snr_db / 10.0);
        double w = snr_lin / snr_sum;
        double az_rad = s.azimuth_deg * M_PI / 180.0;
        double ca = std::cos(az_rad);
        double sa = std::sin(az_rad);
        awat00 += w * ca * ca;
        awat01 += w * ca * sa;
        awat11 += w * sa * sa;
    }

    double i00, i01, i11;
    if (!invert2x2(awat00, awat01, awat11, i00, i01, i11)) return 9999.0;

    double whdop = std::sqrt(i00 + i11);  // sqrt(trace of inverse)
    selected_out = candidates;
    return std::max(whdop, 0.1);
}

// ---------------------------------------------------------------------------
void computeWHDOP(GridData& data, const Scenario& scenario,
                  const std::atomic<bool>& cancel)
{
    auto it_whdop = data.layers.find("whdop");
    auto it_rep   = data.layers.find("repeatable");
    auto it_gw    = data.layers.find("groundwave");

    if (it_gw == data.layers.end()) return;

    const auto& pts = it_gw->second.points;
    size_t n = pts.size();
    if (n == 0) return;

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    const double veh_noise = vehicle_noise_dbuvm(scenario.receiver.vehicle_noise_dbuvpm);
    const double atm_noise_val = atm_noise_dbuvm(scenario.frequencies.f1_hz);

    // Precompute per-TX per-grid-point field strength and SNR
    struct TxCache {
        std::vector<double> gw;   // groundwave dBuV/m
        std::vector<double> snr;  // SNR dB
        double lat_tx, lon_tx;
        double power_w;
        int    slot;
        bool   ok;
    };

    auto cond_map = make_conductivity_map(scenario);

    std::vector<TxCache> tx_cache;
    const auto flat_txs = scenario.flatTransmitters();
    tx_cache.reserve(flat_txs.size());

    for (const auto& tx : flat_txs) {
        TxCache c;
        c.lat_tx = tx.lat; c.lon_tx = tx.lon;
        c.power_w = tx.power_w; c.slot = tx.slot;
        c.ok = (tx.power_w > 0.0);
        c.gw.resize(n, -200.0);
        c.snr.resize(n, -200.0);

        if (!c.ok) { tx_cache.push_back(std::move(c)); continue; }
        if (cancel.load()) return;

        for (size_t i = 0; i < n; ++i) {
            double dist_m = 0.0;
            geod.Inverse(tx.lat, tx.lon, pts[i].lat, pts[i].lon, dist_m);
            double dist_km = std::max(dist_m / 1000.0, 0.1);
            GroundConstants gc = cond_map->lookup(
                0.5 * (tx.lat + pts[i].lat),
                0.5 * (tx.lon + pts[i].lon));
            double e_gw = groundwave_field_dbuvm(
                scenario.frequencies.f1_hz, dist_km, gc, tx.power_w);
            c.gw[i]  = e_gw;
            c.snr[i] = compute_snr_db(e_gw, atm_noise_val, veh_noise);
        }
        tx_cache.push_back(std::move(c));
    }

    if (cancel.load()) return;

    // Per grid point: build station geometry, compute WHDOP and repeatable
    for (size_t i = 0; i < n; ++i) {
        if (cancel.load()) return;

        std::vector<StationGeometry> stations;
        stations.reserve(tx_cache.size());

        for (size_t ti = 0; ti < tx_cache.size(); ++ti) {
            const auto& tc = tx_cache[ti];
            if (!tc.ok) continue;

            StationGeometry sg;
            sg.slot   = tc.slot;
            sg.lat_tx = tc.lat_tx;
            sg.lon_tx = tc.lon_tx;
            sg.snr_db = tc.snr[i];
            sg.usable = (sg.snr_db > 0.0);

            double dist_m = 0.0, az1 = 0.0, az2 = 0.0;
            geod.Inverse(pts[i].lat, pts[i].lon, tc.lat_tx, tc.lon_tx,
                         dist_m, az1, az2);
            sg.dist_km     = dist_m / 1000.0;
            sg.azimuth_deg = az1;
            sg.sigma_phi_ml = phase_uncertainty_ml(sg.snr_db, scenario.frequencies);
            stations.push_back(sg);
        }

        std::vector<int> selected;
        double whdop = compute_whdop(stations,
                                      scenario.receiver.min_stations,
                                      scenario.receiver.max_range_km,
                                      selected);

        if (it_whdop != data.layers.end())
            it_whdop->second.values[i] = whdop;

        // Repeatable accuracy: sigma_r = mean(sigma_phi) * WHDOP  [ml]
        if (it_rep != data.layers.end()) {
            if (selected.empty() || whdop >= 9000.0) {
                it_rep->second.values[i] = 9999.0;
            } else {
                double sigma_sum = 0.0;
                for (int idx : selected) sigma_sum += stations[idx].sigma_phi_ml;
                double sigma_mean = sigma_sum / selected.size();
                it_rep->second.values[i] = sigma_mean * whdop;
            }
        }
    }
}

} // namespace bp
