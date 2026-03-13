#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/groundwave.h"
#include "engine/noise.h"
#include "engine/skywave.h"

using namespace bp;
using Catch::Approx;

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
