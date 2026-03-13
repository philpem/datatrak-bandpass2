#include "Osgb.h"
#include <cmath>

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

// OSTN15 stub — falls back to Helmert until grid file is loaded
LatLon wgs84_to_osgb36_ostn15(LatLon wgs84) {
    return wgs84_to_osgb36(wgs84);
}

LatLon osgb36_to_wgs84_ostn15(LatLon osgb36) {
    return osgb36_to_wgs84(osgb36);
}

} // namespace osgb
} // namespace bp
