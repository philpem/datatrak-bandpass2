#include "asf.h"
#include "monteath.h"
#include "groundwave.h"
#include "parallel.h"
#include "grwave.h"
#include "skywave.h"
#include "noise.h"
#include "snr.h"
#include "whdop.h"
#include "conductivity.h"
#include "terrain.h"
#include "../coords/Osgb.h"
#include <GeographicLib/Geodesic.hpp>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <map>
#include <string>
#include <limits>

namespace bp {

// ---------------------------------------------------------------------------
// Airy 1830 geodesic — matches Mk4 Locator firmware (P4-04)
//
// The Mk4 Locator firmware computes ranges using the Airy 1830 ellipsoid
// with the Andoyer-Lambert formula.  GeographicLib implements the exact
// geodesic on any ellipsoid; the WGS84 and Airy results differ by < 0.1%
// in length but that systematic offset would bias the VL fix.
//
// Airy 1830: a = 6377563.396 m, 1/f = 299.3249646
// ---------------------------------------------------------------------------
namespace {

const GeographicLib::Geodesic& airy_geod() {
    static const GeographicLib::Geodesic g(6377563.396, 1.0 / 299.3249646);
    return g;
}

// Distance (metres) and azimuth (degrees) on Airy ellipsoid.
// azi_deg is bearing from (lat1,lon1) to (lat2,lon2), 0=N, 90=E.
double airy_inverse(double lat1, double lon1, double lat2, double lon2,
                    double& azi_deg)
{
    double dist_m = 0.0, az1 = 0.0, az2 = 0.0;
    airy_geod().Inverse(lat1, lon1, lat2, lon2, dist_m, az1, az2);
    azi_deg = az1;
    return dist_m;
}

// Distance only (Airy ellipsoid).
double airy_dist_m(double lat1, double lon1, double lat2, double lon2) {
    double az_unused;
    return airy_inverse(lat1, lon1, lat2, lon2, az_unused);
}

// ---------------------------------------------------------------------------
// Iterated 2D Weighted Least-Squares Virtual Locator fix (P4-02)
//
// Williams (2004) Eq. 9.9-9.12.
//
// The receiver "measures" pseudoranges that include the Monteath ASF delay.
// The VL (which has no ASF corrections) minimises free-space range residuals
// to estimate position.  This function returns the absolute position error
// (distance from the VL's fix to the true grid-point position) in metres.
//
// Inputs:
//   lat_p, lon_p  — true position (WGS84, grid point)
//   asf_m[]       — ASF delay in METRES per station at this grid point
//   geom[]        — station geometry (azimuth, dist, SNR) for all stations
//   selected[]    — indices of selected stations
//   freq          — for information only (not used; ranges already in metres)
//
// Algorithm:
//   1. "Measured" pseudoranges ρᵢ = dist_airy(P_true, TXᵢ) + asf_m[i]
//   2. Start VL estimate at P_true
//   3. For up to MAX_ITER iterations:
//      a. For each station i: predicted ρ̂ᵢ = dist_airy(P_est, TXᵢ)
//      b. Residuals: δρᵢ = ρᵢ - ρ̂ᵢ
//      c. Weight wᵢ = SNR_linear_i / ΣSNR
//      d. Design matrix H[i] = [-sin(az_i), -cos(az_i)]  (E, N)
//      e. WLS: ΔEN = (HᵀWH)⁻¹ Hᵀ W δρ
//      f. Update lat/lon from ΔEN; stop if |ΔEN| < CONV_M
//   4. Return dist_airy(P_est_final, P_true)
// ---------------------------------------------------------------------------
constexpr int    VL_MAX_ITER = 8;
constexpr double VL_CONV_M   = 0.1;   // convergence threshold (metres)
constexpr double AIRY_A      = 6377563.396;

double virtual_locator_error_m(
    double lat_p, double lon_p,
    const std::vector<double>& asf_m,   // ASF in metres per station
    const std::vector<StationGeometry>& geom,
    const std::vector<int>& selected,
    const std::vector<double>& true_dist_m_cache)  // pre-computed Airy dist per station (or empty)
{
    if (selected.empty()) return std::numeric_limits<double>::quiet_NaN();
    int N = (int)selected.size();

    // "Measured" pseudoranges: free-space + ASF (metres, Airy ellipsoid)
    // Use pre-computed distances when available to avoid redundant geodesic calls.
    std::vector<double> rho_meas(N);
    for (int k = 0; k < N; ++k) {
        int idx = selected[k];
        double dist_m = (!true_dist_m_cache.empty())
            ? true_dist_m_cache[idx]
            : airy_dist_m(lat_p, lon_p, geom[idx].lat_tx, geom[idx].lon_tx);
        rho_meas[k] = dist_m + asf_m[idx];
    }

    // SNR weights (linear scale)
    double snr_sum = 0.0;
    for (int k = 0; k < N; ++k)
        snr_sum += std::pow(10.0, geom[selected[k]].snr_db / 10.0);
    if (snr_sum <= 0.0) return std::numeric_limits<double>::quiet_NaN();

    // Iterative WLS fix starting from true position
    double lat_est = lat_p;
    double lon_est = lon_p;
    const double DEG2RAD = M_PI / 180.0;

    for (int iter = 0; iter < VL_MAX_ITER; ++iter) {
        // Build design matrix and residual vector
        // H is N×2 (columns: East, North)
        double HTW_H[2][2] = {};   // 2×2 accumulator
        double HTW_d[2]    = {};   // 2-vector accumulator

        for (int k = 0; k < N; ++k) {
            int idx = selected[k];
            double w = std::pow(10.0, geom[idx].snr_db / 10.0) / snr_sum;

            double az_deg = 0.0;
            double dist_pred = airy_inverse(lat_est, lon_est,
                                            geom[idx].lat_tx, geom[idx].lon_tx,
                                            az_deg);
            double delta_rho = rho_meas[k] - dist_pred;

            // Jacobian: ∂dist/∂(East)  = -sin(az), ∂dist/∂(North) = -cos(az)
            double az_rad = az_deg * DEG2RAD;
            double hE = -std::sin(az_rad);
            double hN = -std::cos(az_rad);

            HTW_H[0][0] += w * hE * hE;
            HTW_H[0][1] += w * hE * hN;
            HTW_H[1][0] += w * hN * hE;
            HTW_H[1][1] += w * hN * hN;
            HTW_d[0]    += w * hE * delta_rho;
            HTW_d[1]    += w * hN * delta_rho;
        }

        // Solve 2×2 system by direct inversion
        double det = HTW_H[0][0] * HTW_H[1][1] - HTW_H[0][1] * HTW_H[1][0];
        if (std::abs(det) < 1e-30) return std::numeric_limits<double>::quiet_NaN();
        double inv_det = 1.0 / det;
        double dE = ( HTW_H[1][1] * HTW_d[0] - HTW_H[0][1] * HTW_d[1]) * inv_det;
        double dN = (-HTW_H[1][0] * HTW_d[0] + HTW_H[0][0] * HTW_d[1]) * inv_det;

        // Convert ENU displacement (metres) to lat/lon increment
        // Using Airy semi-major axis as local radius approximation
        // (full meridional/prime vertical radii would be marginally better,
        // but the difference is < 0.3% over the lat range 49–59°)
        double cos_lat = std::cos(lat_est * DEG2RAD);
        if (std::abs(cos_lat) < 1e-10) cos_lat = 1e-10;
        lat_est += (dN / AIRY_A) / DEG2RAD;
        lon_est += (dE / (AIRY_A * cos_lat)) / DEG2RAD;

        // Convergence check
        if (std::sqrt(dE * dE + dN * dN) < VL_CONV_M) break;
    }

    // Absolute accuracy = distance from VL fix to true position
    return airy_dist_m(lat_p, lon_p, lat_est, lon_est);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Grid computation
// ---------------------------------------------------------------------------
void computeASF(GridData& data, const Scenario& scenario,
                const std::atomic<bool>& cancel,
                const std::function<void(int)>& progress_fn)
{
    auto it_asf  = data.layers.find("asf");
    auto it_abs  = data.layers.find("absolute_accuracy");
    auto it_corr = data.layers.find("absolute_accuracy_corrected");
    auto it_delt = data.layers.find("absolute_accuracy_delta");
    auto it_grad = data.layers.find("asf_gradient");
    auto it_gw   = data.layers.find("groundwave");
    if (it_gw == data.layers.end()) return;

    const auto& pts = it_gw->second.points;
    size_t n = pts.size();
    if (n == 0) return;

    // Build conductivity and terrain maps from scenario (P4-01 / conductivity fix)
    auto cond_map    = make_cached_conductivity_map(scenario);
    auto terrain_map = make_terrain_map(scenario);

    // Build pattern-offset lookup: "slave_slot,master_slot" → PatternOffset*
    // Used to apply po corrections in the corrected-accuracy VL fix (P5-14).
    // An entry is present only for slave stations (non-masters with master_slot > 0).
    std::map<std::string, const PatternOffset*> po_lookup;
    for (const auto& po : scenario.pattern_offsets)
        po_lookup[po.pattern] = &po;

    // Receiver firmware velocity — used for ml↔m conversion in the VL fix.
    // The VL mimics the receiver, which converts phase to range using vp_ms.
    const double vp         = scenario.receiver.vp_ms;
    const double lane_m_f1  = vp / scenario.frequencies.f1_hz;
    const double veh_noise  = vehicle_noise_dbuvm(scenario.receiver.vehicle_noise_dbuvpm);
    const double atm_noise_v = atm_noise_dbuvm(scenario.frequencies.f1_hz);

    // ASF values grid: store for gradient computation after main loop
    std::vector<double> asf_grid(n, 0.0);

    const auto flat_txs = scenario.flatTransmitters();
    int last_pct = -1;

    // -----------------------------------------------------------------------
    // Groundwave cache: reuse per-transmitter field strength layers already
    // computed by computeGroundwave() in Stage 1 (stored as "groundwave_<slot>").
    // This avoids recomputing the full Millington/GRWAVE model for every
    // TX-RX pair, which was the dominant cost in this stage.
    // -----------------------------------------------------------------------
    struct GwCacheEntry {
        const std::vector<double>* values = nullptr;  // pointer into GridData layer
        int slot = 0;
    };
    std::vector<GwCacheEntry> gw_cache;
    gw_cache.reserve(flat_txs.size());
    for (const auto& tx : flat_txs) {
        GwCacheEntry entry;
        entry.slot = tx.slot;
        std::string key = "groundwave_" + std::to_string(tx.slot);
        auto it = data.layers.find(key);
        if (it != data.layers.end() && it->second.values.size() == n) {
            entry.values = &it->second.values;
        }
        gw_cache.push_back(entry);
    }

    // -----------------------------------------------------------------------
    // Pre-compute Airy distance and azimuth for all TX-RX pairs.
    // These are needed both for building StationGeometry and inside the VL
    // fix iteration — caching avoids redundant GeographicLib Inverse() calls.
    // Layout: airy_dist[ti * n + i], airy_az[ti * n + i].
    //
    // When precompute_airy_cache is false, skip the bulk allocation (~16 bytes
    // per TX-RX pair) and compute on-the-fly in the per-point loop instead.
    // This trades speed for lower memory usage on large grids.
    // -----------------------------------------------------------------------
    size_t ntx = flat_txs.size();
    const bool use_airy_cache = scenario.precompute_airy_cache;
    std::vector<double> airy_dist_cache;
    std::vector<double> airy_az_cache;

    if (use_airy_cache) {
        airy_dist_cache.resize(ntx * n);
        airy_az_cache.resize(ntx * n);

        for (size_t ti = 0; ti < ntx; ++ti) {
            if (cancel.load()) return;
            const auto& tx = flat_txs[ti];
            if (tx.power_w <= 0.0) {
                // Fill with sentinel values; will be skipped by power_w check below
                for (size_t i = 0; i < n; ++i) {
                    airy_dist_cache[ti * n + i] = 0.0;
                    airy_az_cache[ti * n + i] = 0.0;
                }
                continue;
            }
            for (size_t i = 0; i < n; ++i) {
                double az = 0.0;
                double d = airy_inverse(pts[i].lat, pts[i].lon,
                                        tx.lat, tx.lon, az);
                airy_dist_cache[ti * n + i] = d;
                airy_az_cache[ti * n + i] = az;
            }
        }
    }

    if (cancel.load()) return;

    // Get output layer value vectors as raw pointers for thread-safe indexed writes.
    double* asf_vals  = (it_asf  != data.layers.end()) ? it_asf->second.values.data()  : nullptr;
    double* abs_vals  = (it_abs  != data.layers.end()) ? it_abs->second.values.data()  : nullptr;
    double* corr_vals = (it_corr != data.layers.end()) ? it_corr->second.values.data() : nullptr;
    double* delt_vals = (it_delt != data.layers.end()) ? it_delt->second.values.data() : nullptr;
    std::atomic<size_t> asf_done{0};
    parallel_for(n, cancel, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            if (cancel.load()) return;

            std::vector<StationGeometry> stations;
            std::vector<double>          asf_m_vals;
            std::vector<size_t>          tx_idx_map;
            std::vector<double>          raw_dist_m;

            for (size_t ti = 0; ti < flat_txs.size(); ++ti) {
                const auto& tx = flat_txs[ti];
                if (tx.power_w <= 0.0) continue;

                double az_deg, dist_m;
                if (use_airy_cache) {
                    az_deg = airy_az_cache[ti * n + i];
                    dist_m = airy_dist_cache[ti * n + i];
                } else {
                    dist_m = airy_inverse(pts[i].lat, pts[i].lon,
                                          tx.lat, tx.lon, az_deg);
                }
                double dist_km = std::max(dist_m / 1000.0, 0.1);

                double e_gw;
                if (gw_cache[ti].values) {
                    e_gw = (*gw_cache[ti].values)[i];
                } else {
                    e_gw = groundwave_for_model(
                        scenario.frequencies.f1_hz,
                        tx.lat, tx.lon, pts[i].lat, pts[i].lon,
                        *cond_map, tx.power_w,
                        scenario.propagation_model, 20);
                }
                double snr = compute_snr_db(e_gw, atm_noise_v, veh_noise);

                StationGeometry sg;
                sg.slot         = tx.slot;
                sg.lat_tx       = tx.lat;
                sg.lon_tx       = tx.lon;
                sg.dist_km      = dist_km;
                sg.azimuth_deg  = az_deg;
                sg.snr_db       = snr;
                sg.usable       = (snr > 0.0 && dist_km <= scenario.receiver.max_range_km);
                sg.sigma_phi_ml = phase_uncertainty_ml(snr, scenario.frequencies);

                stations.push_back(sg);
                tx_idx_map.push_back(ti);
                if (!use_airy_cache)
                    raw_dist_m.push_back(dist_m);

                double asf_ml = monteath_asf_ml(
                    scenario.frequencies.f1_hz,
                    tx.lat, tx.lon,
                    pts[i].lat, pts[i].lon,
                    *terrain_map, *cond_map,
                    20);
                asf_m_vals.push_back(asf_ml * lane_m_f1 / 1000.0);
            }

            // Mean ASF (millilanes)
            double asf_sum = 0.0;
            int asf_count  = 0;
            for (size_t k = 0; k < stations.size(); ++k) {
                if (stations[k].usable) {
                    asf_sum += asf_m_vals[k] * 1000.0 / lane_m_f1;
                    ++asf_count;
                }
            }
            double asf_mean_ml = (asf_count > 0) ? asf_sum / asf_count : 0.0;

            if (asf_vals)
                asf_vals[i] = asf_mean_ml;
            asf_grid[i] = asf_mean_ml;

            // Build per-station Airy distance vector for VL fix
            std::vector<double> station_dist_vec(stations.size());
            if (use_airy_cache) {
                for (size_t k = 0; k < stations.size(); ++k)
                    station_dist_vec[k] = airy_dist_cache[tx_idx_map[k] * n + i];
            } else {
                for (size_t k = 0; k < stations.size(); ++k)
                    station_dist_vec[k] = raw_dist_m[k];
            }

            // Absolute accuracy from iterated WLS VL (P4-02)
            std::vector<int> selected;
            {
                double whdop_local = compute_whdop(stations,
                                                   scenario.receiver.min_stations,
                                                   scenario.receiver.max_range_km,
                                                   selected);
                double err = std::isnan(whdop_local)
                           ? std::numeric_limits<double>::quiet_NaN()
                           : virtual_locator_error_m(pts[i].lat, pts[i].lon,
                                                     asf_m_vals, stations, selected,
                                                     station_dist_vec);
                if (abs_vals)
                    abs_vals[i] = err;
            }

            // Corrected absolute accuracy (P5-14)
            if ((corr_vals || delt_vals) && !selected.empty()) {
                std::vector<double> corrected_asf(asf_m_vals);
                for (size_t k = 0; k < stations.size(); ++k) {
                    const auto& tx = flat_txs[tx_idx_map[k]];
                    if (!tx.is_master && tx.master_slot > 0) {
                        std::string pat = std::to_string(tx.slot) + ","
                                        + std::to_string(tx.master_slot);
                        auto pit = po_lookup.find(pat);
                        if (pit != po_lookup.end()) {
                            double corr_m = pit->second->f1plus_ml * lane_m_f1 / 1000.0;
                            corrected_asf[k] -= corr_m;
                        }
                    }
                }
                double corr_err = virtual_locator_error_m(pts[i].lat, pts[i].lon,
                                                           corrected_asf, stations, selected,
                                                           station_dist_vec);
                if (corr_vals)
                    corr_vals[i] = corr_err;

                if (delt_vals && abs_vals) {
                    double uncorr = abs_vals[i];
                    delt_vals[i] = (std::isnan(uncorr) || std::isnan(corr_err))
                                   ? std::numeric_limits<double>::quiet_NaN()
                                   : uncorr - corr_err;
                }
            }

            asf_done.fetch_add(1, std::memory_order_relaxed);
        }
    });

