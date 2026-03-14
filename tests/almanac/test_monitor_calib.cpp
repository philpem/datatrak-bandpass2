// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for MonitorCalib (P5-14 / P5-15)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "almanac/MonitorCalib.h"
#include "model/Scenario.h"
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace bp;
using namespace bp::almanac;

// Helper: write a temp file and return its path
static std::filesystem::path write_temp(const std::string& content) {
    auto path = std::filesystem::temp_directory_path()
              / ("bp_test_monitor_" + std::to_string(std::hash<std::string>{}(content)) + ".csv");
    std::ofstream f(path);
    f << content;
    return path;
}

// Helper: make a simple two-transmitter scenario
static Scenario make_scenario() {
    Scenario s;
    s.frequencies.f1_hz = 146437.5;
    s.frequencies.f2_hz = 131250.0;
    s.frequencies.recompute();

    Transmitter t1;
    t1.name = "Huntingdon"; t1.lat = 52.32; t1.lon = -0.18;
    t1.slot = 1; t1.is_master = true; t1.master_slot = 0; t1.power_w = 40.0;
    s.transmitters.push_back(t1);

    Transmitter t2;
    t2.name = "Selsey"; t2.lat = 50.73; t2.lon = -0.79;
    t2.slot = 2; t2.is_master = false; t2.master_slot = 1; t2.power_w = 40.0;
    s.transmitters.push_back(t2);

    return s;
}

// ---------------------------------------------------------------------------
// import_monitor_log
// ---------------------------------------------------------------------------
TEST_CASE("import_monitor_log: missing file throws") {
    REQUIRE_THROWS_AS(
        import_monitor_log("/nonexistent/path/file.csv"),
        std::runtime_error);
}

TEST_CASE("import_monitor_log: parses basic log") {
    auto path = write_temp(
        "# Monitor: Test Station\n"
        "# Lat: 52.3247\n"
        "# Lon: -0.1848\n"
        "2,1,12,9,7,11\n"
        "3,1,-5,-3,-8,-6\n");

    auto ms = import_monitor_log(path, "Default Name", 0.0, 0.0);
    REQUIRE(ms.name == "Test Station");
    REQUIRE(ms.lat  == Catch::Approx(52.3247));
    REQUIRE(ms.lon  == Catch::Approx(-0.1848));
    REQUIRE(ms.corrections.size() == 2);

    CHECK(ms.corrections[0].pattern    == "2,1");
    CHECK(ms.corrections[0].f1plus_ml  == 12);
    CHECK(ms.corrections[0].f1minus_ml ==  9);
    CHECK(ms.corrections[0].f2plus_ml  ==  7);
    CHECK(ms.corrections[0].f2minus_ml == 11);

    CHECK(ms.corrections[1].pattern    == "3,1");
    CHECK(ms.corrections[1].f1plus_ml  == -5);
}

TEST_CASE("import_monitor_log: blank lines and comments are ignored") {
    auto path = write_temp(
        "# header\n\n"
        "# another comment\n"
        "2,1,1,2,3,4\n\n");

    auto ms = import_monitor_log(path);
    REQUIRE(ms.corrections.size() == 1);
}

TEST_CASE("import_monitor_log: malformed line throws") {
    auto path = write_temp("2,1,10,20\n");  // only 4 fields → should be 6
    REQUIRE_THROWS_AS(import_monitor_log(path), std::runtime_error);
}

TEST_CASE("import_monitor_log: default station name from param") {
    auto path = write_temp("2,1,0,0,0,0\n");
    auto ms = import_monitor_log(path, "MyStation", 10.0, 20.0);
    CHECK(ms.name == "MyStation");
    CHECK(ms.lat  == Catch::Approx(10.0));
    CHECK(ms.lon  == Catch::Approx(20.0));
}

// ---------------------------------------------------------------------------
// apply_monitor_corrections
// ---------------------------------------------------------------------------
TEST_CASE("apply_monitor_corrections: no monitors returns original offsets") {
    Scenario s = make_scenario();
    PatternOffset po;
    po.pattern = "2,1"; po.f1plus_ml = 100; po.f1minus_ml = 90;
    po.f2plus_ml = 80; po.f2minus_ml = 70;
    s.pattern_offsets.push_back(po);

    auto result = apply_monitor_corrections(s);
    REQUIRE(result.size() == 1);
    CHECK(result[0].f1plus_ml == 100);
}

TEST_CASE("apply_monitor_corrections: single monitor applies delta") {
    Scenario s = make_scenario();
    // Existing po = 100 ml
    PatternOffset po;
    po.pattern = "2,1"; po.f1plus_ml = 100; po.f1minus_ml = 90;
    po.f2plus_ml = 80; po.f2minus_ml = 70;
    s.pattern_offsets.push_back(po);

    // Monitor says: observed offset is 12 ml higher than predicted
    MonitorStation ms;
    ms.name = "Mon1";
    MonitorStation::Correction c;
    c.pattern = "2,1"; c.f1plus_ml = 12; c.f1minus_ml = 9;
    c.f2plus_ml = 7; c.f2minus_ml = 11;
    ms.corrections.push_back(c);
    s.monitor_stations.push_back(ms);

    auto result = apply_monitor_corrections(s);
    REQUIRE(result.size() == 1);
    CHECK(result[0].f1plus_ml  == 100 + 12);
    CHECK(result[0].f1minus_ml ==  90 +  9);
    CHECK(result[0].f2plus_ml  ==  80 +  7);
    CHECK(result[0].f2minus_ml ==  70 + 11);
}

