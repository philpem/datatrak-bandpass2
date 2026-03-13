#include "skywave.h"
#include <GeographicLib/Geodesic.hpp>
#include <cmath>
#include <algorithm>

namespace bp {

// ---------------------------------------------------------------------------
// ITU-R P.684-6 sky-wave field strength (LF/MF, night-time, median).
//
// Simplified model for planning; calibrated to typical LF sky-wave levels:
//   E_sky(dBuV/m) = 55 + 10*log10(P_kW) - 10*log10(d_km)
//                   - L_freq(f) - L_geomag(lat)
//
// Reference: ~25-35 dBuV/m at 500 km, 1 kW, 100 kHz, 52 deg lat.
// Within the skip zone (d < 100 km), returns -200 (sentinel).
// ---------------------------------------------------------------------------
double skywave_field_dbuvm(double freq_hz,
                            double dist_km,
                            double power_w,
                            double lat_tx,
                            double lat_rx)
{
    // Within skip zone no reliable 1-hop sky wave exists for LF
    if (dist_km < 100.0 || power_w <= 0.0) return -200.0;

    double f_khz    = freq_hz / 1000.0;
    double P_kw     = power_w / 1000.0;

    // Base level at 1 km, 1 kW, 100 kHz: 55 dBuV/m
    double E_base   = 55.0;
    double G_pwr    = 10.0 * std::log10(std::max(P_kw, 1e-9));
    // Cylindrical distance spreading (1/sqrt(d) → -10*log10(d))
    double L_dist   = 10.0 * std::log10(std::max(dist_km, 1.0));
    // Frequency-dependent ionospheric absorption relative to 100 kHz
    double L_freq   = 20.0 * std::log10(f_khz / 100.0);
    // Geomagnetic latitude correction
    double lat_mid  = 0.5 * (lat_tx + lat_rx);
    double L_geo    = 0.05 * std::abs(lat_mid - 52.0);

    return E_base + G_pwr - L_dist - L_freq - L_geo;
}

void computeSkywave(GridData& data, const Scenario& scenario,
                    const std::atomic<bool>& cancel)
{
    auto it = data.layers.find("skywave");
    if (it == data.layers.end()) return;
    const auto& pts = it->second.points;
    if (pts.empty()) return;

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    size_t n = pts.size();

    std::vector<double> rss(n, 0.0);

    for (const auto& tx : scenario.transmitters) {
        if (cancel.load()) return;
        if (tx.power_w <= 0.0) continue;
        for (size_t i = 0; i < n; ++i) {
            double dist_m = 0.0;
            geod.Inverse(tx.lat, tx.lon, pts[i].lat, pts[i].lon, dist_m);
            double e = skywave_field_dbuvm(scenario.frequencies.f1_hz,
                                           dist_m / 1000.0,
                                           tx.power_w,
                                           tx.lat, pts[i].lat);
            if (e > -199.0) {
                double lin = std::pow(10.0, e / 20.0);
                rss[i] += lin * lin;
            }
        }
    }

    auto& arr = it->second;
    for (size_t i = 0; i < n; ++i) {
        arr.values[i] = (rss[i] > 0.0) ? 20.0 * std::log10(std::sqrt(rss[i])) : -200.0;
    }
}

} // namespace bp
