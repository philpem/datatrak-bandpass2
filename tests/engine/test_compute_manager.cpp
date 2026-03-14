#include <catch2/catch_test_macros.hpp>
#include "engine/grid.h"
#include "model/Scenario.h"
#include <atomic>

using namespace bp;

// Note: ComputeManager requires wxWidgets event handling, so we test
// the lower-level grid-building functions here without wx dependency.

TEST_CASE("buildGrid returns correct point count", "[grid]") {
    GridDef def;
    def.lat_min      = 51.0;
    def.lat_max      = 52.0;
    def.lon_min      = -1.0;
    def.lon_max      =  0.0;
    def.resolution_km = 10.0;

    std::atomic<bool> cancel{false};
    auto grid = buildGrid(def, cancel);
    // Should have a reasonable number of points for a 1°×1° area at 10 km resolution
    REQUIRE(grid.points.size() > 10);
    REQUIRE(grid.points.size() < 1000);
    REQUIRE(grid.width > 0);
    REQUIRE(grid.height > 0);
    REQUIRE(grid.width * grid.height == (int)grid.points.size());
}

TEST_CASE("buildGrid points have valid lat/lon", "[grid]") {
    GridDef def;
    def.lat_min      = 50.0;
    def.lat_max      = 51.0;
    def.lon_min      = -2.0;
    def.lon_max      = -1.0;
    def.resolution_km = 50.0;

    std::atomic<bool> cancel{false};
    auto grid = buildGrid(def, cancel);
    REQUIRE(!grid.points.empty());
    for (const auto& p : grid.points) {
        REQUIRE(p.lat >= 49.9);
        REQUIRE(p.lat <= 51.1);
        REQUIRE(p.lon >= -2.1);
        REQUIRE(p.lon <= -0.9);
    }
}

TEST_CASE("GridArray to_geojson produces valid JSON structure", "[grid]") {
    GridArray arr;
    arr.layer_name = "test";
    GridPoint p;
    p.lat = 52.0; p.lon = -0.1; p.easting = 500000; p.northing = 250000;
    arr.points.push_back(p);
    arr.values.push_back(42.0);
    arr.lat_min = 51.5; arr.lat_max = 52.5;
    arr.lon_min = -0.5; arr.lon_max =  0.5;
    arr.resolution_km = 10.0;

    std::string geojson = arr.to_geojson();
    REQUIRE(!geojson.empty());
    REQUIRE(geojson.find("FeatureCollection") != std::string::npos);
    REQUIRE(geojson.find("features") != std::string::npos);
}

TEST_CASE("Empty GridArray to_geojson is safe", "[grid]") {
    GridArray arr;
    arr.layer_name = "empty";
    std::string geojson = arr.to_geojson();
    REQUIRE(geojson.find("FeatureCollection") != std::string::npos);
    REQUIRE(geojson.find("\"features\":[]") != std::string::npos);
}

TEST_CASE("buildGrid: finer resolution produces more grid points", "[grid]") {
    GridDef def;
    def.lat_min = 49.5; def.lat_max = 59.5;
    def.lon_min = -7.0; def.lon_max =  2.5;

    std::atomic<bool> cancel{false};

    def.resolution_km = 50.0;
    auto coarse = buildGrid(def, cancel);

    def.resolution_km = 10.0;
    auto medium = buildGrid(def, cancel);

    def.resolution_km = 5.0;
    auto fine = buildGrid(def, cancel);

    // Finer resolution must produce more points.
    REQUIRE(coarse.points.size() < medium.points.size());
    REQUIRE(medium.points.size() < fine.points.size());

    // Point count scales roughly as (1/res)^2: 10km should have ~25x more
    // points than 50km and ~4x fewer than 5km.
    REQUIRE(medium.points.size() > coarse.points.size() * 10);
    REQUIRE(fine.points.size()   > medium.points.size() * 2);

    // width * height must equal total point count for all resolutions.
    REQUIRE(coarse.width * coarse.height == (int)coarse.points.size());
    REQUIRE(medium.width * medium.height == (int)medium.points.size());
    REQUIRE(fine.width   * fine.height   == (int)fine.points.size());
}

TEST_CASE("buildGrid: zero resolution and zero max_points returns empty", "[grid]") {
    GridDef def;
    def.resolution_km = 0.0;
    def.max_points    = 0;   // both zero → nothing to build
    std::atomic<bool> cancel{false};
    auto grid = buildGrid(def, cancel);
    CHECK(grid.points.empty());
}

TEST_CASE("buildGrid: negative resolution with zero max_points returns empty", "[grid]") {
    GridDef def;
    def.resolution_km = -5.0;
    def.max_points    = 0;
    std::atomic<bool> cancel{false};
    auto grid = buildGrid(def, cancel);
    CHECK(grid.points.empty());
}

TEST_CASE("buildGrid: max_points limits grid size", "[grid]") {
    GridDef def;
    def.lat_min   = 49.5; def.lat_max = 59.5;
    def.lon_min   = -7.0; def.lon_max =  2.5;
    def.max_points = 100;
    def.resolution_km = 0.0;
    std::atomic<bool> cancel{false};
    auto grid = buildGrid(def, cancel);
    REQUIRE(!grid.points.empty());
    // With max_points=100 the grid should stay near that size
    CHECK((int)grid.points.size() <= 200);   // allow some rounding
    CHECK(grid.resolution_km > 0.0);
}

TEST_CASE("buildGrid: cancel flag aborts mid-grid", "[grid]") {
    GridDef def;
    def.lat_min = 49.5; def.lat_max = 59.5;
    def.lon_min = -7.0; def.lon_max =  2.5;
    def.resolution_km = 10.0;
    std::atomic<bool> cancel{false};
    // Pre-cancel: should return empty immediately
    cancel.store(true);
    auto grid = buildGrid(def, cancel);
    CHECK(grid.points.empty());
}

TEST_CASE("Frequencies default values", "[scenario]") {
    Frequencies f;
    f.recompute();
    REQUIRE(f.f1_hz == 146437.5);
    REQUIRE(f.f2_hz == 131250.0);
    REQUIRE(f.lane_width_f1_m > 2000.0);
    REQUIRE(f.lane_width_f2_m > 2000.0);
    REQUIRE(f.lane_width_f1_m < 2200.0);
    REQUIRE(f.lane_width_f2_m < 2400.0);
}

TEST_CASE("Frequencies out-of-range detection", "[scenario]") {
    Frequencies f;

    // Below lower limit (30 kHz)
    f.f1_hz = 25000.0;  f.f2_hz = 131250.0;
    CHECK_FALSE(f.is_valid_range());

    // Above upper limit (300 kHz)
    f.f1_hz = 350000.0;  f.f2_hz = 131250.0;
    CHECK_FALSE(f.is_valid_range());

    // F2 out of range
    f.f1_hz = 146437.5;  f.f2_hz = 29999.0;
    CHECK_FALSE(f.is_valid_range());

    // Exactly at lower bound — must be accepted
    f.f1_hz = 30000.0;  f.f2_hz = 30000.0;
    CHECK(f.is_valid_range());

    // Exactly at upper bound — must be accepted
    f.f1_hz = 300000.0;  f.f2_hz = 300000.0;
    CHECK(f.is_valid_range());

    // f1 == f2 at Datatrak standard — allowed (with warning, not error)
    f.f1_hz = 146437.5;  f.f2_hz = 146437.5;
    CHECK(f.is_valid_range());
}
