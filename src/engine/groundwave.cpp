#include "groundwave.h"
#include "grwave.h"
#include "conductivity.h"
#include <GeographicLib/Geodesic.hpp>
#include <GeographicLib/GeodesicLine.hpp>
#include <cmath>
#include <algorithm>
#include <vector>

namespace bp {

// ---------------------------------------------------------------------------
// ITU-R P.368-9 groundwave field strength
//
// Uses an empirical attenuation model calibrated to ITU-R P.368 LF curves.
// The full Sommerfeld/Wait/GRWAVE algorithm (spherical Earth residue series)
// can replace this in a later phase.
//
// Attenuation formula fitted to P.368 data for 30-300 kHz:
//   A_db = 0.0438 * d_km^0.832 * (f/100kHz)^0.5 * (0.005/sigma)^0.3
//
// Free-space reference (ITU-R P.368, short monopole over perfect ground):
//   E0 = 300 mV/m at d=1 km for P=1 kW  →  E0 [µV/m] = 300e3 * sqrt(P_kW) / d_km
//
// NOTE: this function uses a single conductivity value for the whole path.
// For mixed land/sea paths use millington_field_dbuvm() instead.
// Terrain height profile is not used here; Monteath terrain phase delay
// is applied separately in asf.cpp::monteath_asf_ml().
// ---------------------------------------------------------------------------
double groundwave_field_dbuvm(double freq_hz,
                               double dist_km,
                               const GroundConstants& gc,
                               double power_w)
{
    if (dist_km <= 0.0 || power_w <= 0.0) return -200.0;

    // Free-space field for short monopole over perfect ground.
    // ITU-R P.368 defines E0 = 300 mV/m at d=1 km for P=1 kW (total radiated).
    // Scaling: E0 [µV/m] = 300e3 * sqrt(P_kW) / d_km  =  9487.1 * sqrt(P_W) / d_km
    double P_kW     = power_w / 1000.0;
    double E0_uvm   = 300.0e3 * std::sqrt(P_kW) / dist_km;
    double E0_dbuvm = 20.0 * std::log10(std::max(E0_uvm, 1e-20));

    // Empirical surface-wave attenuation
    double sigma_eff = std::max(gc.sigma, 1e-6);
    double A_db = 0.0438
                  * std::pow(dist_km, 0.832)
                  * std::pow(freq_hz / 100e3, 0.5)
                  * std::pow(0.005 / sigma_eff, 0.3);

    return E0_dbuvm - A_db;
}

// ---------------------------------------------------------------------------
// Millington (1949) mixed-path groundwave field strength
//
// The Millington method corrects the groundwave field strength for mixed
// conductivity paths (e.g. land + sea) using a forward–backward average.
//
// For N segments (conductivities σ₀…σ_{N-1}, cumulative distances D₁…D_N):
//
//   Forward:  E_fwd = E_hom(D_N, σ_{N-1})
//                   + Σ_{k=0}^{N-2} [E_hom(D_{k+1}, σ_k) − E_hom(D_{k+1}, σ_{k+1})]
//
//   Backward: E_back = E_hom(D_N, σ_0)
//                    + Σ_{k=0}^{N-2} [E_hom(D_N−D_{N-2-k}, σ_{N-1-k})
//                                   − E_hom(D_N−D_{N-2-k}, σ_{N-2-k})]
//
//   Result: 20 log10(½(lin(E_fwd) + lin(E_back)))
//
// For a homogeneous path all correction terms cancel, giving E_hom(D_N, σ)
// from both passes — identical to groundwave_field_dbuvm().
// ---------------------------------------------------------------------------
double millington_field_dbuvm(double freq_hz,
                               double lat_tx, double lon_tx,
                               double lat_rx, double lon_rx,
                               const ConductivityMap& cond,
                               double power_w,
                               int nsamples)
{
    if (nsamples < 1) nsamples = 1;
    if (power_w <= 0.0) return -200.0;

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();

    double total_dist_m = 0.0;
    geod.Inverse(lat_tx, lon_tx, lat_rx, lon_rx, total_dist_m);
    double total_dist_km = total_dist_m / 1000.0;
    if (total_dist_km < 0.1) return -200.0;

    // Sample nsamples segments along the great circle.
    // Segment k spans from sample point k to k+1.
    // Conductivity is looked up at each segment midpoint.
    struct Seg {
        double d_end_km;    // cumulative distance from TX at segment end
        GroundConstants gc; // conductivity at segment midpoint
    };

    std::vector<Seg> segs;
    segs.reserve(nsamples);

    GeographicLib::GeodesicLine line = geod.InverseLine(lat_tx, lon_tx, lat_rx, lon_rx);

    double prev_lat = lat_tx, prev_lon = lon_tx;
    for (int i = 1; i <= nsamples; ++i) {
        double s   = (double(i) / nsamples) * total_dist_m;
        double lat = 0.0, lon = 0.0;
        line.Position(s, lat, lon);

        double mid_lat = 0.5 * (prev_lat + lat);
        double mid_lon = 0.5 * (prev_lon + lon);

        segs.push_back({ s / 1000.0, cond.lookup(mid_lat, mid_lon) });

        prev_lat = lat;
        prev_lon = lon;
    }

    int N = (int)segs.size(); // == nsamples

    // --- Forward pass ---
    // Start: homogeneous field for total distance at last segment's conductivity.
    double E_fwd = groundwave_field_dbuvm(freq_hz, total_dist_km, segs[N-1].gc, power_w);
    // Add correction at each interior boundary (between segment k and k+1).
    for (int k = 0; k < N - 1; ++k) {
        double D = segs[k].d_end_km;
        E_fwd += groundwave_field_dbuvm(freq_hz, D, segs[k  ].gc, power_w)
               - groundwave_field_dbuvm(freq_hz, D, segs[k+1].gc, power_w);
    }

    // --- Backward pass ---
    // Start: homogeneous field for total distance at first segment's conductivity.
    double E_back = groundwave_field_dbuvm(freq_hz, total_dist_km, segs[0].gc, power_w);
    // Add correction at each interior boundary traversed in reverse.
    // For k=0..N-2: boundary distance from RX = total − segs[N-2-k].d_end_km.
    // Reversed segment k has conductivity segs[N-1-k].gc;
    // reversed segment k+1 has conductivity segs[N-2-k].gc.
    for (int k = 0; k < N - 1; ++k) {
        double D_back = total_dist_km - segs[N-2-k].d_end_km;
        E_back += groundwave_field_dbuvm(freq_hz, D_back, segs[N-1-k].gc, power_w)
                - groundwave_field_dbuvm(freq_hz, D_back, segs[N-2-k].gc, power_w);
    }

    // Linear average of forward and backward estimates.
    double lin_fwd  = std::pow(10.0, E_fwd  / 20.0);
    double lin_back = std::pow(10.0, E_back / 20.0);
    return 20.0 * std::log10(0.5 * (lin_fwd + lin_back));
}

// ---------------------------------------------------------------------------
// Millington with pluggable per-segment function.
// ---------------------------------------------------------------------------
double millington_with(double freq_hz,
                       double lat_tx, double lon_tx,
                       double lat_rx, double lon_rx,
                       const ConductivityMap& cond,
                       double power_w,
                       SegmentFieldFn seg_fn,
                       int nsamples)
{
    if (nsamples < 1) nsamples = 1;
    if (power_w <= 0.0) return -200.0;

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();

    double total_dist_m = 0.0;
    geod.Inverse(lat_tx, lon_tx, lat_rx, lon_rx, total_dist_m);
    double total_dist_km = total_dist_m / 1000.0;
    if (total_dist_km < 0.1) return -200.0;

    struct Seg {
        double d_end_km;
        GroundConstants gc;
    };

    std::vector<Seg> segs;
    segs.reserve(nsamples);

    GeographicLib::GeodesicLine line = geod.InverseLine(lat_tx, lon_tx, lat_rx, lon_rx);

    double prev_lat = lat_tx, prev_lon = lon_tx;
    for (int i = 1; i <= nsamples; ++i) {
        double s   = (double(i) / nsamples) * total_dist_m;
        double lat = 0.0, lon = 0.0;
        line.Position(s, lat, lon);

        double mid_lat = 0.5 * (prev_lat + lat);
        double mid_lon = 0.5 * (prev_lon + lon);

        segs.push_back({ s / 1000.0, cond.lookup(mid_lat, mid_lon) });

        prev_lat = lat;
        prev_lon = lon;
    }

    int N = (int)segs.size();

    double E_fwd = seg_fn(freq_hz, total_dist_km, segs[N-1].gc, power_w);
    for (int k = 0; k < N - 1; ++k) {
        double D = segs[k].d_end_km;
        E_fwd += seg_fn(freq_hz, D, segs[k  ].gc, power_w)
               - seg_fn(freq_hz, D, segs[k+1].gc, power_w);
    }

    double E_back = seg_fn(freq_hz, total_dist_km, segs[0].gc, power_w);
    for (int k = 0; k < N - 1; ++k) {
        double D_back = total_dist_km - segs[N-2-k].d_end_km;
        E_back += seg_fn(freq_hz, D_back, segs[N-1-k].gc, power_w)
                - seg_fn(freq_hz, D_back, segs[N-2-k].gc, power_w);
    }

    double lin_fwd  = std::pow(10.0, E_fwd  / 20.0);
    double lin_back = std::pow(10.0, E_back / 20.0);
    return 20.0 * std::log10(0.5 * (lin_fwd + lin_back));
}

// ---------------------------------------------------------------------------
// Homogeneous-path groundwave: single midpoint conductivity lookup.
// ---------------------------------------------------------------------------
double homogeneous_field_dbuvm(double freq_hz,
                                double lat_tx, double lon_tx,
                                double lat_rx, double lon_rx,
                                const ConductivityMap& cond,
                                double power_w)
{
    if (power_w <= 0.0) return -200.0;

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    double total_dist_m = 0.0;
    geod.Inverse(lat_tx, lon_tx, lat_rx, lon_rx, total_dist_m);
    double total_dist_km = total_dist_m / 1000.0;
    if (total_dist_km < 0.1) return -200.0;

    double mid_lat = 0.5 * (lat_tx + lat_rx);
    double mid_lon = 0.5 * (lon_tx + lon_rx);
    GroundConstants gc = cond.lookup(mid_lat, mid_lon);
    return groundwave_field_dbuvm(freq_hz, total_dist_km, gc, power_w);
}

// ---------------------------------------------------------------------------
// Model dispatch
// ---------------------------------------------------------------------------
double groundwave_for_model(double freq_hz,
                            double lat_tx, double lon_tx,
                            double lat_rx, double lon_rx,
                            const ConductivityMap& cond,
                            double power_w,
                            Scenario::PropagationModel model,
                            int nsamples)
{
    if (model == Scenario::PropagationModel::Homogeneous) {
        return homogeneous_field_dbuvm(freq_hz,
                                       lat_tx, lon_tx, lat_rx, lon_rx,
                                       cond, power_w);
    }
    if (model == Scenario::PropagationModel::GRWAVE) {
        return millington_with(freq_hz,
                               lat_tx, lon_tx, lat_rx, lon_rx,
                               cond, power_w,
                               grwave_field_dbuvm, nsamples);
    }
    return millington_field_dbuvm(freq_hz,
                                  lat_tx, lon_tx, lat_rx, lon_rx,
                                  cond, power_w, nsamples);
}

// ---------------------------------------------------------------------------
// Grid computation
// ---------------------------------------------------------------------------
void computeGroundwave(GridData&               data,
                       const Scenario&         scenario,
                       const std::atomic<bool>& cancel,
                       const std::function<void(int)>& progress_fn)
{
    auto it = data.layers.find("groundwave");
    if (it == data.layers.end()) return;
    const auto& pts = it->second.points;
    if (pts.empty()) return;

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    size_t n = pts.size();

    // Build conductivity map from scenario settings (P2-03)
    auto cond_map = make_conductivity_map(scenario);

    std::vector<double> rss_total(n, 0.0);

    size_t ntx        = scenario.transmitters.size();
    size_t total_work = n * std::max(ntx, (size_t)1);
    size_t done       = 0;
    int    last_pct   = -1;

    for (size_t ti = 0; ti < ntx; ++ti) {
        if (cancel.load()) return;
        const auto& tx = scenario.transmitters[ti];
        if (tx.power_w <= 0.0) {
            done += n;
            continue;
        }

        std::vector<double> vals(n);
        for (size_t i = 0; i < n; ++i) {
            if (cancel.load()) return;
            // Groundwave field strength using selected propagation model.
            double e = groundwave_for_model(
                scenario.frequencies.f1_hz,
                tx.lat, tx.lon, pts[i].lat, pts[i].lon,
                *cond_map, tx.power_w,
                scenario.propagation_model, 20);
            vals[i] = e;
            double lin = std::pow(10.0, e / 20.0);
            rss_total[i] += lin * lin;

            ++done;
            if (progress_fn) {
                int pct = (int)(done * 100 / total_work);
                if (pct != last_pct) {
                    progress_fn(pct);
                    last_pct = pct;
                }
            }
        }

        std::string key = "groundwave_" + std::to_string(tx.slot);
        GridArray& arr = data.layers[key];
        arr.layer_name    = key;
        arr.points        = pts;
        arr.values        = std::move(vals);
        arr.width         = it->second.width;
        arr.height        = it->second.height;
        arr.lat_min       = scenario.grid.lat_min;
        arr.lat_max       = scenario.grid.lat_max;
        arr.lon_min       = scenario.grid.lon_min;
        arr.lon_max       = scenario.grid.lon_max;
        arr.resolution_km = scenario.grid.resolution_km;
    }

    if (!cancel.load()) {
        auto& total = data.layers["groundwave"];
        total.values.resize(n);
        for (size_t i = 0; i < n; ++i) {
            total.values[i] = (rss_total[i] > 0.0)
                ? 20.0 * std::log10(std::sqrt(rss_total[i]))
                : -200.0;
        }
    }
}

} // namespace bp
