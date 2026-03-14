#include "groundwave.h"
#include "conductivity.h"
#include <GeographicLib/Geodesic.hpp>
#include <cmath>
#include <algorithm>

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
// Free-space reference: E0 = 300*sqrt(P_W) / d_km  uV/m
// ---------------------------------------------------------------------------
double groundwave_field_dbuvm(double freq_hz,
                               double dist_km,
                               const GroundConstants& gc,
                               double power_w)
{
    if (dist_km <= 0.0 || power_w <= 0.0) return -200.0;

    // Free-space field for short monopole over perfect ground
    double E0_uvm   = 300.0 * std::sqrt(power_w) / dist_km;
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
// Grid computation
// ---------------------------------------------------------------------------
void computeGroundwave(GridData&               data,
                       const Scenario&         scenario,
                       const std::atomic<bool>& cancel)
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

    for (size_t ti = 0; ti < scenario.transmitters.size(); ++ti) {
        if (cancel.load()) return;
        const auto& tx = scenario.transmitters[ti];
        if (tx.power_w <= 0.0) continue;

        std::vector<double> vals(n);
        for (size_t i = 0; i < n; ++i) {
            double dist_m = 0.0;
            geod.Inverse(tx.lat, tx.lon, pts[i].lat, pts[i].lon, dist_m);
            double dist_km = std::max(dist_m / 1000.0, 0.1);
            // Conductivity lookup at the midpoint of the path
            double mid_lat = (tx.lat + pts[i].lat) / 2.0;
            double mid_lon = (tx.lon + pts[i].lon) / 2.0;
            GroundConstants gc = cond_map->lookup(mid_lat, mid_lon);
            double e = groundwave_field_dbuvm(
                scenario.frequencies.f1_hz, dist_km, gc, tx.power_w);
            vals[i] = e;
            double lin = std::pow(10.0, e / 20.0);
            rss_total[i] += lin * lin;
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
