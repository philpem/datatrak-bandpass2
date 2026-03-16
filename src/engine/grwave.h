#pragma once
#include "groundwave.h"
#include <array>
#include <functional>
#include <memory>

namespace bp {

// ITU-R P.368 GRWAVE groundwave field strength [dBµV/m].
//
// Full Sommerfeld/Wait/Norton computation for a smooth spherical Earth:
//   - Short distances: Norton flat-earth attenuation with curvature correction
//   - Long distances: spherical-earth residue series (Airy function roots)
//
// Uses the same reference field as groundwave_field_dbuvm():
//   E0 = 300 mV/m at d = 1 km for P = 1 kW (ITU-R P.368 short monopole)
//
// Parameters are identical to groundwave_field_dbuvm() for drop-in use.
// Vertical polarisation assumed (correct for Datatrak LF ground wave).
// Ground-level antennas assumed (h_tx = h_rx = 0).
//
// When a GrwaveLUT is active on this thread, the LUT is used for
// interpolation instead of the full residue series computation.
//
// Reference: NTIA Report 99-368 (DeMinco), ITU-R P.368-9.
double grwave_field_dbuvm(double freq_hz,
                           double dist_km,
                           const GroundConstants& gc,
                           double power_w);

// ---------------------------------------------------------------------------
// GRWAVE lookup table — precomputed attenuation indexed by (distance, sigma).
//
// At LF (30-300 kHz) the surface-wave attenuation is almost entirely
// determined by conductivity sigma; relative permittivity eps_r contributes
// < 0.1 dB because sigma/(eps0*omega) >> eps_r.  The LUT therefore indexes
// by sigma only, using eps_r=15 (land) for sigma < 1 S/m and eps_r=70 (sea)
// for sigma >= 1 S/m.
//
// Memory:  N_DIST * N_SIGMA * 8 bytes = 200 * 50 * 8 = 80 KB per table.
// Build:   ~10k calls to grwave_field_dbuvm (one-time, ~1 s on modern CPU).
// Lookup:  bilinear interpolation in log(dist) x log(sigma) space.
//
// Multiple LUTs (e.g. F1 + F2) can be active simultaneously on the same
// thread.  Scopes form a linked list; grwave_field_dbuvm() walks the chain
// to find the matching frequency.
// ---------------------------------------------------------------------------
class GrwaveLUT {
public:
    // Build the LUT for the given frequency.
    // progress_fn, if provided, is called with a value 0-100 during build.
    explicit GrwaveLUT(double freq_hz,
                       const std::function<void(int)>& progress_fn = {});

    // Interpolated field strength [dBuV/m] matching grwave_field_dbuvm().
    double lookup(double dist_km, const GroundConstants& gc, double power_w) const;

    double freq_hz() const { return freq_hz_; }

    // Thread-local active LUT chain.  grwave_field_dbuvm() walks this to
    // find a LUT matching the requested frequency.
    static const GrwaveLUT* find_active(double freq_hz);

    // RAII scope guard: pushes this LUT onto the active chain for the
    // current thread.  Multiple scopes can be nested (e.g. F1 + F2).
    struct Scope {
        explicit Scope(const GrwaveLUT& lut);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        const GrwaveLUT& lut_;
        const Scope* prev_;
        friend class GrwaveLUT;
    };

    // Legacy single-LUT query (returns first in chain, or null).
    static const GrwaveLUT* active();

private:
    static constexpr int N_DIST  = 200;
    static constexpr int N_SIGMA = 50;
    static constexpr double LOG_DIST_MIN  = -1.0;  // log10(0.1 km)
    static constexpr double LOG_DIST_MAX  =  3.0;  // log10(1000 km)
    static constexpr double LOG_SIGMA_MIN = -3.3;   // log10(0.0005 S/m)
    static constexpr double LOG_SIGMA_MAX =  0.7;   // log10(~5 S/m)

    double freq_hz_;
    double dist_step_;    // (LOG_DIST_MAX - LOG_DIST_MIN) / (N_DIST - 1)
    double sigma_step_;   // (LOG_SIGMA_MAX - LOG_SIGMA_MIN) / (N_SIGMA - 1)

    // Stored as attenuation in dB (field strength minus free-space reference).
    // atten_db_[di * N_SIGMA + si]
    std::array<double, N_DIST * N_SIGMA> atten_db_;

    static thread_local const Scope* active_scope_;
};

} // namespace bp
