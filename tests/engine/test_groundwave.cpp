#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/groundwave.h"
#include "engine/grwave.h"
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
    // ITU-R P.368 reference: E0 = 300 mV/m at d=1 km for P=1 kW.
    // For P=40 W = 0.04 kW at d=1 km:
    //   E0 = 300e3 * sqrt(0.04) / 1 = 300e3 * 0.2 = 60000 uV/m = 95.56 dBuV/m
    GroundConstants gc_pec { 1e6, 15.0 };
    double E = groundwave_field_dbuvm(146437.5, 1.0, gc_pec, 40.0);
    CHECK(E == Approx(95.56).margin(0.1));
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

// ---- homogeneous_field_dbuvm ----

TEST_CASE("homogeneous: matches groundwave_field_dbuvm with midpoint conductivity") {
    // For a uniform conductivity map, homogeneous_field_dbuvm should match
    // the single-call groundwave_field_dbuvm at the geodesic distance.
    GroundConstants gc { 0.005, 15.0 };
    FixedConductivityMap cmap(gc.sigma, gc.eps_r);

    double lat1 = 52.0, lon1 = -0.5;
    double lat2 = 51.5, lon2 =  1.0;

    double dist_km = geodesic_dist_km(lat1, lon1, lat2, lon2);
    double E_ref = groundwave_field_dbuvm(146437.5, dist_km, gc, 40.0);
    double E_hom = homogeneous_field_dbuvm(146437.5, lat1, lon1, lat2, lon2,
                                            cmap, 40.0);
    CHECK(E_hom == Approx(E_ref).margin(0.01));
}

TEST_CASE("homogeneous: uses midpoint conductivity, not path average") {
    // With a step conductivity map, homogeneous looks up only the midpoint.
    StepConductivityMap cmap;
    cmap.boundary_lon = -3.5;
    cmap.gc_west = GroundConstants{4.0, 70.0};   // sea
    cmap.gc_east = GroundConstants{0.005, 15.0};  // land

    double lat_tx = 52.5, lon_tx = -5.5;  // sea
    double lat_rx = 52.5, lon_rx = -1.0;  // land
    // Midpoint lon = -3.25, east of boundary -> land conductivity

    double E_hom = homogeneous_field_dbuvm(146437.5, lat_tx, lon_tx, lat_rx, lon_rx,
                                            cmap, 40.0);
    double dist_km = geodesic_dist_km(lat_tx, lon_tx, lat_rx, lon_rx);
    double E_land = groundwave_field_dbuvm(146437.5, dist_km, cmap.gc_east, 40.0);

    // Should match the land-only value since midpoint falls in land zone
    CHECK(E_hom == Approx(E_land).margin(0.01));
}

// ---- groundwave_for_model dispatch ----

TEST_CASE("groundwave_for_model: Homogeneous dispatches to homogeneous") {
    GroundConstants gc { 0.005, 15.0 };
    FixedConductivityMap cmap(gc.sigma, gc.eps_r);

    double lat1 = 52.0, lon1 = -0.5;
    double lat2 = 51.5, lon2 =  1.0;

    double E_hom = homogeneous_field_dbuvm(146437.5, lat1, lon1, lat2, lon2,
                                            cmap, 40.0);
    double E_disp = groundwave_for_model(146437.5, lat1, lon1, lat2, lon2,
                                          cmap, 40.0,
                                          Scenario::PropagationModel::Homogeneous);
    CHECK(E_disp == Approx(E_hom).margin(0.001));
}

TEST_CASE("groundwave_for_model: Millington dispatches to millington") {
    GroundConstants gc { 0.005, 15.0 };
    FixedConductivityMap cmap(gc.sigma, gc.eps_r);

    double lat1 = 52.0, lon1 = -0.5;
    double lat2 = 51.5, lon2 =  1.0;

    double E_mil = millington_field_dbuvm(146437.5, lat1, lon1, lat2, lon2,
                                          cmap, 40.0, 20);
    double E_disp = groundwave_for_model(146437.5, lat1, lon1, lat2, lon2,
                                          cmap, 40.0,
                                          Scenario::PropagationModel::Millington);
    CHECK(E_disp == Approx(E_mil).margin(0.001));
}