    if (cancel.load()) return;

    // Report final progress
    if (progress_fn)
        progress_fn(100);

    if (cancel.load()) return;

    // ---------------------------------------------------------------------------
    // ASF gradient layer — spatial rate of change of ASF [ml/km]
    // Used as a monitor siting planning aid (P5-11): high gradient zones are
    // most sensitive to propagation drift, so a monitor there detects changes
    // earliest.  We use a central-difference approximation on the 2D grid.
    // ---------------------------------------------------------------------------
    if (it_grad != data.layers.end() && it_asf != data.layers.end() && n >= 4) {
        auto& grad_vals = it_grad->second.values;
        const double res_km = scenario.grid.resolution_km;
        if (res_km > 0.0) {
            // Infer ncols from the point set directly.
            // buildGrid() builds in row-major order: outer loop = lat, inner = lon.
            // The first row consists of all consecutive points sharing pts[0].lat.
            // Use a small tolerance (half the lon step) to count the first row.
            int ncols = 1;
            if (n >= 2) {
                double lat0 = pts[0].lat;
                double tol  = std::abs(pts[1].lon - pts[0].lon) * 0.5;
                for (size_t k = 1; k < n; ++k) {
                    if (std::abs(pts[k].lat - lat0) <= tol)
                        ncols = (int)k + 1;
                    else
                        break;
                }
            }
            int nrows = (ncols > 0) ? (int)(n / (size_t)ncols) : 0;

            if (ncols > 1 && nrows > 1 && (size_t)(ncols * nrows) == n) {
                for (int row = 0; row < nrows; ++row) {
                    if (cancel.load()) return;
                    for (int col = 0; col < ncols; ++col) {
                        size_t idx = (size_t)(row * ncols + col);

                        // Central differences where available, one-sided at edges
                        double dA_drow = 0.0, dA_dcol = 0.0;

                        if (row > 0 && row < nrows - 1) {
                            dA_drow = (asf_grid[(size_t)((row+1)*ncols) + col]
                                     - asf_grid[(size_t)((row-1)*ncols) + col])
                                     / (2.0 * res_km);
                        } else if (row == 0 && nrows > 1) {
                            dA_drow = (asf_grid[(size_t)ncols + col]
                                     - asf_grid[col]) / res_km;
                        } else if (row == nrows - 1 && nrows > 1) {
                            dA_drow = (asf_grid[(size_t)((nrows-1)*ncols) + col]
                                     - asf_grid[(size_t)((nrows-2)*ncols) + col]) / res_km;
                        }

                        if (col > 0 && col < ncols - 1) {
                            dA_dcol = (asf_grid[(size_t)(row*ncols) + col + 1]
                                     - asf_grid[(size_t)(row*ncols) + col - 1])
                                     / (2.0 * res_km);
                        } else if (col == 0 && ncols > 1) {
                            dA_dcol = (asf_grid[(size_t)(row*ncols) + 1]
                                     - asf_grid[(size_t)(row*ncols)]) / res_km;
                        } else if (col == ncols - 1 && ncols > 1) {
                            dA_dcol = (asf_grid[(size_t)(row*ncols) + ncols - 1]
                                     - asf_grid[(size_t)(row*ncols) + ncols - 2]) / res_km;
                        }

                        grad_vals[idx] = std::sqrt(dA_drow * dA_drow
                                                 + dA_dcol * dA_dcol);
                    }
                }
            }
            // If grid dimensions don't match (non-uniform grid etc), leave as zeros
        }
    }

