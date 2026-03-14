#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "engine/snr.h"
#include "engine/whdop.h"
#include <cmath>

using namespace bp;
using Catch::Approx;

// ---- SNR ----

TEST_CASE("snr: stronger signal gives higher SNR") {
    double snr1 = compute_snr_db(30.0, 0.0, 10.0);  // E_gw=30, noise floor
    double snr2 = compute_snr_db(20.0, 0.0, 10.0);
    CHECK(snr1 > snr2);
}

TEST_CASE("snr: higher noise gives lower SNR") {
    double snr1 = compute_snr_db(25.0, 0.0, 10.0);
    double snr2 = compute_snr_db(25.0, 15.0, 10.0);
    CHECK(snr1 > snr2);
}

TEST_CASE("gdr: no skywave gives same as SNR approximately") {
    // With no skywave (-200 dBuV/m), GDR should be close to SNR
    double snr = compute_snr_db(25.0, 0.0, 10.0);
    double gdr = compute_gdr_db(25.0, -200.0, 0.0, 10.0);
    CHECK(std::abs(gdr - snr) < 0.5);
}

TEST_CASE("gdr: skywave degrades GDR") {
    double gdr_nosky = compute_gdr_db(25.0, -200.0, 0.0, 10.0);
    double gdr_sky   = compute_gdr_db(25.0,   20.0, 0.0, 10.0);
    CHECK(gdr_nosky > gdr_sky);
}

TEST_CASE("sgr: negative when groundwave > skywave") {
    double sgr = compute_sgr_db(30.0, 20.0);
    CHECK(sgr < 0.0);
}

// ---- phase_uncertainty_ml ----

TEST_CASE("phase_uncertainty: higher SNR gives lower uncertainty") {
    double s1 = phase_uncertainty_ml(30.0, Frequencies{});
    double s2 = phase_uncertainty_ml(10.0, Frequencies{});
    CHECK(s1 < s2);
}

TEST_CASE("phase_uncertainty: poor SNR caps at 500 ml") {
    double s = phase_uncertainty_ml(-50.0, Frequencies{});
    CHECK(s == Approx(500.0).margin(1.0));
}

TEST_CASE("phase_uncertainty: good SNR (30 dB) is sub-millilane") {
    double s = phase_uncertainty_ml(30.0, Frequencies{});
    CHECK(s < 10.0);  // < 10 ml for 30 dB SNR
}

// ---- compute_whdop ----

TEST_CASE("whdop: fewer than min_stations returns -infinity") {
    std::vector<StationGeometry> stations;
    StationGeometry s;
    s.usable = true; s.snr_db = 20.0; s.dist_km = 100.0; s.azimuth_deg = 0.0;
    stations.push_back(s);
    std::vector<int> sel;
    double w = compute_whdop(stations, 4, 500.0, sel);
    CHECK(std::isinf(w));
    CHECK(w < 0.0);
}

TEST_CASE("whdop: ideal geometry (4 stations at 90 deg spacing) gives low WHDOP") {
    std::vector<StationGeometry> stations;
    for (int i = 0; i < 4; ++i) {
        StationGeometry s;
        s.usable = true; s.snr_db = 20.0; s.dist_km = 150.0;
        s.azimuth_deg = i * 90.0;
        s.sigma_phi_ml = 5.0;
        stations.push_back(s);
    }
    std::vector<int> sel;
    double w = compute_whdop(stations, 4, 500.0, sel);
    CHECK(w < 5.0);
    CHECK(sel.size() == 4);
}

TEST_CASE("whdop: poor geometry (collinear stations) gives high WHDOP") {
    std::vector<StationGeometry> stations;
    for (int i = 0; i < 4; ++i) {
        StationGeometry s;
        s.usable = true; s.snr_db = 20.0; s.dist_km = 150.0;
        s.azimuth_deg = i * 2.0;  // all nearly north
        s.sigma_phi_ml = 5.0;
        stations.push_back(s);
    }
    std::vector<int> sel;
    double w = compute_whdop(stations, 4, 500.0, sel);
    CHECK(w > 5.0);
}

TEST_CASE("whdop: stations beyond max range are excluded") {
    std::vector<StationGeometry> stations;
    for (int i = 0; i < 4; ++i) {
        StationGeometry s;
        s.usable = true; s.snr_db = 20.0; s.dist_km = 600.0;  // beyond 500km
        s.azimuth_deg = i * 90.0;
        stations.push_back(s);
    }
    std::vector<int> sel;
    double w = compute_whdop(stations, 4, 500.0, sel);
    CHECK(std::isinf(w));
    CHECK(w < 0.0);
}
