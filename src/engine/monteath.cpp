#include "monteath.h"
#include <cmath>
#include <algorithm>

namespace bp {

// ---------------------------------------------------------------------------
// Monteath surface-impedance integration (P2-05)
//
// Computes the ASF delay by integrating the local surface impedance factor
// along the great-circle path from TX to RX.  The conductivity at each
// path sample is looked up from the scenario's ConductivityMap, so mixed
// land/sea paths (e.g. coastal transmitters) are handled correctly.
//
// Algorithm:
//   1. Sample nsamples points along the great-circle path via terrain.profile()
//   2. At each segment, look up GroundConstants at the midpoint
//   3. Compute the local impedance factor f_σ = 0.5 × |η|²
//      = 0.5 / sqrt(εᵣ² + (σ/(ωε₀))²)
//   4. Add path-length slope correction f_h from terrain height gradient
//   5. Integrate by trapezoidal rule
//   6. τ_asf = integral / c;  return τ_asf × freq_hz × 1000 millilanes
// ---------------------------------------------------------------------------
double monteath_asf_ml(
    double freq_hz,
    double lat_tx, double lon_tx,
    double lat_rx, double lon_rx,
    const TerrainMap&      terrain,
    const ConductivityMap& cond,
    int nsamples)
{
    if (nsamples < 2) nsamples = 2;

    const double c     = 299'792'458.0;
    const double eps0  = 8.854187817e-12;
    const double omega = 2.0 * M_PI * freq_hz;

    // Sample the great-circle path; height_m values come from TerrainMap.
    // FlatTerrainMap returns 0.0 everywhere (the common case).
    auto profile = terrain.profile(lat_tx, lon_tx, lat_rx, lon_rx, nsamples);
    if ((int)profile.size() < 2) return 0.0;

    double total_dist_m = profile.back().dist_km * 1000.0;
    if (total_dist_m < 1.0) return 0.0;

    // Pre-compute impedance factor at each sample point for trapezoidal rule.
    // f(x) = f_σ(x) + f_h(x):
    //   f_σ(x) = 0.5 / sqrt(εᵣ(x)² + x_σ(x)²)  where x_σ = σ/(ωε₀)
    //   f_h(x) = slope correction (see below)

    // Trapezoidal integration: accumulate dx*(f0+f1)/2 for each segment
    double integral = 0.0;

    // Evaluate at first sample
    GroundConstants gc0 = cond.lookup(profile[0].lat, profile[0].lon);
    double xp0 = gc0.sigma / (omega * eps0);
    // Impedance factor at sample 0
    double f0  = 0.5 / std::sqrt(gc0.eps_r * gc0.eps_r + xp0 * xp0);
    double h0  = profile[0].height_m;
    double d0  = profile[0].dist_km * 1000.0;

    for (int i = 1; i < (int)profile.size(); ++i) {
        const HeightPoint& pi = profile[i];

        double d1   = pi.dist_km * 1000.0;
        double dx_m = d1 - d0;
        if (dx_m <= 0.0) { d0 = d1; h0 = pi.height_m; continue; }

        double h1 = pi.height_m;

        // Conductivity at segment midpoint
        double mid_lat = 0.5 * (profile[i-1].lat + pi.lat);
        double mid_lon = 0.5 * (profile[i-1].lon + pi.lon);
        GroundConstants gc1 = cond.lookup(mid_lat, mid_lon);

        double xp1 = gc1.sigma / (omega * eps0);
        double f1  = 0.5 / std::sqrt(gc1.eps_r * gc1.eps_r + xp1 * xp1);

        // Surface impedance contribution (trapezoidal)
        double f_sigma = 0.5 * (f0 + f1);

        // Terrain slope correction: the surface wave travels along the terrain
        // surface, so the actual path length in the segment is:
        //   sqrt(dx_m² + dh²) ≈ dx_m × (1 + 0.5*(dh/dx_m)²)
        // The additional path length per unit dx_m is 0.5*(dh/dx_m)².
        // We apply this as a multiplicative factor on the impedance integral
        // (since the wave is also accumulating impedance along the slope).
        //
        // At LF over UK terrain (typical slopes < 5°), this adds < 0.2%
        // to the integral.  For flat terrain (FlatTerrainMap), dh=0.
        double dh = h1 - h0;
        double slope_factor = 1.0;
        if (dh != 0.0) {
            double slope = dh / dx_m;
            slope_factor = 1.0 + 0.5 * slope * slope;
        }

        integral += f_sigma * slope_factor * dx_m;

        // Advance for next segment
        d0  = d1;
        h0  = h1;
        // Update f0 for the next iteration using the full conductivity at
        // this sample point (not the midpoint used above)
        gc0 = cond.lookup(pi.lat, pi.lon);
        xp0 = gc0.sigma / (omega * eps0);
        f0  = 0.5 / std::sqrt(gc0.eps_r * gc0.eps_r + xp0 * xp0);
    }

    // τ_asf (seconds) = integral / c
    double tau_asf = integral / c;

    // Convert to millilanes: 1 lane = c/f, so 1 ml = c/(f×1000)
    // τ_asf × f × 1000 = lanes × 1000 = millilanes
    return tau_asf * freq_hz * 1000.0;
}

} // namespace bp
