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
    auto pts = buildGrid(def, cancel);
    // Should have a reasonable number of points for a 1°×1° area at 10 km resolution
    REQUIRE(pts.size() > 10);
    REQUIRE(pts.size() < 1000);
}

TEST_CASE("buildGrid points have valid lat/lon", "[grid]") {
    GridDef def;
    def.lat_min      = 50.0;
    def.lat_max      = 51.0;
    def.lon_min      = -2.0;
    def.lon_max      = -1.0;
    def.resolution_km = 50.0;

    std::atomic<bool> cancel{false};
    auto pts = buildGrid(def, cancel);
    REQUIRE(!pts.empty());
    for (const auto& p : pts) {
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
    f.f1_hz = 25000.0;   // below 30 kHz
    REQUIRE(f.f1_hz < 30000.0);

    f.f1_hz = 350000.0;  // above 300 kHz
    REQUIRE(f.f1_hz > 300000.0);
}
