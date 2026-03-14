#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/groundwave.h"
#include "engine/conductivity.h"
#include "engine/noise.h"
#include "engine/skywave.h"
#include <GeographicLib/Geodesic.hpp>
#include <cmath>

using namespace bp;
using Catch::Approx;

static double geodesic_dist_km(double lat1, double lon1, double lat2, double lon2) {
    double d = 0.0;
    GeographicLib::Geodesic::WGS84().Inverse(lat1, lon1, lat2, lon2, d);
    return d / 1000.0;
}

// ---------------------------------------------------------------------------
// Test helpers for millington_field_dbuvm
// ---------------------------------------------------------------------------

// A conductivity map that always returns the same constants.
struct FixedConductivityMap : public ConductivityMap {
    GroundConstants gc;
    explicit FixedConductivityMap(double sigma, double eps_r)
        : gc{sigma, eps_r} {}
    GroundConstants lookup(double, double) const override { return gc; }
};

// A conductivity map that returns gc_west for lon < boundary_lon, gc_east otherwise.
struct StepConductivityMap : public ConductivityMap {
    double boundary_lon;
    GroundConstants gc_west, gc_east;
    GroundConstants lookup(double, double lon) const override {
        return lon < boundary_lon ? gc_west : gc_east;
    }
};

// ---- groundwave_field_dbuvm ----

TEST_CASE("groundwave: free-space limit at very high conductivity") {
    // Over very high conductivity, attenuation factor -> 0.
    // Free-space field: E0 = 300*sqrt(P_W)/d = 300*sqrt(40)/1 = 1897 uV/m = 65.56 dBuV/m
    GroundConstants gc_pec { 1e6, 15.0 };
    double E = groundwave_field_dbuvm(146437.5, 1.0, gc_pec, 40.0);
    CHECK(E == Approx(65.56).margin(0.1));
}

TEST_CASE("groundwave: field decreases with distance") {
    GroundConstants gc { 0.005, 15.0 };
    double E_100 = groundwave_field_dbuvm(146437.5, 100.0, gc, 40.0);
    double E_300 = groundwave_field_dbuvm(146437.5, 300.0, gc, 40.0);
    CHECK(E_100 > E_300);
    CHECK((E_100 - E_300) > 5.0);
}

TEST_CASE("groundwave: higher conductivity gives stronger signal") {
    GroundConstants gc_land { 0.005, 15.0 };
    GroundConstants gc_sea  { 4.0,   70.0 };
    double E_land = groundwave_field_dbuvm(146437.5, 200.0, gc_land, 40.0);
    double E_sea  = groundwave_field_dbuvm(146437.5, 200.0, gc_sea,  40.0);
    CHECK(E_sea > E_land);
    CHECK((E_sea - E_land) > 3.0);
}

TEST_CASE("groundwave: lower frequency gives stronger signal over land") {
    GroundConstants gc { 0.005, 15.0 };
    double E_high = groundwave_field_dbuvm(300000.0, 200.0, gc, 40.0);
    double E_low  = groundwave_field_dbuvm( 30000.0, 200.0, gc, 40.0);
    CHECK(E_low > E_high);
}

TEST_CASE("groundwave: Datatrak range (100-350 km) gives valid field strength") {
    GroundConstants gc { 0.005, 15.0 };
    double E_100 = groundwave_field_dbuvm(146437.5, 100.0, gc, 40.0);
    double E_350 = groundwave_field_dbuvm(146437.5, 350.0, gc, 40.0);
    CHECK(E_100 > E_350);
}

TEST_CASE("groundwave: zero/negative distance returns sentinel") {
    GroundConstants gc { 0.005, 15.0 };
    CHECK(groundwave_field_dbuvm(146437.5,  0.0, gc, 40.0) < -100.0);
    CHECK(groundwave_field_dbuvm(146437.5, -1.0, gc, 40.0) < -100.0);
}

// ---- BuiltInConductivityMap ----

TEST_CASE("conductivity: open sea gives sea constants") {
    BuiltInConductivityMap cm;
    // Mid-Atlantic well west of Ireland — should be sea
    GroundConstants gc = cm.lookup(52.0, -20.0);
    CHECK(gc.sigma > 1.0);    // sea: 4 S/m
    CHECK(gc.eps_r > 50.0);   // sea: 70
}

TEST_CASE("conductivity: UK mainland gives land constants") {
    BuiltInConductivityMap cm;
    // Central England
    GroundConstants gc = cm.lookup(52.3, -0.2);
    CHECK(gc.sigma < 0.1);    // land: 5 mS/m
    CHECK(gc.eps_r < 30.0);   // land: 15
}