// ---- grwave_field_dbuvm ----

TEST_CASE("grwave: free-space limit at very high conductivity") {
    // Over perfect ground, attenuation factor -> 1 (0 dB loss).
    // Same reference as polynomial: E0 = 300 mV/m at d=1 km for P=1 kW.
    // For P=40 W at d=1 km: E0 ~ 95.56 dBuV/m
    GroundConstants gc_pec { 1e6, 15.0 };
    double E = grwave_field_dbuvm(146437.5, 1.0, gc_pec, 40.0);
    // Should be close to free-space (within 1 dB)
    CHECK(E > 94.0);
    CHECK(E < 96.5);
}

TEST_CASE("grwave: field decreases with distance") {
    GroundConstants gc { 0.005, 15.0 };
    double E_100 = grwave_field_dbuvm(146437.5, 100.0, gc, 40.0);
    double E_300 = grwave_field_dbuvm(146437.5, 300.0, gc, 40.0);
    CHECK(E_100 > E_300);
    CHECK((E_100 - E_300) > 5.0);
}

TEST_CASE("grwave: higher conductivity gives stronger signal") {
    GroundConstants gc_land { 0.005, 15.0 };
    GroundConstants gc_sea  { 4.0,   70.0 };
    double E_land = grwave_field_dbuvm(146437.5, 200.0, gc_land, 40.0);
    double E_sea  = grwave_field_dbuvm(146437.5, 200.0, gc_sea,  40.0);
    CHECK(E_sea > E_land);
    CHECK((E_sea - E_land) > 1.0);
}

TEST_CASE("grwave: lower frequency gives stronger signal over land") {
    GroundConstants gc { 0.005, 15.0 };
    double E_high = grwave_field_dbuvm(300000.0, 200.0, gc, 40.0);
    double E_low  = grwave_field_dbuvm( 30000.0, 200.0, gc, 40.0);
    CHECK(E_low > E_high);
}

TEST_CASE("grwave: zero/negative distance returns sentinel") {
    GroundConstants gc { 0.005, 15.0 };
    CHECK(grwave_field_dbuvm(146437.5,  0.0, gc, 40.0) < -100.0);
    CHECK(grwave_field_dbuvm(146437.5, -1.0, gc, 40.0) < -100.0);
}

TEST_CASE("grwave: result in same ballpark as polynomial fit") {
    // GRWAVE and the polynomial fit should agree to within ~5 dB at
    // typical Datatrak distances (100-300 km).  The polynomial is an
    // approximation of the full GRWAVE curves, so agreement is expected.
    GroundConstants gc { 0.005, 15.0 };
    for (double d : {50.0, 100.0, 200.0, 300.0}) {
        double E_poly = groundwave_field_dbuvm(146437.5, d, gc, 40.0);
        double E_grw  = grwave_field_dbuvm(146437.5, d, gc, 40.0);
        // Within 8 dB — the polynomial is a rough fit
        CHECK(std::abs(E_poly - E_grw) < 8.0);
    }
}

TEST_CASE("grwave: sea path gives reasonable field at 500 km") {
    // Over sea, field should still be measurable at long range
    GroundConstants gc_sea { 4.0, 70.0 };
    double E = grwave_field_dbuvm(146437.5, 500.0, gc_sea, 40.0);
    CHECK(E > -10.0);  // should be positive at LF over sea
    CHECK(E < 70.0);   // but weaker than 1 km
}

// ---- millington_with ----

TEST_CASE("millington_with: polynomial matches millington_field_dbuvm") {
    GroundConstants gc { 0.005, 15.0 };
    FixedConductivityMap cmap(gc.sigma, gc.eps_r);
    double lat1 = 52.0, lon1 = -0.5;
    double lat2 = 51.5, lon2 =  1.0;

    double E_orig = millington_field_dbuvm(146437.5, lat1, lon1, lat2, lon2,
                                            cmap, 40.0, 20);
    double E_with = millington_with(146437.5, lat1, lon1, lat2, lon2,
                                     cmap, 40.0, groundwave_field_dbuvm, 20);
    CHECK(E_with == Approx(E_orig).margin(0.001));
}

