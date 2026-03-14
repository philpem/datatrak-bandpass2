#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "almanac/ZonePatterns.h"
#include "almanac/AlmanacExport.h"
#include "model/Scenario.h"
#include "engine/grid.h"
#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>

using namespace bp;
using namespace bp::almanac;

// Scenario with high-power transmitters surrounding Zone 9 so SNR is positive.
// Zone 9 centroid is ~51.975N, -0.125E.  Transmitters are placed close to ensure
// multiple stations are above SNR threshold.
static Scenario make_zp_scenario() {
    Scenario s;
    s.name = "ZpTest";
    s.frequencies.f1_hz = 146437.5;
    s.frequencies.f2_hz = 131250.0;
    s.frequencies.recompute();

    s.receiver.noise_floor_dbuvpm = 14.0;
    s.receiver.vehicle_noise_dbuvpm = 27.0;
    s.receiver.max_range_km = 500.0;
    s.receiver.min_stations = 2;

    // Use 4000 W (high ERP) so groundwave > noise floor at 40-80 km distances
    auto make_site = [](const std::string& name, double lat, double lon,
                        int slot, bool is_master, double power_w) {
        TransmitterSite site;
        site.name = name; site.lat = lat; site.lon = lon; site.power_w = power_w;
        SlotConfig sc; sc.slot = slot; sc.is_master = is_master;
        site.slots.push_back(sc);
        return site;
    };
    s.transmitter_sites = {
        make_site("NearZone9a", 52.5, -0.5, 1, true,  4000.0),
        make_site("NearZone9b", 51.5,  0.5, 2, false, 4000.0),
        make_site("NearZone9c", 51.5, -1.0, 3, false, 4000.0),
    };
    return s;
}

// Cross-platform temp path helper
static std::string temp_path(const std::string& filename) {
    return (std::filesystem::temp_directory_path() / filename).string();
}

// Write a minimal GeoJSON with 2 zones to a temp file
static std::string write_minimal_geojson() {
    std::string path = temp_path("test_zones.geojson");
    std::ofstream f(path);
    f << R"({
  "type": "FeatureCollection",
  "features": [
    {
      "type": "Feature",
      "properties": { "zone_id": 9, "name": "Zone 9 - London" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[
          [-1.25, 51.55], [1.00, 51.55], [1.00, 52.40], [-1.25, 52.40], [-1.25, 51.55]
        ]]
      }
    },
    {
      "type": "Feature",
      "properties": { "zone_id": 32, "name": "Zone 32 - N Scotland" },
      "geometry": {
        "type": "Polygon",
        "coordinates": [[
          [-3.00, 58.35], [-1.00, 58.35], [-1.00, 59.20], [-3.00, 59.20], [-3.00, 58.35]
        ]]
      }
    }
  ]
})";
    return path;
}

TEST_CASE("ZonePatterns: empty path returns no results") {
    auto s = make_zp_scenario();
    GridData gd;
    auto res = compute_zone_patterns(s, gd, "");
    CHECK(res.empty());
}

TEST_CASE("ZonePatterns: nonexistent file returns no results") {
    auto s = make_zp_scenario();
    GridData gd;
    auto res = compute_zone_patterns(s, gd, temp_path("does_not_exist_xyz.geojson"));
    CHECK(res.empty());
}

TEST_CASE("ZonePatterns: loads zones from minimal GeoJSON") {
    auto s  = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto res = compute_zone_patterns(s, gd, path);
    CHECK(res.size() == 2);
    // Zones should be sorted by zone_id
    if (res.size() == 2) {
        CHECK(res[0].zone_id == 9);
        CHECK(res[1].zone_id == 32);
    }
}

TEST_CASE("ZonePatterns: zone name is preserved") {
    auto s    = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto res = compute_zone_patterns(s, gd, path);
    REQUIRE(res.size() >= 1);
    CHECK(res[0].zone_name == "Zone 9 - London");
}

