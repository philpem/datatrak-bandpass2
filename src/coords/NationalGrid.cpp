#include "NationalGrid.h"
#include "Osgb.h"
#include <cmath>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace bp {
namespace national_grid {

// Airy 1830 ellipsoid parameters
namespace {
    constexpr double A  = 6377563.396;   // semi-major axis
    constexpr double B  = 6356256.909;   // semi-minor axis
    constexpr double E2 = (A*A - B*B) / (A*A);

    // Transverse Mercator projection parameters (OSGB)
    constexpr double F0 = 0.9996012717;  // central meridian scale factor
    constexpr double LAT0 = 49.0 * M_PI / 180.0;  // true origin latitude
    constexpr double LON0 = -2.0 * M_PI / 180.0;  // central meridian longitude
    constexpr double E0  =  400000.0;   // false easting (m)
    constexpr double N0  = -100000.0;   // false northing (m)

    double M_coeff(double phi) {
        // Meridional arc length from equator to phi (OSGB formula)
        double n = (A - B) / (A + B);
        double n2 = n*n, n3 = n2*n;
        return B * F0 * (
            (1.0 + n + 1.25*n2 + 1.25*n3) * (phi - LAT0)
          - (3.0*n + 3.0*n2 + 2.625*n3) * std::sin(phi - LAT0) * std::cos(phi + LAT0)
          + (1.875*n2 + 1.875*n3) * std::sin(2*(phi - LAT0)) * std::cos(2*(phi + LAT0))
          - (35.0/24.0*n3) * std::sin(3*(phi - LAT0)) * std::cos(3*(phi + LAT0))
        );
    }
} // anonymous namespace

EastNorth latlon_to_en(LatLon osgb36) {
    double phi = osgb36.lat * M_PI / 180.0;
    double lam = osgb36.lon * M_PI / 180.0;

    double sphi = std::sin(phi),  cphi = std::cos(phi);
    double tphi = std::tan(phi);
    double nu   = A * F0 / std::sqrt(1.0 - E2 * sphi * sphi);
    double rho  = A * F0 * (1.0 - E2) / std::pow(1.0 - E2 * sphi * sphi, 1.5);
    double eta2 = nu/rho - 1.0;
    double M    = M_coeff(phi);

    double dlam = lam - LON0;
    double I    = M + N0;
    double II   = 0.5 * nu * sphi * cphi;
    double III  = (nu/24.0) * sphi * cphi*cphi*cphi * (5.0 - tphi*tphi + 9.0*eta2);
    double IIIA = (nu/720.0) * sphi * cphi*cphi*cphi*cphi*cphi *
                  (61.0 - 58.0*tphi*tphi + tphi*tphi*tphi*tphi);
    double IV   = nu * cphi;
    double V    = (nu/6.0) * cphi*cphi*cphi * (nu/rho - tphi*tphi);
    double VI   = (nu/120.0) * cphi*cphi*cphi*cphi*cphi *
                  (5.0 - 18.0*tphi*tphi + tphi*tphi*tphi*tphi + 14.0*eta2 - 58.0*tphi*tphi*eta2);

    double N = I  + II*dlam*dlam + III*dlam*dlam*dlam*dlam + IIIA*dlam*dlam*dlam*dlam*dlam*dlam;
    double E = E0 + IV*dlam + V*dlam*dlam*dlam + VI*dlam*dlam*dlam*dlam*dlam;

    return { E, N };
}

LatLon en_to_latlon(EastNorth en) {
    // Iterative solution (OS algorithm)
    double E = en.easting;
    double N = en.northing;

    double phi = LAT0 + (N - N0) / (A * F0);
    double M;
    int iter = 0;
    do {
        phi += (N - N0 - M_coeff(phi)) / (A * F0);
        M = M_coeff(phi);
    } while (std::abs(N - N0 - M) >= 0.00001 && ++iter < 100);

    double sphi = std::sin(phi),  cphi = std::cos(phi);
    double tphi = std::tan(phi);
    double nu   = A * F0 / std::sqrt(1.0 - E2 * sphi * sphi);
    double rho  = A * F0 * (1.0 - E2) / std::pow(1.0 - E2 * sphi * sphi, 1.5);
    double eta2 = nu/rho - 1.0;

    double dE = E - E0;
    double VII  = tphi / (2.0 * rho * nu);
    double VIII = tphi / (24.0 * rho * nu*nu*nu) *
                  (5.0 + 3.0*tphi*tphi + eta2 - 9.0*tphi*tphi*eta2);
    double IX   = tphi / (720.0 * rho * nu*nu*nu*nu*nu) *
                  (61.0 + 90.0*tphi*tphi + 45.0*tphi*tphi*tphi*tphi);
    double X    = 1.0 / (cphi * nu);
    double XI   = 1.0 / (6.0 * cphi * nu*nu*nu) * (nu/rho + 2.0*tphi*tphi);
    double XII  = 1.0 / (120.0 * cphi * nu*nu*nu*nu*nu) *
                  (5.0 + 28.0*tphi*tphi + 24.0*tphi*tphi*tphi*tphi);
    double XIIA = 1.0 / (5040.0 * cphi * nu*nu*nu*nu*nu*nu*nu) *
                  (61.0 + 662.0*tphi*tphi + 1320.0*tphi*tphi*tphi*tphi +
                   720.0*tphi*tphi*tphi*tphi*tphi*tphi);

    double lat_rad = phi - VII*dE*dE + VIII*dE*dE*dE*dE - IX*dE*dE*dE*dE*dE*dE;
    double lon_rad = LON0 + X*dE - XI*dE*dE*dE + XII*dE*dE*dE*dE*dE - XIIA*dE*dE*dE*dE*dE*dE*dE;

    return { lat_rad * 180.0 / M_PI, lon_rad * 180.0 / M_PI };
}

std::string en_to_gridref(EastNorth en, int digits) {
    // Letters A-Z (no I) in row-major order from SW: A=col0,row0 ... Z=col4,row4
    static const char* LETTERS = "ABCDEFGHJKLMNOPQRSTUVWXYZ";

    // UK 500km major squares: 2 columns, up to 3 rows
    // col 0 = E:0-500000; col 1 = E:500000-1000000
    // row 0 = N:0-500000 (S/T); row 1 = N:500000-1000000 (N/O); row 2 = N:1000000-1500000 (H/J)
    // Corresponding letter indices in ABCDEFGHJKLMNOPQRSTUVWXYZ:
    // S=17, T=18, N=12, O=13, H=7, J=8
    static const int MAJOR_IDX[3][2] = { {17,18}, {12,13}, {7,8} };

    int e_col = (en.easting >= 500000.0) ? 1 : 0;
    int n_row = (int)(en.northing / 500000.0);
    if (n_row < 0) n_row = 0;
    if (n_row > 2) n_row = 2;
    char major_letter = LETTERS[MAJOR_IDX[n_row][e_col]];

    // Minor 100km letter within the 500km block
    // Offsets within block:
    double e_base = e_col * 500000.0;
    double n_base = n_row * 500000.0;
    int e100 = (int)((en.easting  - e_base) / 100000.0);  // 0-4
    int n100 = (int)((en.northing - n_base) / 100000.0);  // 0-4
    if (e100 < 0) e100 = 0; if (e100 > 4) e100 = 4;
    if (n100 < 0) n100 = 0; if (n100 > 4) n100 = 4;
    int minor_idx = n100 * 5 + e100;  // A(sw)…Z(ne)
    char minor_letter = LETTERS[minor_idx];

    // Numeric part within the 100km square
    int half = digits / 2;
    int divisor = (int)std::pow(10, 5 - half);
    int e_num = (int)(std::fmod(en.easting  - e_base, 100000.0)) / divisor;
    int n_num = (int)(std::fmod(en.northing - n_base, 100000.0)) / divisor;

    char buf[32];
    snprintf(buf, sizeof(buf), "%c%c %0*d %0*d", major_letter, minor_letter,
             half, e_num, half, n_num);
    return std::string(buf);
}

EastNorth gridref_to_en(const std::string& ref) {
    static const char* LETTERS = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
    static const int MAJOR_IDX[3][2] = { {17,18}, {12,13}, {7,8} };

    std::string s = ref;
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    if (s.size() < 4) throw std::invalid_argument("Grid ref too short: " + ref);

    char maj = (char)std::toupper(s[0]);
    char min = (char)std::toupper(s[1]);

    auto findLetter = [](const char* L, char c) -> int {
        for (int i = 0; i < 25; ++i) if (L[i] == c) return i;
        throw std::invalid_argument(std::string("Bad grid letter: ") + c);
        return -1;
    };

    int maj_idx = findLetter(LETTERS, maj);
    int min_idx = findLetter(LETTERS, min);

    // Decode major 500km base from MAJOR_IDX table
    double E500 = 0.0, N500 = 0.0;
    bool found = false;
    for (int row = 0; row < 3 && !found; ++row) {
        for (int col = 0; col < 2 && !found; ++col) {
            if (MAJOR_IDX[row][col] == maj_idx) {
                E500 = col * 500000.0;
                N500 = row * 500000.0;
                found = true;
            }
        }
    }
    if (!found) throw std::invalid_argument("Grid ref letter out of UK range: " + ref);

    // Decode minor 100km offset (A=0,0 … Z=4,4)
    int e100_idx = min_idx % 5;
    int n100_idx = min_idx / 5;

    // Numeric part
    std::string num_part = s.substr(2);
    if (num_part.size() % 2 != 0) throw std::invalid_argument("Odd numeric part in grid ref: " + ref);
    int half = (int)num_part.size() / 2;
    std::string es = num_part.substr(0, half);
    std::string ns = num_part.substr(half);
    double mult = std::pow(10.0, 5 - half);

    double E = E500 + e100_idx * 100000.0 + std::stoi(es) * mult;
    double N = N500 + n100_idx * 100000.0 + std::stoi(ns) * mult;

    return { E, N };
}

LatLon parse_coordinate(const std::string& input, bool use_ostn15) {
    std::string s = input;
    // Trim whitespace
    while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace(s.back()))  s.pop_back();

    // Check if first character is an alpha → grid reference
    if (!s.empty() && std::isalpha(s[0])) {
        EastNorth en = gridref_to_en(s);
        LatLon osgb  = en_to_latlon(en);
        return use_ostn15 ? osgb::osgb36_to_wgs84_ostn15(osgb)
                          : osgb::osgb36_to_wgs84(osgb);
    }

    // Try to parse as two numbers
    std::istringstream ss(s);
    double a, b;
    if (!(ss >> a >> b)) throw std::invalid_argument("Cannot parse coordinate: " + input);

    // Heuristic: if both values are large (> 1000) treat as raw E/N
    if (std::abs(a) > 1000.0 && std::abs(b) > 1000.0) {
        EastNorth en = { a, b };
        LatLon osgb  = en_to_latlon(en);
        return use_ostn15 ? osgb::osgb36_to_wgs84_ostn15(osgb)
                          : osgb::osgb36_to_wgs84(osgb);
    }

    // Otherwise treat as WGS84 decimal degrees
    return { a, b };
}

} // namespace national_grid
} // namespace bp