TEST_CASE("apply_monitor_corrections: two monitors averaged") {
    Scenario s = make_scenario();
    PatternOffset po;
    po.pattern = "2,1"; po.f1plus_ml = 0; po.f1minus_ml = 0;
    po.f2plus_ml = 0; po.f2minus_ml = 0;
    s.pattern_offsets.push_back(po);

    // Monitor 1 says +10, monitor 2 says +20 → mean = +15
    for (int delta : {10, 20}) {
        MonitorStation ms;
        ms.name = "Mon" + std::to_string(delta);
        MonitorStation::Correction c;
        c.pattern = "2,1";
        c.f1plus_ml = delta; c.f1minus_ml = delta;
        c.f2plus_ml = delta; c.f2minus_ml = delta;
        ms.corrections.push_back(c);
        s.monitor_stations.push_back(ms);
    }

    auto result = apply_monitor_corrections(s);
    REQUIRE(result.size() == 1);
    CHECK(result[0].f1plus_ml == 15);
}

TEST_CASE("apply_monitor_corrections: creates new pattern if not in offsets") {
    Scenario s = make_scenario();
    // No existing pattern_offsets
    MonitorStation ms;
    ms.name = "Mon1";
    MonitorStation::Correction c;
    c.pattern = "2,1"; c.f1plus_ml = 50; c.f1minus_ml = 40;
    c.f2plus_ml = 30; c.f2minus_ml = 20;
    ms.corrections.push_back(c);
    s.monitor_stations.push_back(ms);

    auto result = apply_monitor_corrections(s);
    REQUIRE(result.size() == 1);
    CHECK(result[0].pattern   == "2,1");
    CHECK(result[0].f1plus_ml == 50);
}

// ---------------------------------------------------------------------------
// check_consistency
// ---------------------------------------------------------------------------
TEST_CASE("check_consistency: single monitor returns early") {
    Scenario s = make_scenario();
    MonitorStation ms;
    ms.name = "OnlyMonitor";
    MonitorStation::Correction c;
    c.pattern = "2,1"; c.f1plus_ml = 10; c.f1minus_ml = 9;
    c.f2plus_ml = 8; c.f2minus_ml = 7;
    ms.corrections.push_back(c);
    s.monitor_stations.push_back(ms);

    auto report = check_consistency(s);
    CHECK(report.inconsistencies.empty());
    CHECK_THAT(report.summary, Catch::Matchers::ContainsSubstring("Only one monitor"));
}

TEST_CASE("check_consistency: two monitors agreeing") {
    Scenario s = make_scenario();
    for (int i = 0; i < 2; ++i) {
        MonitorStation ms;
        ms.name = "Mon" + std::to_string(i);
        MonitorStation::Correction c;
        c.pattern = "2,1"; c.f1plus_ml = 10; c.f1minus_ml = 9;
        c.f2plus_ml = 8; c.f2minus_ml = 7;
        ms.corrections.push_back(c);
        s.monitor_stations.push_back(ms);
    }

    auto report = check_consistency(s, 20);
    CHECK(report.inconsistencies.empty());
    CHECK_THAT(report.summary, Catch::Matchers::ContainsSubstring("passed"));
}

TEST_CASE("check_consistency: two monitors disagreeing by more than threshold") {
    Scenario s = make_scenario();

    MonitorStation ms1; ms1.name = "Mon1";
    MonitorStation::Correction c1;
    c1.pattern = "2,1"; c1.f1plus_ml = 10; c1.f1minus_ml = 9;
    c1.f2plus_ml = 8; c1.f2minus_ml = 7;
    ms1.corrections.push_back(c1);
    s.monitor_stations.push_back(ms1);

    MonitorStation ms2; ms2.name = "Mon2";
    MonitorStation::Correction c2;
    c2.pattern = "2,1"; c2.f1plus_ml = 50; c2.f1minus_ml = 9;  // 40 ml difference
    c2.f2plus_ml = 8; c2.f2minus_ml = 7;
    ms2.corrections.push_back(c2);
    s.monitor_stations.push_back(ms2);

    auto report = check_consistency(s, 20);
    REQUIRE(report.inconsistencies.size() == 1);
    CHECK(report.inconsistencies[0].pattern == "2,1");
    CHECK(report.inconsistencies[0].max_delta_ml == 40);
}

TEST_CASE("check_consistency: summary contains monitor count") {
    Scenario s = make_scenario();
    for (int i = 0; i < 3; ++i) {
        MonitorStation ms;
        ms.name = "Mon" + std::to_string(i);
        MonitorStation::Correction c;
        c.pattern = "2,1"; c.f1plus_ml = i * 5; c.f1minus_ml = 0;
        c.f2plus_ml = 0; c.f2minus_ml = 0;
        ms.corrections.push_back(c);
        s.monitor_stations.push_back(ms);
    }

    auto report = check_consistency(s, 20);
    CHECK_THAT(report.summary, Catch::Matchers::ContainsSubstring("3"));
}
