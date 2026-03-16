// ---------------------------------------------------------------------------
// ITU-R P.368 GRWAVE — Sommerfeld/Wait/Norton groundwave field strength
//
// Implements the smooth-Earth groundwave propagation model from:
//   NTIA Report 99-368 (DeMinco, 1999)
//   ITU-R Recommendation P.368-9
//   ITU Handbook on Ground Wave Propagation (2014)
//
// Two computational regions:
//   1. Flat-earth with curvature correction (Norton, short distances)
//   2. Residue series with spherical-earth diffraction (long distances)
//
// Transition: d_test = 80 / cbrt(f_MHz)  [SG3 Handbook Eq. 15]
//
// Simplifications for BANDPASS II:
//   - Vertical polarisation only (Datatrak LF ground wave)
//   - Ground-level antennas (h_tx = h_rx = 0), no height gain
//   - Standard atmosphere (N_s = 315 N-units)
//
// Reference implementation: NTIA/LFMF (github.com/NTIA/LFMF)
// ---------------------------------------------------------------------------

#include "grwave.h"
#include <complex>
#include <cmath>
#include <algorithm>
#include <array>

namespace bp {
namespace {

using cx = std::complex<double>;
constexpr double PI      = 3.14159265358979323846;
constexpr double C_LIGHT = 299'792'458.0;       // m/s
constexpr double EPS0    = 8.854187817e-12;      // F/m
constexpr double A0_KM   = 6370.0;              // Earth radius, km
constexpr double NS      = 315.0;               // surface refractivity, N-units
constexpr cx     J(0.0, 1.0);                   // imaginary unit

// =========================================================================
// Faddeeva function  W(z) = exp(-z^2) * erfc(-iz)
//
// Laplace continued fraction for large |z|, Kummer series for |z| <= 8.
// =========================================================================
cx wofz_cf(cx z, int nterms) {
    cx cfrac(0.0, 0.0);
    for (int n = nterms; n >= 1; --n)
        cfrac = cx(n * 0.5, 0.0) / (z - cfrac);
    return cx(0.0, 1.0 / std::sqrt(PI)) / (z - cfrac);
}

cx wofz(cx z) {
    double r = std::abs(z);

    if (r > 8.0) {
        // Laplace continued fraction — converges well for large |z|.
        // For Im(z) < 0, use W(z) = 2*exp(-z^2) - W(-z).
        bool negate = (std::imag(z) < 0.0);
        cx zc = negate ? -z : z;
        int nterms = (r > 15.0) ? 20 : 40;
        cx result = wofz_cf(zc, nterms);
        if (negate)
            result = 2.0 * std::exp(-z * z) - result;
        return result;
    }

    // Kummer series (converges for all z, stable near real axis):
    //   W(z) = exp(-z^2) + (2iz/sqrt(pi)) * sum_{n>=0} (-2z^2)^n / (2n+1)!!
    cx z2 = z * z;
    cx neg2z2 = -2.0 * z2;
    cx term(1.0, 0.0);
    cx sum(1.0, 0.0);
    for (int n = 1; n <= 300; ++n) {
        term *= neg2z2 / cx(2.0 * n + 1.0, 0.0);
        sum += term;
        if (std::abs(term) < 1e-15 * std::abs(sum) && n > 5) break;
    }
    return std::exp(-z2) + cx(0.0, 2.0 / std::sqrt(PI)) * z * sum;
}

// =========================================================================
// Airy functions Ai(z), Ai'(z), Bi(z), Bi'(z)
//
// Power series from ODE recurrence y'' = z*y for |z| <= 15,
// asymptotic expansion (DLMF 9.7) for |z| > 15.
// =========================================================================

constexpr double AI0  =  0.35502805388781724;
constexpr double AIP0 = -0.25881940379280680;
constexpr double BI0  =  0.61492662744600074;
constexpr double BIP0 =  0.44828835735382636;

struct AiryResult {
    cx ai, aip, bi, bip;
};

AiryResult airy_series(cx z) {
    cx z3 = z * z * z;

    cx ai_t0(AI0, 0),  ai_t1(AIP0, 0);
    cx bi_t0(BI0, 0),  bi_t1(BIP0, 0);
    cx ai_s0(AI0, 0),  ai_s1(AIP0, 0);
    cx bi_s0(BI0, 0),  bi_s1(BIP0, 0);
    cx aip_s0(0, 0),   aip_s1(AIP0, 0);
    cx bip_s0(0, 0),   bip_s1(BIP0, 0);

    for (int k = 1; k < 80; ++k) {
        int n0 = 3 * k;
        int n1 = 3 * k + 1;

        double d0 = 1.0 / ((double)n0 * (double)(n0 - 1));
        double d1 = 1.0 / ((double)n1 * (double)(n1 - 1));

        ai_t0 *= z3 * d0;  ai_t1 *= z3 * d1;
        bi_t0 *= z3 * d0;  bi_t1 *= z3 * d1;

        ai_s0 += ai_t0;  ai_s1 += ai_t1;
        bi_s0 += bi_t0;  bi_s1 += bi_t1;

        aip_s0 += (double)n0 * ai_t0;
        aip_s1 += (double)n1 * ai_t1;
        bip_s0 += (double)n0 * bi_t0;
        bip_s1 += (double)n1 * bi_t1;

        double mag = std::abs(ai_t0) + std::abs(ai_t1);
        double ref = std::abs(ai_s0) + std::abs(ai_s1);
        if (mag < 1e-16 * ref && k > 5) break;
    }

    AiryResult r;
    r.ai  = ai_s0 + ai_s1 * z;
    r.bi  = bi_s0 + bi_s1 * z;
    if (std::abs(z) > 1e-30) {
        r.aip = aip_s0 / z + aip_s1;
        r.bip = bip_s0 / z + bip_s1;
    } else {
        r.aip = cx(AIP0, 0);
        r.bip = cx(BIP0, 0);
    }
    return r;
}

AiryResult airy_asymptotic(cx z) {
    cx z32 = std::pow(z, 1.5);
    cx zeta = (2.0 / 3.0) * z32;
    cx zm14 = std::pow(z, -0.25);
    cx zp14 = std::pow(z, 0.25);
    cx inv_zeta = 1.0 / zeta;

    constexpr int N = 15;
    double u[N], v[N];
    u[0] = 1.0;
    v[0] = 1.0;
    for (int k = 1; k < N; ++k) {
        u[k] = u[k-1] * (6.0*k - 1.0) * (6.0*k - 3.0) * (6.0*k - 5.0) / (216.0 * k);
        v[k] = -u[k] * (6.0*k + 1.0) / (6.0*k - 1.0);
    }

    cx s_ai(1,0), s_bi(1,0), s_aip(1,0), s_bip(1,0);
    cx zp = inv_zeta;
    double sign = -1.0;
    for (int k = 1; k < N; ++k) {
        s_ai  += sign * u[k] * zp;
        s_bi  +=         u[k] * zp;
        s_aip += sign * v[k] * zp;
        s_bip +=         v[k] * zp;
        zp *= inv_zeta;
        sign = -sign;
    }

    cx en = std::exp(-zeta);
    cx ep = std::exp(zeta);
    double ispi = 1.0 / std::sqrt(PI);

    AiryResult r;
    r.ai  =  zm14 * en * (0.5 * ispi) * s_ai;
    r.bi  =  zm14 * ep * ispi * s_bi;
    r.aip = -zp14 * en * (0.5 * ispi) * s_aip;
    r.bip =  zp14 * ep * ispi * s_bip;
    return r;
}

AiryResult airy(cx z) {
    return (std::abs(z) > 15.0) ? airy_asymptotic(z) : airy_series(z);
}

// =========================================================================
// Airy function of the 3rd kind — Hufford Wi^(2):
//   Wi(z) = Ai(z) + i*Bi(z)
//   Wi'(z) = Ai'(z) + i*Bi'(z)
//
// Derivative zeros at Ai' zeros rotated by exp(+2*pi*i/3) -> fourth quadrant,
// giving convergent exp(-i*x*T) terms in the residue series.
// Matches NTIA/LFMF WONE+WAIT convention (= WTWO+HUFFORD).
// =========================================================================
struct WiResult { cx wi, wip; };

WiResult wi_func(cx z) {
    auto a = airy(z);
    return { a.ai + J * a.bi,
             a.aip + J * a.bip };
}

// =========================================================================
// Root finder: n-th root of Wi'(t) - q*Wi(t) = 0
//
// Newton iteration starting from tabulated zeros of Ai'(z), rotated
// into the Wi root sector by exp(2*pi*i/3).
// =========================================================================

// First 15 zeros of Ai'(z) — NIST DLMF Table 9.9.1
constexpr std::array<double, 15> AIP_ZEROS = {
    -1.01879297, -3.24819758, -4.82009921, -6.16330736, -7.37217726,
    -8.48848673, -9.53544905, -10.5277554, -11.4750658, -12.3847884,
    -13.2636396, -14.1160358, -14.9453151, -15.7541684, -16.5446260
};

cx wi_root(int n, cx q) {
    cx t;
    if (n <= (int)AIP_ZEROS.size()) {
        t = cx(AIP_ZEROS[n - 1], 0.0);
    } else {
        double arg = 3.0 * PI * (4.0 * n - 3.0) / 8.0;
        t = cx(-std::pow(arg, 2.0 / 3.0), 0.0);
    }

    // Rotate into Wi^(2) root sector (fourth quadrant)
    cx rot = std::exp(cx(0.0, 2.0 * PI / 3.0));
    t *= rot;

    // Newton iteration: F(t) = Wi'(t) - q*Wi(t) = 0
    // F'(t) = t*Wi(t) - q*Wi'(t)   (from ODE: Wi''(t) = t*Wi(t))
    for (int iter = 0; iter < 30; ++iter) {
        auto [wi, wip] = wi_func(t);
        cx F = wip - q * wi;
        cx Fp = t * wi - q * wip;
        if (std::abs(Fp) < 1e-30) break;
        cx dt = F / Fp;
        t -= dt;
        if (std::abs(dt) < 1e-12 * (std::abs(t) + 1e-30)) break;
    }
    return t;
}

// =========================================================================
// Flat-earth with curvature correction (h_tx = h_rx = 0)
//
// Norton attenuation factor using Faddeeva function.
// DeMinco 99-368 Eq. 32.
// =========================================================================
double flat_earth_atten(double k, double d_km, cx delta, cx q) {
    cx qi = cx(-0.5, 0.5) * std::sqrt(k * d_km) * delta;
    cx p  = qi * qi;

    cx W_qi = wofz(qi);
    cx F_p = 1.0 + std::sqrt(PI) * J * qi * W_qi;

    cx f_x;
    if (std::abs(q) > 0.1) {
        // Curvature correction (DeMinco Eq. 35)
        cx q3 = q * q * q;
        cx q6 = q3 * q3;
        cx sqrtp = J * std::sqrt(PI * p);
        cx corr1 = (1.0 - sqrtp - (1.0 + 2.0 * p) * F_p) / (4.0 * q3);
        cx corr2 = (1.0 - sqrtp * (1.0 - p)
                    - 2.0 * p + 5.0 * p * p / 6.0
                    + (p * p / 2.0 - 1.0) * F_p) / (4.0 * q6);
        f_x = F_p + corr1 + corr2;
    } else {
        f_x = F_p;
    }

    return std::abs(f_x);
}

// =========================================================================
// Residue series for spherical-earth diffraction (h_tx = h_rx = 0)
//
// DeMinco 99-368 Eq. 24-26.
// =========================================================================
double residue_atten(double k, double d_km, double ae, double nu, cx q) {
    double theta = d_km / ae;
    double x = nu * theta;
    cx q2 = q * q;

    cx GW(0.0, 0.0);
    for (int n = 1; n <= 200; ++n) {
        cx T = wi_root(n, q);
        cx denom = T - q2;
        if (std::abs(denom) < 1e-30) continue;
        cx G = std::exp(-J * x * T) / denom;
        GW += G;
        if (n > 3 && std::abs(G) < 0.0005 * std::abs(GW)) break;
    }

    cx prefactor = std::sqrt(cx(x, 0.0)) * cx(std::sqrt(PI / 2.0),
                                               -std::sqrt(PI / 2.0));
    return std::abs(prefactor * GW);
}

} // anonymous namespace

// =========================================================================
// Full GRWAVE computation (always runs the residue series — used for LUT
// construction and when no LUT is active).
// =========================================================================
static double grwave_field_full(double freq_hz,
                                 double dist_km,
                                 const GroundConstants& gc,
                                 double power_w)
{
    if (dist_km <= 0.0 || power_w <= 0.0) return -200.0;

    // Free-space reference (ITU-R P.368, short monopole over perfect ground)
    double P_kW = power_w / 1000.0;
    double E0_uvm = 300.0e3 * std::sqrt(P_kW) / dist_km;
    double E0_dbuvm = 20.0 * std::log10(std::max(E0_uvm, 1e-20));

    // Wavenumber in km (wavelength in km)
    double lambda_km = C_LIGHT / (freq_hz * 1000.0);
    double k = 2.0 * PI / lambda_km;

    // Effective earth radius (standard atmosphere)
    double ae = A0_KM / (1.0 - 0.04665 * std::exp(0.005577 * NS));

    // Fock parameter
    double nu = std::cbrt(ae * k / 2.0);

    // Complex dielectric constant (DeMinco Eq. 17)
    double omega = 2.0 * PI * freq_hz;
    double sigma_eff = std::max(gc.sigma, 1e-10);
    cx eta(gc.eps_r, -sigma_eff / (EPS0 * omega));

    // Normalised surface impedance, vertical pol (DeMinco Eq. 15)
    cx delta = std::sqrt(eta - 1.0) / eta;

    // Fock q parameter (DeMinco Eq. 18)
    cx q = -J * nu * delta;

    // Transition distance (SG3 Handbook Eq. 15)
    double d_test = 80.0 / std::cbrt(freq_hz / 1.0e6);

    // Compute attenuation factor
    double atten;
    if (dist_km < d_test) {
        atten = flat_earth_atten(k, dist_km, delta, q);
    } else {
        atten = residue_atten(k, dist_km, ae, nu, q);
    }

    atten = std::clamp(atten, 0.0, 1.0);
    double atten_db = 20.0 * std::log10(std::max(atten, 1e-20));
    return E0_dbuvm + atten_db;
}

// =========================================================================
// Public API — uses LUT when available, full computation otherwise
// =========================================================================
double grwave_field_dbuvm(double freq_hz,
                           double dist_km,
                           const GroundConstants& gc,
                           double power_w)
{
    auto* lut = GrwaveLUT::find_active(freq_hz);
    if (lut)
        return lut->lookup(dist_km, gc, power_w);
    return grwave_field_full(freq_hz, dist_km, gc, power_w);
}

// =========================================================================
// GrwaveLUT implementation
// =========================================================================

thread_local const GrwaveLUT::Scope* GrwaveLUT::active_scope_ = nullptr;

const GrwaveLUT* GrwaveLUT::find_active(double freq_hz) {
    for (auto* s = active_scope_; s; s = s->prev_)
        if (s->lut_.freq_hz() == freq_hz)
            return &s->lut_;
    return nullptr;
}

const GrwaveLUT* GrwaveLUT::active() {
    return active_scope_ ? &active_scope_->lut_ : nullptr;
}

GrwaveLUT::Scope::Scope(const GrwaveLUT& lut)
    : lut_(lut), prev_(active_scope_)
{
    active_scope_ = this;
}

GrwaveLUT::Scope::~Scope() {
    active_scope_ = prev_;
}

GrwaveLUT::GrwaveLUT(double freq_hz, const std::function<void(int)>& progress_fn)
    : freq_hz_(freq_hz)
{
    dist_step_  = (LOG_DIST_MAX  - LOG_DIST_MIN)  / (N_DIST  - 1);
    sigma_step_ = (LOG_SIGMA_MAX - LOG_SIGMA_MIN) / (N_SIGMA - 1);

    // Precompute attenuation at each (dist, sigma) grid point.
    // Use power_w = 1 kW so the free-space reference E0 at d=1 km is
    // 300 mV/m = 109.54 dBuV/m.  Store atten_db = field - E0 so that
    // lookup() can reconstruct any power level cheaply.
    int last_pct = -1;
    for (int di = 0; di < N_DIST; ++di) {
        double log_d = LOG_DIST_MIN + di * dist_step_;
        double d_km  = std::pow(10.0, log_d);

        // E0 at this distance for 1 kW
        double E0_uvm   = 300.0e3 / d_km;
        double E0_dbuvm = 20.0 * std::log10(std::max(E0_uvm, 1e-20));

        for (int si = 0; si < N_SIGMA; ++si) {
            double log_s = LOG_SIGMA_MIN + si * sigma_step_;
            double sigma = std::pow(10.0, log_s);

            // eps_r: sigma >= 1 S/m is sea-like, else land-like.
            // At LF the choice barely matters (sigma/(eps0*omega) >> eps_r).
            double eps_r = (sigma >= 1.0) ? 70.0 : 15.0;
            GroundConstants gc{ sigma, eps_r };

            double field = grwave_field_full(freq_hz, d_km, gc, 1000.0);
            atten_db_[di * N_SIGMA + si] = field - E0_dbuvm;
        }

        if (progress_fn) {
            int pct = (di + 1) * 100 / N_DIST;
            if (pct != last_pct) {
                progress_fn(pct);
                last_pct = pct;
            }
        }
    }
}

double GrwaveLUT::lookup(double dist_km, const GroundConstants& gc,
                          double power_w) const
{
    if (dist_km <= 0.0 || power_w <= 0.0) return -200.0;

    // Free-space reference for the actual power and distance
    double P_kW = power_w / 1000.0;
    double E0_uvm   = 300.0e3 * std::sqrt(P_kW) / dist_km;
    double E0_dbuvm = 20.0 * std::log10(std::max(E0_uvm, 1e-20));

    // Locate in log-space and bilinearly interpolate
    double log_d = std::log10(std::max(dist_km, 0.1));
    double log_s = std::log10(std::max(gc.sigma, 5e-4));

    double fd = (log_d - LOG_DIST_MIN) / dist_step_;
    double fs = (log_s - LOG_SIGMA_MIN) / sigma_step_;

    // Clamp to table bounds
    fd = std::clamp(fd, 0.0, (double)(N_DIST  - 1));
    fs = std::clamp(fs, 0.0, (double)(N_SIGMA - 1));

    int di0 = std::min((int)fd, N_DIST  - 2);
    int si0 = std::min((int)fs, N_SIGMA - 2);
    double td = fd - di0;
    double ts = fs - si0;

    // Bilinear interpolation
    double v00 = atten_db_[di0       * N_SIGMA + si0    ];
    double v10 = atten_db_[(di0 + 1) * N_SIGMA + si0    ];
    double v01 = atten_db_[di0       * N_SIGMA + si0 + 1];
    double v11 = atten_db_[(di0 + 1) * N_SIGMA + si0 + 1];

    double atten = v00 * (1.0 - td) * (1.0 - ts)
                 + v10 * td         * (1.0 - ts)
                 + v01 * (1.0 - td) * ts
                 + v11 * td         * ts;

    return E0_dbuvm + atten;
}

} // namespace bp
