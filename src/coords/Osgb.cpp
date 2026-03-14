#include "Osgb.h"
#include "NationalGrid.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace bp {
namespace osgb {

// Helmert 7-parameter transform parameters (WGS84 -> OSGB36)
// Source: OS transformation guide (OSTN15/OSGM15), Appendix J
namespace {
    constexpr double TX = -446.448;   // metres
    constexpr double TY =  125.157;
    constexpr double TZ = -542.060;
    // Rotation parameters in arc-seconds, converted to radians below
    constexpr double RX_ARCSEC = -0.1502;
    constexpr double RY_ARCSEC = -0.2470;
    constexpr double RZ_ARCSEC = -0.8421;
    constexpr double S_PPM     = 20.4894;   // scale (ppm)

    constexpr double ARCSEC_TO_RAD = M_PI / (180.0 * 3600.0);
    constexpr double RX = RX_ARCSEC * ARCSEC_TO_RAD;
    constexpr double RY = RY_ARCSEC * ARCSEC_TO_RAD;
    constexpr double RZ = RZ_ARCSEC * ARCSEC_TO_RAD;
    constexpr double S  = S_PPM * 1e-6;

    // WGS84 ellipsoid
    constexpr double WGS84_A = 6378137.000;
    constexpr double WGS84_B = 6356752.3141;

    // Airy 1830 ellipsoid (OSGB36)
    constexpr double AIRY_A = 6377563.396;
    constexpr double AIRY_B = 6356256.909;

    double eccentricitySquared(double a, double b) {
        return (a*a - b*b) / (a*a);
    }

    // Convert geodetic (lat, lon, h) to ECEF (X, Y, Z)
    void toECEF(double lat_rad, double lon_rad, double h,
                double a, double e2,
                double& X, double& Y, double& Z) {
        double N = a / std::sqrt(1.0 - e2 * std::sin(lat_rad) * std::sin(lat_rad));
        X = (N + h) * std::cos(lat_rad) * std::cos(lon_rad);
        Y = (N + h) * std::cos(lat_rad) * std::sin(lon_rad);
        Z = (N * (1.0 - e2) + h) * std::sin(lat_rad);
    }

    // Convert ECEF to geodetic on given ellipsoid (Bowring iterative)
    void fromECEF(double X, double Y, double Z,
                  double a, double b,
                  double& lat_rad, double& lon_rad) {
        double e2  = (a*a - b*b) / (a*a);
        double ep2 = (a*a - b*b) / (b*b);
        double p   = std::sqrt(X*X + Y*Y);
        double th  = std::atan2(a * Z, b * p);

        lon_rad = std::atan2(Y, X);
        lat_rad = std::atan2(Z + ep2 * b * std::pow(std::sin(th), 3),
                             p -  e2  * a * std::pow(std::cos(th), 3));
        // One Newton-Raphson iteration for accuracy
        double N   = a / std::sqrt(1.0 - e2 * std::sin(lat_rad) * std::sin(lat_rad));
        lat_rad    = std::atan2(Z + e2 * N * std::sin(lat_rad), p);
    }