TEST_CASE("conductivity: sea gives higher field strength") {
    BuiltInConductivityMap cm;
    GroundConstants gc_land = cm.lookup(52.3, -0.2);
    GroundConstants gc_sea  = cm.lookup(52.0, -20.0);
    double E_land = groundwave_field_dbuvm(146437.5, 200.0, gc_land, 40.0);
    double E_sea  = groundwave_field_dbuvm(146437.5, 200.0, gc_sea,  40.0);
    CHECK(E_sea > E_land);
}

// ---- atm_noise_dbuvm ----

TEST_CASE("atm_noise: increases at lower frequency") {
    double n_low  = atm_noise_dbuvm( 30e3);
    double n_high = atm_noise_dbuvm(300e3);
    CHECK(n_low > n_high);
}

TEST_CASE("atm_noise: LF noise level in reasonable range") {
    double n = atm_noise_dbuvm(146437.5);
    CHECK(n > -40.0);
    CHECK(n <  30.0);
}

// ---- skywave_field_dbuvm ----

TEST_CASE("skywave: skip-zone sentinel for d < 100 km") {
    double E = skywave_field_dbuvm(146437.5, 50.0, 40.0, 52.0, 52.0);
    CHECK(E < -100.0);
}

TEST_CASE("skywave: weaker than groundwave at 100 km") {
    // At 100 km, sky wave is in skip zone (-200), groundwave is real signal
    GroundConstants gc { 0.005, 15.0 };
    double E_gw  = groundwave_field_dbuvm(146437.5, 100.0, gc, 40.0);
    double E_sky = skywave_field_dbuvm(146437.5, 100.0, 40.0, 52.0, 52.0);
    CHECK(E_gw > E_sky);
}

TEST_CASE("skywave: decreases with distance beyond skip zone") {
    double E_200 = skywave_field_dbuvm(146437.5, 200.0, 40.0, 52.0, 52.0);
    double E_500 = skywave_field_dbuvm(146437.5, 500.0, 40.0, 52.0, 52.0);
    CHECK(E_200 > E_500);
}

// ---- millington_field_dbuvm ----

TEST_CASE("millington: homogeneous land path matches groundwave_field_dbuvm") {
    // Over a uniform conductivity, Millington must give exactly the same
    // result as the single-call formula (all correction increments cancel).
    GroundConstants gc_land { 0.005, 15.0 };
    FixedConductivityMap cmap(gc_land.sigma, gc_land.eps_r);

    // Two inland England points
    double lat1 = 52.0, lon1 = -0.5;
    double lat2 = 51.5, lon2 =  1.0;

    double dist_km = geodesic_dist_km(lat1, lon1, lat2, lon2);
    double E_ref = groundwave_field_dbuvm(146437.5, dist_km, gc_land, 40.0);
    double E_mil = millington_field_dbuvm(146437.5, lat1, lon1, lat2, lon2,
                                          cmap, 40.0, 20);
    CHECK(E_mil == Approx(E_ref).margin(0.02));
}

TEST_CASE("millington: homogeneous sea path matches groundwave_field_dbuvm") {
    GroundConstants gc_sea { 4.0, 70.0 };
    FixedConductivityMap cmap(gc_sea.sigma, gc_sea.eps_r);

    // Two open-sea points in the North Sea
    double lat1 = 55.0, lon1 = 2.0;
    double lat2 = 54.0, lon2 = 4.0;

    double dist_km = geodesic_dist_km(lat1, lon1, lat2, lon2);
    double E_ref = groundwave_field_dbuvm(146437.5, dist_km, gc_sea, 40.0);
    double E_mil = millington_field_dbuvm(146437.5, lat1, lon1, lat2, lon2,
                                          cmap, 40.0, 20);
    CHECK(E_mil == Approx(E_ref).margin(0.02));
}

TEST_CASE("millington: homogeneous N=1 degenerates to midpoint formula") {
    // With a single segment (N=1), Millington uses only the midpoint
    // conductivity — identical to groundwave_field_dbuvm at the true distance.
    GroundConstants gc { 0.005, 15.0 };
    FixedConductivityMap cmap(gc.sigma, gc.eps_r);

    double lat1 = 52.0, lon1 = -1.0;
    double lat2 = 52.0, lon2 =  1.0;

    double dist_km = geodesic_dist_km(lat1, lon1, lat2, lon2);
    double E_ref = groundwave_field_dbuvm(146437.5, dist_km, gc, 40.0);
    double E_mil = millington_field_dbuvm(146437.5, lat1, lon1, lat2, lon2,
                                          cmap, 40.0, 1);
    CHECK(E_mil == Approx(E_ref).margin(0.02));
}

