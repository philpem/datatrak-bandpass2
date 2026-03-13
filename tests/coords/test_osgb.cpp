#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "coords/Osgb.h"
#include "coords/NationalGrid.h"

using namespace bp;
using namespace Catch::Matchers;

// OS benchmark point: Caister Water Tower
// WGS84: 52.65757°N, 1.71792°E
// OSGB36 NG: 651409 E, 313177 N
// Source: OS OSTN15/OSGM15 test dataset

TEST_CASE("WGS84 to OSGB36 Helmert transform", "[osgb]") {
    LatLon wgs84 = { 52.65757, 1.71792 };
    LatLon osgb36 = osgb::wgs84_to_osgb36(wgs84);
    // Helmert is ±5m, so the lat/lon will be close but not exact NG easting/northing
    // Check that the result is close to WGS84 (the shift is small)
    REQUIRE_THAT(osgb36.lat, WithinAbs(52.65, 0.05));
    REQUIRE_THAT(osgb36.lon, WithinAbs(1.71,  0.05));
}

TEST_CASE("OSGB36 LatLon to National Grid easting/northing", "[national_grid]") {
    // Use a known point near Greenwich
    // OSGB36 lat=51.4778, lon=-0.0014 → TQ area
    LatLon osgb36 = { 51.4778, -0.0014 };
    EastNorth en = national_grid::latlon_to_en(osgb36);
    // Rough check: should be in TQ area (E ~530000-540000, N ~175000-185000)
    REQUIRE(en.easting  > 520000.0);
    REQUIRE(en.easting  < 560000.0);
    REQUIRE(en.northing > 170000.0);
    REQUIRE(en.northing < 200000.0);
}

TEST_CASE("National Grid round-trip: latlon → EN → latlon", "[national_grid]") {
    LatLon original = { 52.3247, -0.1848 };  // Huntingdon area (OSGB36 approx)
    EastNorth en  = national_grid::latlon_to_en(original);
    LatLon   back = national_grid::en_to_latlon(en);
    REQUIRE_THAT(back.lat, WithinAbs(original.lat, 1e-5));
    REQUIRE_THAT(back.lon, WithinAbs(original.lon, 1e-5));
}

TEST_CASE("Grid reference formatting", "[national_grid]") {
    // TL 271 707 → roughly E=527100, N=270700 (Huntingdon area)
    EastNorth en = { 527100.0, 270700.0 };
    std::string ref = national_grid::en_to_gridref(en, 6);
    REQUIRE(ref.size() >= 8);  // at least "TL271707"
    // Should start with two letters
    REQUIRE(std::isalpha(ref[0]));
    REQUIRE(std::isalpha(ref[1]));
}

TEST_CASE("Grid reference parse round-trip", "[national_grid]") {
    EastNorth en_orig = { 513054.0, 262453.0 };
    std::string ref   = national_grid::en_to_gridref(en_orig, 6);
    EastNorth en_back = national_grid::gridref_to_en(ref);
    // 6-digit ref has 100m precision
    REQUIRE_THAT(en_back.easting,  WithinAbs(en_orig.easting,  200.0));
    REQUIRE_THAT(en_back.northing, WithinAbs(en_orig.northing, 200.0));
}

TEST_CASE("WGS84 round-trip through OSGB36 Helmert", "[osgb]") {
    LatLon wgs84 = { 51.5074, -0.1278 };  // London
    LatLon osgb36 = osgb::wgs84_to_osgb36(wgs84);
    LatLon back   = osgb::osgb36_to_wgs84(osgb36);
    REQUIRE_THAT(back.lat, WithinAbs(wgs84.lat, 0.001));
    REQUIRE_THAT(back.lon, WithinAbs(wgs84.lon, 0.001));
}

TEST_CASE("parse_coordinate: WGS84 decimal degrees", "[national_grid]") {
    LatLon result = national_grid::parse_coordinate("52.3247 -0.1848");
    REQUIRE_THAT(result.lat, WithinAbs(52.3247, 1e-4));
    REQUIRE_THAT(result.lon, WithinAbs(-0.1848, 1e-4));
}

TEST_CASE("parse_coordinate: grid reference format", "[national_grid]") {
    // "TL 271 707" → Huntingdon area; parse_coordinate detects leading letter → grid ref
    LatLon result = national_grid::parse_coordinate("TL 271 707");
    REQUIRE_THAT(result.lat, WithinAbs(52.3, 0.3));
    REQUIRE_THAT(result.lon, WithinAbs(-0.2, 0.5));
}

TEST_CASE("parse_coordinate: raw easting/northing", "[national_grid]") {
    LatLon result = national_grid::parse_coordinate("513054 262453");
    // Should return WGS84 approximation of Huntingdon area
    REQUIRE_THAT(result.lat, WithinAbs(52.3, 0.1));
    REQUIRE_THAT(result.lon, WithinAbs(-0.2, 0.5));
}