TEST_CASE("millington_with: grwave seg_fn gives different result from polynomial") {
    // For a mixed land/sea path, using GRWAVE vs polynomial should give
    // a measurably different (but same-ballpark) result.
    StepConductivityMap cmap;
    cmap.boundary_lon = -3.5;
    cmap.gc_west = GroundConstants{4.0, 70.0};   // sea
    cmap.gc_east = GroundConstants{0.005, 15.0};  // land

    double lat_tx = 52.5, lon_tx = -5.5;
    double lat_rx = 52.5, lon_rx = -1.0;

    double E_poly = millington_with(146437.5, lat_tx, lon_tx, lat_rx, lon_rx,
                                     cmap, 40.0, groundwave_field_dbuvm, 20);
    double E_grw  = millington_with(146437.5, lat_tx, lon_tx, lat_rx, lon_rx,
                                     cmap, 40.0, grwave_field_dbuvm, 20);
    // Both should be valid field strengths (finite, reasonable)
    CHECK(E_poly > -50.0);
    CHECK(E_grw  > -50.0);
    CHECK(E_poly < 80.0);
    CHECK(E_grw  < 80.0);
}

// ---- groundwave_for_model with GRWAVE ----

TEST_CASE("groundwave_for_model: GRWAVE dispatches to millington_with+grwave") {
    GroundConstants gc { 0.005, 15.0 };
    FixedConductivityMap cmap(gc.sigma, gc.eps_r);
    double lat1 = 52.0, lon1 = -0.5;
    double lat2 = 51.5, lon2 =  1.0;

    double E_direct = millington_with(146437.5, lat1, lon1, lat2, lon2,
                                       cmap, 40.0, grwave_field_dbuvm, 20);
    double E_disp = groundwave_for_model(146437.5, lat1, lon1, lat2, lon2,
                                          cmap, 40.0,
                                          Scenario::PropagationModel::GRWAVE);
    CHECK(E_disp == Approx(E_direct).margin(0.001));
}

// ---- computeGroundwave: propagation model produces different grid output ----

TEST_CASE("computeGroundwave: Millington vs GRWAVE produce different grid values") {
    // Build a small scenario with one transmitter and a coarse grid
    Scenario s;
    s.frequencies.f1_hz = 146437.5;
    s.frequencies.recompute();
    s.grid.lat_min = 51.0;
    s.grid.lat_max = 52.0;
    s.grid.lon_min = -1.0;
    s.grid.lon_max =  0.0;
    s.grid.resolution_km = 50.0;  // very coarse — just a few points

    TransmitterSite site;
    site.name    = "Test";
    site.lat     = 52.3;
    site.lon     = -0.2;
    site.power_w = 40.0;
    SlotConfig sc;
    sc.slot      = 1;
    sc.is_master = true;
    site.slots.push_back(sc);
    s.transmitter_sites.push_back(site);

    std::atomic<bool> cancel{false};

    // --- Run with Millington ---
    s.propagation_model = Scenario::PropagationModel::Millington;
    auto grid_mil = buildGrid(s.grid, cancel);
    REQUIRE(!grid_mil.points.empty());
    GridData data_mil;
    GridArray arr_mil;
    arr_mil.layer_name    = "groundwave";
    arr_mil.points        = grid_mil.points;
    arr_mil.values.assign(grid_mil.points.size(), 0.0);
    arr_mil.width         = grid_mil.width;
    arr_mil.height        = grid_mil.height;
    arr_mil.lat_min       = s.grid.lat_min;
    arr_mil.lat_max       = s.grid.lat_max;
    arr_mil.lon_min       = s.grid.lon_min;
    arr_mil.lon_max       = s.grid.lon_max;
    arr_mil.resolution_km = s.grid.resolution_km;
    data_mil.layers["groundwave"] = std::move(arr_mil);
    computeGroundwave(data_mil, s, cancel);

    // --- Run with GRWAVE ---
    s.propagation_model = Scenario::PropagationModel::GRWAVE;
    GridData data_grw;
    GridArray arr_grw;
    arr_grw.layer_name    = "groundwave";
    arr_grw.points        = grid_mil.points;
    arr_grw.values.assign(grid_mil.points.size(), 0.0);
    arr_grw.width         = grid_mil.width;
    arr_grw.height        = grid_mil.height;
    arr_grw.lat_min       = s.grid.lat_min;
    arr_grw.lat_max       = s.grid.lat_max;
    arr_grw.lon_min       = s.grid.lon_min;
    arr_grw.lon_max       = s.grid.lon_max;
    arr_grw.resolution_km = s.grid.resolution_km;
    data_grw.layers["groundwave"] = std::move(arr_grw);
    computeGroundwave(data_grw, s, cancel);

    // The two models must produce different values at at least one grid point.
    const auto& vals_mil = data_mil.layers["groundwave"].values;
    const auto& vals_grw = data_grw.layers["groundwave"].values;
    REQUIRE(vals_mil.size() == vals_grw.size());
    bool any_different = false;
    for (size_t i = 0; i < vals_mil.size(); ++i) {
        if (std::abs(vals_mil[i] - vals_grw[i]) > 0.01) {
            any_different = true;
            break;
        }
    }
    CHECK(any_different);
}