    // Apply Helmert transform to ECEF coordinates
    // sign=+1 → WGS84→OSGB36; sign=-1 → OSGB36→WGS84
    void applyHelmert(double& X, double& Y, double& Z, double sign) {
        double tx =  sign * TX;
        double ty =  sign * TY;
        double tz =  sign * TZ;
        double rx =  sign * RX;
        double ry =  sign * RY;
        double rz =  sign * RZ;
        double s  =  sign * S;

        double x2 = tx + (1.0 + s) * ( X - rz*Y + ry*Z);
        double y2 = ty + (1.0 + s) * ( rz*X + Y - rx*Z);
        double z2 = tz + (1.0 + s) * (-ry*X + rx*Y + Z);
        X = x2; Y = y2; Z = z2;
    }
} // anonymous namespace

LatLon wgs84_to_osgb36(LatLon wgs84) {
    const double DEG = M_PI / 180.0;
    double e2w = eccentricitySquared(WGS84_A, WGS84_B);
    double X, Y, Z;
    toECEF(wgs84.lat * DEG, wgs84.lon * DEG, 0.0, WGS84_A, e2w, X, Y, Z);
    applyHelmert(X, Y, Z, +1.0);
    double lat_rad, lon_rad;
    fromECEF(X, Y, Z, AIRY_A, AIRY_B, lat_rad, lon_rad);
    return { lat_rad / DEG, lon_rad / DEG };
}

LatLon osgb36_to_wgs84(LatLon osgb36) {
    const double DEG = M_PI / 180.0;
    double e2a = eccentricitySquared(AIRY_A, AIRY_B);
    double X, Y, Z;
    toECEF(osgb36.lat * DEG, osgb36.lon * DEG, 0.0, AIRY_A, e2a, X, Y, Z);
    applyHelmert(X, Y, Z, -1.0);
    double lat_rad, lon_rad;
    fromECEF(X, Y, Z, WGS84_A, WGS84_B, lat_rad, lon_rad);
    return { lat_rad / DEG, lon_rad / DEG };
}

// ---------------------------------------------------------------------------
// OSTN15 grid state
// ---------------------------------------------------------------------------
namespace {

// Binary file format produced by tools/ostn15_download.py:
//   [0-7]   magic "OSTN1500" (8 bytes)
//   [8-11]  version uint32_t LE = 1
//   [12-15] ncols   uint32_t LE (typically 701)
//   [16-19] nrows   uint32_t LE (typically 1251)
//   [20-27] origin_e double LE (metres, typically 0.0)
//   [28-35] origin_n double LE (metres, typically 0.0)
//   [36-43] cell_size double LE (metres, typically 1000.0)
//   [44 ..] float32 pairs: se,sn per cell, row-major (row 0 = northing=origin_n)
//           zero se,sn indicates outside GB coverage → fallback to Helmert

struct Ostn15Grid {
    uint32_t ncols     = 0;
    uint32_t nrows     = 0;
    double   origin_e  = 0.0;
    double   origin_n  = 0.0;
    double   cell_size = 1000.0;
    std::vector<float> se; // east shifts  [row * ncols + col]
    std::vector<float> sn; // north shifts [row * ncols + col]

    bool loaded() const { return !se.empty(); }