TEST_CASE("ZonePatterns: centroid is inside zone bounding box") {
    auto s    = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto res = compute_zone_patterns(s, gd, path);
    REQUIRE(res.size() >= 1);
    // Zone 9: lon [-1.25, 1.00], lat [51.55, 52.40]
    CHECK(res[0].centroid_lon >= -1.25);
    CHECK(res[0].centroid_lon <= 1.00);
    CHECK(res[0].centroid_lat >= 51.55);
    CHECK(res[0].centroid_lat <= 52.40);
}

TEST_CASE("ZonePatterns: central UK zone with 3 transmitters has viable pairs") {
    auto s    = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto res = compute_zone_patterns(s, gd, path);
    // Zone 9 is in central England — should have at least 1 viable pair
    REQUIRE(res.size() >= 1);
    CHECK(res[0].viable_count >= 1);
    // set1 should not be "0,0" if there's a viable pair
    if (res[0].viable_count >= 1) {
        CHECK(res[0].set1 != "0,0");
    }
}

TEST_CASE("ZonePatterns: remote zone (N Scotland) may have fewer viable pairs") {
    auto s    = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto res = compute_zone_patterns(s, gd, path);
    REQUIRE(res.size() >= 2);
    // Zone 32 (N Scotland) is far from all 3 transmitters — may be a gap
    // Just verify the structure is correct
    CHECK(res[1].zone_id == 32);
    CHECK(res[1].viable_count >= 0);
    // is_gap should be true if fewer than 3 viable pairs
    CHECK(res[1].is_gap == (res[1].viable_count < 3));
}

TEST_CASE("ZonePatterns: set4 is 0,0 when fewer than 4 viable pairs") {
    auto s    = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto res = compute_zone_patterns(s, gd, path);
    REQUIRE(!res.empty());
    for (const auto& z : res) {
        if (z.viable_count < 4) {
            CHECK(z.set4 == "0,0");
        }
    }
}

TEST_CASE("generate_zp: produces Zp commands") {
    auto s    = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto zones = compute_zone_patterns(s, gd, path);
    auto text  = generate_zp(zones);
    // Must contain at least the Zp header comment
    CHECK(text.find("Zp commands") != std::string::npos);
}

TEST_CASE("generate_zp: gap zones get a comment") {
    auto s    = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto zones = compute_zone_patterns(s, gd, path);
    // Inject an artificial gap zone
    ZoneResult gap;
    gap.zone_id = 99; gap.zone_name = "Test Gap";
    gap.viable_count = 1; gap.is_gap = true;
    gap.set1 = "1,2";
    zones.push_back(gap);
    auto text = generate_zp(zones);
    CHECK(text.find("COVERAGE GAP") != std::string::npos);
}

TEST_CASE("generate_zp: Zp commands have correct format") {
    std::vector<ZoneResult> zones;
    ZoneResult z;
    z.zone_id = 5; z.zone_name = "Test";
    z.viable_count = 3; z.is_gap = false;
    z.set1 = "1,2"; z.set2 = "1,3"; z.set3 = "2,3"; z.set4 = "0,0";
    zones.push_back(z);
    auto text = generate_zp(zones);
    CHECK(text.find("Zp 5 1 1,2") != std::string::npos);
    CHECK(text.find("Zp 5 2 1,3") != std::string::npos);
    CHECK(text.find("Zp 5 3 2,3") != std::string::npos);
    // set4 is "0,0" with viable_count=3 — should NOT be emitted
    CHECK(text.find("Zp 5 4") == std::string::npos);
}

TEST_CASE("generate_almanac: includes Zp block when geojson_path given") {
    auto s    = make_zp_scenario();
    auto path = write_minimal_geojson();
    GridData gd;
    auto text = generate_almanac(s, gd, FirmwareFormat::V7, path);
    // Should contain Sg, Stxs, and Zp blocks
    CHECK(text.find("Sg ") != std::string::npos);
    CHECK(text.find("Stxs ") != std::string::npos);
    CHECK(text.find("Zp commands") != std::string::npos);
}

TEST_CASE("generate_almanac: omits Zp block when no geojson_path") {
    auto s = make_zp_scenario();
    GridData gd;
    auto text = generate_almanac(s, gd, FirmwareFormat::V7, "");
    CHECK(text.find("Zp commands") == std::string::npos);
}