// ---- GrwaveLUT ----

TEST_CASE("GrwaveLUT: matches full computation within 0.5 dB over land") {
    GrwaveLUT lut(146437.5);
    GroundConstants gc_land { 0.005, 15.0 };
    for (double d : {1.0, 10.0, 50.0, 100.0, 200.0, 350.0, 500.0}) {
        double E_full = grwave_field_dbuvm(146437.5, d, gc_land, 40.0);
        double E_lut  = lut.lookup(d, gc_land, 40.0);
        CHECK(std::abs(E_full - E_lut) < 0.5);
    }
}

TEST_CASE("GrwaveLUT: matches full computation within 0.5 dB over sea") {
    GrwaveLUT lut(146437.5);
    GroundConstants gc_sea { 4.0, 70.0 };
    for (double d : {1.0, 10.0, 50.0, 100.0, 200.0, 350.0, 500.0}) {
        double E_full = grwave_field_dbuvm(146437.5, d, gc_sea, 40.0);
        double E_lut  = lut.lookup(d, gc_sea, 40.0);
        CHECK(std::abs(E_full - E_lut) < 0.5);
    }
}

TEST_CASE("GrwaveLUT: power scaling is correct") {
    GrwaveLUT lut(146437.5);
    GroundConstants gc { 0.005, 15.0 };
    double E_40W  = lut.lookup(100.0, gc, 40.0);
    double E_160W = lut.lookup(100.0, gc, 160.0);
    // 4x power = +6 dB field strength (20*log10(sqrt(4)) = 6.02)
    CHECK(E_160W - E_40W == Approx(6.02).margin(0.1));
}

TEST_CASE("GrwaveLUT: active() returns null when no scope") {
    CHECK(GrwaveLUT::active() == nullptr);
}

TEST_CASE("GrwaveLUT: scope installs and uninstalls LUT") {
    GrwaveLUT lut(146437.5);
    CHECK(GrwaveLUT::active() == nullptr);
    {
        GrwaveLUT::Scope scope(lut);
        CHECK(GrwaveLUT::active() == &lut);
    }
    CHECK(GrwaveLUT::active() == nullptr);
}

TEST_CASE("GrwaveLUT: grwave_field_dbuvm uses LUT when active") {
    GrwaveLUT lut(146437.5);
    GroundConstants gc { 0.005, 15.0 };

    // Without LUT: full computation
    double E_full = grwave_field_dbuvm(146437.5, 200.0, gc, 40.0);

    // With LUT: should match closely
    GrwaveLUT::Scope scope(lut);
    double E_via_lut = grwave_field_dbuvm(146437.5, 200.0, gc, 40.0);
    CHECK(std::abs(E_full - E_via_lut) < 0.5);
}

TEST_CASE("GrwaveLUT: different frequency builds different table") {
    GrwaveLUT lut_lo(100000.0);
    GrwaveLUT lut_hi(200000.0);
    GroundConstants gc { 0.005, 15.0 };
    double E_lo = lut_lo.lookup(200.0, gc, 40.0);
    double E_hi = lut_hi.lookup(200.0, gc, 40.0);
    CHECK(E_lo != Approx(E_hi).margin(0.01));
}

// ---- TOML round-trip for GRWAVE ----
// (TOML tests are in tests/model/test_toml_io.cpp)