    if (cancel.load()) return;

    // ---------------------------------------------------------------------------
    // Stage 11: Confidence factor
    // confidence(err) = 1 / (1 + (err/50m)^2)
    //   → 1.0 at 0 m absolute error
    //   → 0.5 at 50 m error
    //   → NaN where no fix is possible (absolute_accuracy is NaN)
    // ---------------------------------------------------------------------------
    auto it_conf    = data.layers.find("confidence");
    auto it_abs_acc = data.layers.find("absolute_accuracy");
    if (it_conf != data.layers.end() && it_abs_acc != data.layers.end()) {
        const auto& abs_vals  = it_abs_acc->second.values;
        auto&       conf_vals = it_conf->second.values;
        for (size_t i = 0; i < abs_vals.size(); ++i) {
            double err = abs_vals[i];
            conf_vals[i] = std::isnan(err)
                         ? std::numeric_limits<double>::quiet_NaN()
                         : 1.0 / (1.0 + (err / 50.0) * (err / 50.0));
        }
    }
}

// ---------------------------------------------------------------------------
// Per-slot phase at a single point (virtual receiver)
//
// Williams Eq. 11.1: propagation delay includes free-space + Monteath ASF.
//
// Polarity fix (P3-07 / CLAUDE.md §Current implementation state):
//   F+ and F− navslots in a Datatrak transmission have opposite polarity.
//   The F− phase offset is (1.0 − frac_phase_F+) mod 1.0.
//   Previously f1− was incorrectly set equal to f1+.
//
// SPO and station delay (tx.spo_us, tx.station_delay_us) are applied here:
//   total_delay = tau_prop + spo_us*1e-6 + station_delay_us*1e-6
// ---------------------------------------------------------------------------
std::vector<SlotPhaseResult> computeAtPoint(
    double lat_rx, double lon_rx,
    const Scenario& scenario)
{
    const double c     = 299'792'458.0;       // vacuum — for propagation delay physics
    const double vp    = scenario.receiver.vp_ms;  // receiver firmware velocity
    const double atm_n = atm_noise_dbuvm(scenario.frequencies.f1_hz);
    const double veh_n = vehicle_noise_dbuvm(scenario.receiver.vehicle_noise_dbuvpm);

    // Build conductivity and terrain maps from scenario
    auto cond_map    = make_cached_conductivity_map(scenario);
    auto terrain_map = make_terrain_map(scenario);

    // Build GRWAVE LUTs for fast lookup if using the GRWAVE propagation model.
    // computeAtPoint() is called independently (not from the pipeline), so
    // it needs its own LUTs.
    std::unique_ptr<GrwaveLUT> pt_lut_f1, pt_lut_f2;
    std::unique_ptr<GrwaveLUT::Scope> pt_scope_f1, pt_scope_f2;
    if (scenario.propagation_model == Scenario::PropagationModel::GRWAVE) {
        pt_lut_f1 = std::make_unique<GrwaveLUT>(scenario.frequencies.f1_hz);
        pt_scope_f1 = std::make_unique<GrwaveLUT::Scope>(*pt_lut_f1);
        if (scenario.frequencies.f1_hz != scenario.frequencies.f2_hz) {
            pt_lut_f2 = std::make_unique<GrwaveLUT>(scenario.frequencies.f2_hz);
            pt_scope_f2 = std::make_unique<GrwaveLUT::Scope>(*pt_lut_f2);
        }
    }

    std::vector<SlotPhaseResult> results;

    for (const auto& tx : scenario.flatTransmitters()) {
        // Distance and azimuth (Airy ellipsoid, matching firmware)
        double az_unused = 0.0;
        double dist_m = airy_inverse(lat_rx, lon_rx, tx.lat, tx.lon, az_unused);
        dist_m = std::max(dist_m, 10.0);  // avoid division by zero
        double dist_km = dist_m / 1000.0;

        // Propagation delay components:
        //   free-space delay + Monteath ASF delay + SPO + station delay
        double tau_free = dist_m / c;

        double asf_ml_f1 = monteath_asf_ml(
            scenario.frequencies.f1_hz,
            tx.lat, tx.lon, lat_rx, lon_rx,
            *terrain_map, *cond_map,
            50);  // 50 samples for single-point accuracy
        double asf_ml_f2 = monteath_asf_ml(
            scenario.frequencies.f2_hz,
            tx.lat, tx.lon, lat_rx, lon_rx,
            *terrain_map, *cond_map,
            50);

        // Convert ASF from millilanes to seconds
        double lane_m_f1 = c / scenario.frequencies.f1_hz;
        double lane_m_f2 = c / scenario.frequencies.f2_hz;
        double asf_s_f1  = (asf_ml_f1 / 1000.0) * lane_m_f1 / c;
        double asf_s_f2  = (asf_ml_f2 / 1000.0) * lane_m_f2 / c;

        // SPO and station delay (µs → seconds)
        double spo_s    = tx.spo_us           * 1e-6;
        double delay_s  = tx.station_delay_us * 1e-6;

        double tau_f1 = tau_free + asf_s_f1 + spo_s + delay_s;
        double tau_f2 = tau_free + asf_s_f2 + spo_s + delay_s;

        // Pseudorange (m) — as the receiver firmware would compute it:
        // total_phase × (vp_ms / freq) = total_delay × vp_ms
        double pr_m = (tau_free + asf_s_f1) * vp;

        // Fractional phase and lane number
        // lane_phase(tau, freq): returns {lane_number, frac_phase [0,1)}
        auto lane_phase = [](double tau_s, double freq_hz, double c_val)
            -> std::pair<int, double>
        {
            double lane_m   = c_val / freq_hz;
            double total_m  = tau_s * c_val;
            double lanes    = total_m / lane_m;
            int    lane_n   = (int)std::floor(lanes);
            double frac     = lanes - lane_n;
            if (frac < 0.0) { frac += 1.0; --lane_n; }
            return { lane_n, frac };
        };

        auto [l1p, p1p] = lane_phase(tau_f1, scenario.frequencies.f1_hz, c);
        auto [l2p, p2p] = lane_phase(tau_f2, scenario.frequencies.f2_hz, c);

        // F− polarity: Datatrak F− navslots carry the same carrier with
        // bit-inverted spreading code.  The receiver correlating against F−
        // measures the phase complement of F+:
        //
        //   phase_F− = (1.0 − phase_F+) mod 1.0
        //
        // The lane number (integer count of complete wavelengths TX→RX) is
        // the same physical quantity for both F+ and F−; only the fractional
        // phase reference differs.
        double p1m = 1.0 - p1p;
        if (p1m >= 1.0) p1m -= 1.0;
        int    l1m = l1p;   // same physical distance → same lane count

        double p2m = 1.0 - p2p;
        if (p2m >= 1.0) p2m -= 1.0;
        int    l2m = l2p;   // same physical distance → same lane count

        // SNR and GDR using selected propagation model.
        double e_gw  = groundwave_for_model(scenario.frequencies.f1_hz,
                                             tx.lat, tx.lon, lat_rx, lon_rx,
                                             *cond_map, tx.power_w,
                                             scenario.propagation_model, 50);
        double e_sky = skywave_field_dbuvm(scenario.frequencies.f1_hz,
                                            dist_km, tx.power_w,
                                            tx.lat, lat_rx);
        double snr   = compute_snr_db(e_gw, atm_n, veh_n);
        double gdr   = compute_gdr_db(e_gw, e_sky, atm_n, veh_n);

        SlotPhaseResult r;
        r.slot           = tx.slot;
        r.pseudorange_m  = pr_m;
        r.f1plus_phase   = p1p;  r.f1plus_lane   = l1p;
        r.f1minus_phase  = p1m;  r.f1minus_lane  = l1m;
        r.f2plus_phase   = p2p;  r.f2plus_lane   = l2p;
        r.f2minus_phase  = p2m;  r.f2minus_lane  = l2m;
        r.snr_db         = snr;
        r.gdr_db         = gdr;
        r.distance_m     = dist_m;
        results.push_back(r);
    }

    return results;
}

} // namespace bp
