#pragma once
#include "terrain.h"
#include "conductivity.h"

namespace bp {

// ---------------------------------------------------------------------------
// Monteath terrain method — surface-impedance path integration
//
// Williams (2004) §11, following Monteath (1973),
// "Applications of the Electromagnetic Reciprocity Principle".
//
// The ASF (Additional Secondary Factor) is the phase delay accumulated as the
// surface wave travels over lossy ground at slower-than-free-space velocity.
// Williams Eq. 11.1:
//
//   τ_asf = (1/c) × ∫₀ᵈ Re[1 - 1/n_eff(x)] dx
//
// where n_eff(x) is the effective refractive index of the surface wave at
// position x along the path, determined by the normalised surface impedance:
//
//   n_eff(x) = 1 / sqrt(1 - η(x)²)
//
// For |η| << 1 (valid for LF ground waves over typical ground):
//   Re[1 - 1/n_eff] ≈ 0.5 × |η|²
//
// The normalised surface impedance for a TM-polarised wave (Monteath 1973):
//   η(x) = 1 / sqrt(ε_c(x))
//   where ε_c = εᵣ(x) - j × σ(x)/(ω ε₀)  is the complex permittivity
//
//   |η|² = 1 / |ε_c| = 1 / sqrt(εᵣ(x)² + [σ(x)/(ωε₀)]²)
//
// This gives:
//   Re[1 - 1/n_eff] ≈ 0.5 / sqrt(εᵣ(x)² + [σ(x)/(ωε₀)]²)
//
// The path is sampled at nsamples points, conductivity is looked up from
// the scenario's ConductivityMap at each segment midpoint, and the integral
// is evaluated by the trapezoidal rule.
//
// Terrain heights from TerrainMap are included via a path-length correction:
// where the terrain has slope dh/dx, the surface wave travels a path
// sqrt(1 + (dh/dx)²) ≈ 1 + (dh/dx)²/2 times longer per unit ground distance.
// This is a small correction (typically < 0.1% for UK terrain) but correct.
//
// Returns: ASF in millilanes (positive = path delayed vs free-space).
//          A path over perfect conductor (σ→∞) returns 0.
// ---------------------------------------------------------------------------
double monteath_asf_ml(
    double freq_hz,
    double lat_tx, double lon_tx,
    double lat_rx, double lon_rx,
    const TerrainMap&      terrain,
    const ConductivityMap& cond,
    int nsamples = 50);

} // namespace bp