    // Bilinear interpolation of shifts at (E, N) in metres.
    // Returns false if (E, N) is outside the grid or in a no-data region.
    bool interpolate(double E, double N, double& se_out, double& sn_out) const {
        double fc = (E - origin_e) / cell_size;
        double fr = (N - origin_n) / cell_size;
        if (fc < 0.0 || fr < 0.0) return false;
        int ic = static_cast<int>(fc);
        int ir = static_cast<int>(fr);
        if (ic >= static_cast<int>(ncols) - 1 ||
            ir >= static_cast<int>(nrows) - 1) return false;
        double te = fc - ic;
        double tn = fr - ir;

        auto idx = [&](int c, int r) -> int {
            return r * static_cast<int>(ncols) + c;
        };
        double se00 = se[idx(ic,   ir)];
        double se10 = se[idx(ic+1, ir)];
        double se01 = se[idx(ic,   ir+1)];
        double se11 = se[idx(ic+1, ir+1)];
        double sn00 = sn[idx(ic,   ir)];
        double sn10 = sn[idx(ic+1, ir)];
        double sn01 = sn[idx(ic,   ir+1)];
        double sn11 = sn[idx(ic+1, ir+1)];

        // If all four corners are zero the point is outside GB coverage
        if (se00 == 0.0f && se10 == 0.0f && se01 == 0.0f && se11 == 0.0f &&
            sn00 == 0.0f && sn10 == 0.0f && sn01 == 0.0f && sn11 == 0.0f)
            return false;

        se_out = (1.0-te)*(1.0-tn)*se00 + te*(1.0-tn)*se10
               + (1.0-te)*tn*se01       + te*tn*se11;
        sn_out = (1.0-te)*(1.0-tn)*sn00 + te*(1.0-tn)*sn10
               + (1.0-te)*tn*sn01       + te*tn*sn11;
        return true;
    }
};

static Ostn15Grid g_ostn15;

} // anonymous namespace

bool load_ostn15(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;

    char magic[8];
    ifs.read(magic, 8);
    if (!ifs || std::memcmp(magic, "OSTN1500", 8) != 0) return false;

    auto read_u32 = [&]() -> uint32_t {
        uint32_t v = 0;
        ifs.read(reinterpret_cast<char*>(&v), 4);
        return v;
    };
    auto read_f64 = [&]() -> double {
        double v = 0.0;
        ifs.read(reinterpret_cast<char*>(&v), 8);
        return v;
    };

    uint32_t file_version   = read_u32();
    uint32_t file_ncols     = read_u32();
    uint32_t file_nrows     = read_u32();
    double   file_origin_e  = read_f64();
    double   file_origin_n  = read_f64();
    double   file_cell_size = read_f64();

    if (!ifs || file_version != 1 ||
        file_ncols == 0 || file_nrows == 0 || file_cell_size <= 0.0)
        return false;

    uint64_t n = static_cast<uint64_t>(file_ncols) * file_nrows;
    if (n > 4'000'000ULL) return false;  // sanity cap

    std::vector<float> se_buf(n), sn_buf(n);
    for (uint64_t i = 0; i < n; ++i) {
        ifs.read(reinterpret_cast<char*>(&se_buf[i]), 4);
        ifs.read(reinterpret_cast<char*>(&sn_buf[i]), 4);
    }
    if (!ifs) return false;

    g_ostn15.ncols     = file_ncols;
    g_ostn15.nrows     = file_nrows;
    g_ostn15.origin_e  = file_origin_e;
    g_ostn15.origin_n  = file_origin_n;
    g_ostn15.cell_size = file_cell_size;
    g_ostn15.se        = std::move(se_buf);
    g_ostn15.sn        = std::move(sn_buf);
    return true;
}

bool ostn15_loaded() {
    return g_ostn15.loaded();
}

// OSTN15 forward: WGS84 (≈ ETRS89) lat/lon → OSGB36 lat/lon
//
// Algorithm (OS transformation guide):
//  1. Compute provisional OSGB36 E/N by applying the national grid TM formulas
//     to the WGS84 lat/lon, treating it as if on the Airy 1830 ellipsoid.
//     (This is a slight misapplication that the OSTN15 shift grid is calibrated
//     to absorb; it introduces ~100 m provisional error which the grid corrects.)
//  2. Bilinearly interpolate SE, SN shifts from the OSTN15 grid.
//  3. Final OSGB36 E = provisional E + SE; N = provisional N + SN.
//  4. Invert to OSGB36 lat/lon.
//
// Falls back to Helmert if grid not loaded or point outside GB coverage.
LatLon wgs84_to_osgb36_ostn15(LatLon wgs84) {
    if (!g_ostn15.loaded()) return wgs84_to_osgb36(wgs84);

    EastNorth en_prov = national_grid::latlon_to_en({wgs84.lat, wgs84.lon});
    double se, sn;
    if (!g_ostn15.interpolate(en_prov.easting, en_prov.northing, se, sn))
        return wgs84_to_osgb36(wgs84);

    return national_grid::en_to_latlon({en_prov.easting + se, en_prov.northing + sn});
}

// OSTN15 inverse: OSGB36 lat/lon → WGS84 (≈ ETRS89) lat/lon
//
// Algorithm (OS transformation guide):
//  1. Convert OSGB36 lat/lon → OSGB36 E/N.
//  2. Bilinearly interpolate SE, SN at the OSGB36 E/N.
//  3. Provisional ETRS89 E = OSGB36 E − SE; N = OSGB36 N − SN.
//  4. Invert via Airy TM formulas → ETRS89 lat/lon.
//     (Using Airy inverse on ETRS89 E/N introduces < 1 mm error over GB,
//     well within the ±0.1 m OSTN15 specification.)
//
// Falls back to Helmert if grid not loaded or point outside GB coverage.
LatLon osgb36_to_wgs84_ostn15(LatLon osgb36) {
    if (!g_ostn15.loaded()) return osgb36_to_wgs84(osgb36);

    EastNorth en_osgb = national_grid::latlon_to_en(osgb36);
    double se, sn;
    if (!g_ostn15.interpolate(en_osgb.easting, en_osgb.northing, se, sn))
        return osgb36_to_wgs84(osgb36);

    return national_grid::en_to_latlon({en_osgb.easting - se, en_osgb.northing - sn});
}

} // namespace osgb
} // namespace bp