TEST_CASE("millington: mixed sea-to-land path is between sea and land extremes") {
    // TX at sea (west of Wales), RX inland (central England).
    // Millington result must lie between the all-sea and all-land estimates.
    GroundConstants gc_sea  { 4.0,   70.0 };
    GroundConstants gc_land { 0.005, 15.0 };

    double lat_tx = 52.5, lon_tx = -5.5;  // sea, west of Wales
    double lat_rx = 52.5, lon_rx = -1.0;  // inland, central England
    // path crosses the Welsh coast ~halfway

    // A step map: sea west of -3.5°, land east of -3.5°
    StepConductivityMap cmap;
    cmap.boundary_lon = -3.5;
    cmap.gc_west      = gc_sea;
    cmap.gc_east      = gc_land;

    double dist_km = std::hypot(0.0, 4.5 * 111.0 * std::cos(52.5*M_PI/180.0));

    double E_sea  = groundwave_field_dbuvm(146437.5, dist_km, gc_sea,  40.0);
    double E_land = groundwave_field_dbuvm(146437.5, dist_km, gc_land, 40.0);
    double E_mil  = millington_field_dbuvm(146437.5,
                                           lat_tx, lon_tx, lat_rx, lon_rx,
                                           cmap, 40.0, 20);

    // Field over sea is stronger (lower attenuation); mixed path must lie
    // between the two homogeneous extremes.
    CHECK(E_mil > E_land);
    CHECK(E_mil < E_sea);
}

TEST_CASE("millington: mixed path differs from midpoint-only approximation") {
    // For a 50/50 land/sea path, the Millington result should differ
    // noticeably from a simple midpoint conductivity lookup.
    GroundConstants gc_sea  { 4.0,   70.0 };
    GroundConstants gc_land { 0.005, 15.0 };

    double lat_tx = 52.5, lon_tx = -5.5;  // sea
    double lat_rx = 52.5, lon_rx = -1.0;  // land

    StepConductivityMap cmap;
    cmap.boundary_lon = -3.25;  // boundary roughly at midpoint
    cmap.gc_west      = gc_sea;
    cmap.gc_east      = gc_land;

    // Midpoint of path is at boundary — gives sea conductivity if just west,
    // land if just east.  Use a fixed-midpoint conductivity for comparison.
    // Average conductivity (geometric) — crude approximation.
    GroundConstants gc_avg { 0.005, 15.0 };  // land (what midpoint lookup gives)
    FixedConductivityMap cmap_mid(gc_avg.sigma, gc_avg.eps_r);

    double dist_km = std::hypot(0.0, 4.5 * 111.0 * std::cos(52.5*M_PI/180.0));

    double E_mid_approx = groundwave_field_dbuvm(146437.5, dist_km, gc_avg, 40.0);
    double E_mil        = millington_field_dbuvm(146437.5,
                                                 lat_tx, lon_tx, lat_rx, lon_rx,
                                                 cmap, 40.0, 40);

    // Millington accounts for the sea segment, so it should be stronger
    // than the all-land approximation.
    CHECK(E_mil > E_mid_approx);
}

TEST_CASE("millington: both sea-to-land and land-to-sea lie between extremes") {
    // Both conductivity orderings (sea-first or land-first on the same TX→RX
    // path) should produce a result between the all-sea and all-land extremes.
    GroundConstants gc_sea  { 4.0,   70.0 };
    GroundConstants gc_land { 0.005, 15.0 };

    StepConductivityMap cmap_sl;  // sea west, land east (TX at sea)
    cmap_sl.boundary_lon = -3.5;
    cmap_sl.gc_west      = gc_sea;
    cmap_sl.gc_east      = gc_land;

    StepConductivityMap cmap_ls;  // land west, sea east (TX inland → sea)
    cmap_ls.boundary_lon = -3.5;
    cmap_ls.gc_west      = gc_land;
    cmap_ls.gc_east      = gc_sea;

    double lat_tx = 52.5, lon_tx = -5.5;
    double lat_rx = 52.5, lon_rx = -1.0;
    double dist_km = geodesic_dist_km(lat_tx, lon_tx, lat_rx, lon_rx);

    double E_sea  = groundwave_field_dbuvm(146437.5, dist_km, gc_sea,  40.0);
    double E_land = groundwave_field_dbuvm(146437.5, dist_km, gc_land, 40.0);

    double E_sl = millington_field_dbuvm(146437.5, lat_tx, lon_tx, lat_rx, lon_rx,
                                         cmap_sl, 40.0, 20);
    double E_ls = millington_field_dbuvm(146437.5, lat_tx, lon_tx, lat_rx, lon_rx,
                                         cmap_ls, 40.0, 20);

    // Both orderings should be bounded by the homogeneous extremes.
    CHECK(E_sl > E_land);
    CHECK(E_sl < E_sea);
    CHECK(E_ls > E_land);
    CHECK(E_ls < E_sea);
}
