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
//   4. Apply path-length slope correction for terrain height gradient
//   5. Integrate by midpoint rule: Σ f_σ(x_mid) × slope_factor(x) × dx
//   6. τ_asf = integral / c;  return τ_asf × freq_hz × 1000 millilanes
//
// The midpoint rule is used (not trapezoidal) because conductivity is
// sampled at segment midpoints.  Using the midpoint value — rather than
// averaging it with an endpoint value — avoids systematic bias when
// conductivity changes abruptly within a segment (e.g. coast crossing).
// Both rules are O(h²); the midpoint rule is the natural choice when the
// function is evaluated at midpoints.
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

    double integral = 0.0;
    double h0 = profile[0].height_m;
    double d0 = profile[0].dist_km * 1000.0;

    for (int i = 1; i < (int)profile.size(); ++i) {
        const HeightPoint& pi = profile[i];

        double d1   = pi.dist_km * 1000.0;
        double dx_m = d1 - d0;
        if (dx_m <= 0.0) { d0 = d1; h0 = pi.height_m; continue; }

        double h1 = pi.height_m;

        // Conductivity at segment midpoint (midpoint rule).
        // Using the midpoint is important for mixed land/sea paths: a
        // coastline may fall within a segment, and the midpoint conductivity
        // represents the full segment better than either endpoint.
        double mid_lat = 0.5 * (profile[i-1].lat + pi.lat);
        double mid_lon = 0.5 * (profile[i-1].lon + pi.lon);
        GroundConstants gc = cond.lookup(mid_lat, mid_lon);

        double xp      = gc.sigma / (omega * eps0);
        double f_sigma = 0.5 / std::sqrt(gc.eps_r * gc.eps_r + xp * xp);

        // Terrain slope correction: the surface wave travels along the terrain
        // surface, so the actual path length in the segment is:
        //   sqrt(dx_m² + dh²) ≈ dx_m × (1 + 0.5*(dh/dx_m)²)
        // At LF over UK terrain (typical slopes < 5°), this adds < 0.2%
        // to the integral.  For flat terrain (FlatTerrainMap), dh = 0.
        double slope_factor = 1.0;
        double dh = h1 - h0;
        if (dh != 0.0) {
            double slope = dh / dx_m;
            slope_factor = 1.0 + 0.5 * slope * slope;
        }

        integral += f_sigma * slope_factor * dx_m;

        d0 = d1;
        h0 = h1;
    }

    // τ_asf (seconds) = integral / c
    // Convert to millilanes: τ_asf × f × 1000
    return (integral / c) * freq_hz * 1000.0;
}

} // namespace bp
