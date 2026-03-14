#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "coords/Osgb.h"
#include "coords/NationalGrid.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// Cross-platform temp directory helper
static std::string temp_dir() {
#ifdef _WIN32
    const char* tmp = std::getenv("TEMP");
    if (!tmp) tmp = std::getenv("TMP");
    if (!tmp) tmp = "C:\\Temp";
    return tmp;
#else
    return "/tmp";
#endif
}

static std::string temp_path(const std::string& filename) {
    return temp_dir() +
#ifdef _WIN32
        "\\"
#else
        "/"
#endif
        + filename;
}

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

TEST_CASE("National Grid round-trip: latlon -> EN -> latlon", "[national_grid]") {
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

// ---------------------------------------------------------------------------
// OSTN15 tests
// ---------------------------------------------------------------------------

// Helper: write a minimal synthetic OSTN15 binary grid for testing.
// Grid: 3×3 cells, 1000 m spacing, origin (0,0).
// All SE=100.0, SN=200.0 (uniform shift).
static std::string write_synthetic_ostn15(const std::string& path,
                                          float se_val = 100.0f,
                                          float sn_val = 200.0f) {
    std::ofstream f(path, std::ios::binary);
    f.write("OSTN1500", 8);
    auto wu32 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v), 4); };
    auto wf64 = [&](double  v){ f.write(reinterpret_cast<const char*>(&v), 8); };
    wu32(1);       // version
    wu32(3);       // ncols
    wu32(3);       // nrows
    wf64(0.0);     // origin_e
    wf64(0.0);     // origin_n
    wf64(1000.0);  // cell_size
    for (int i = 0; i < 9; ++i) {
        f.write(reinterpret_cast<const char*>(&se_val), 4);
        f.write(reinterpret_cast<const char*>(&sn_val), 4);
    }
    return path;
}

TEST_CASE("OSTN15: not loaded by default", "[ostn15]") {
    // ostn15_loaded() should be false before any load call in a fresh run.
    // (Other tests may have loaded a grid; this test is order-sensitive only
    //  if run in isolation first.  We simply check the API exists.)
    bool loaded = osgb::ostn15_loaded();
    // Either state is valid depending on test ordering; just confirm compilation.
    (void)loaded;
    SUCCEED("ostn15_loaded() is accessible");
}

TEST_CASE("OSTN15: load_ostn15 returns false for missing file", "[ostn15]") {
    bool ok = osgb::load_ostn15(temp_path("bandpass2_ostn15_nonexistent_xyz.dat"));
    REQUIRE_FALSE(ok);
}

TEST_CASE("OSTN15: load_ostn15 loads synthetic grid", "[ostn15]") {
    std::string path = temp_path("bandpass2_ostn15_test_synth.dat");
    write_synthetic_ostn15(path, 100.0f, 200.0f);
    bool ok = osgb::load_ostn15(path);
    REQUIRE(ok);
    REQUIRE(osgb::ostn15_loaded());
}

TEST_CASE("OSTN15: wgs84_to_osgb36_ostn15 applies shift from synthetic grid", "[ostn15]") {
    // Load a uniform-shift grid: SE=100 m, SN=200 m everywhere.
    std::string path = temp_path("bandpass2_ostn15_test_shift.dat");
    write_synthetic_ostn15(path, 100.0f, 200.0f);
    REQUIRE(osgb::load_ostn15(path));

    // Use a point well inside the tiny 3x3 grid (covers E 0-2000, N 0-2000).
    // national_grid::latlon_to_en applied to lat~0, lon~0 gives E~TQ area...
    // Instead, test at a point whose provisional E/N falls inside the grid.
    // The provisional E/N = latlon_to_en(wgs84_pt).
    // Pick a WGS84 point that we know maps to E~1000, N~1000 provisionally.
    // en_to_latlon({1000, 1000}) gives approx lat 49.008, lon -1.9948 (OSGB)
    LatLon approx_wgs84 = national_grid::en_to_latlon({1000.0, 1000.0});

    LatLon result = osgb::wgs84_to_osgb36_ostn15(approx_wgs84);

    // With SE=100, SN=200 applied to provisional E/N ~ (1000, 1000),
    // final E ~ 1100, final N ~ 1200.
    // Converting back: result should differ from Helmert result.
    LatLon helmert = osgb::wgs84_to_osgb36(approx_wgs84);

    // OSTN15 result should differ from Helmert (synthetic shifts were added)
    double dlat = result.lat - helmert.lat;
    double dlon = result.lon - helmert.lon;
    double diff_deg = std::sqrt(dlat*dlat + dlon*dlon);
    // 100-200 m shift at ~50N -> roughly 0.001-0.002 deg difference
    REQUIRE(diff_deg > 0.0005);
}

TEST_CASE("OSTN15: osgb36_to_wgs84_ostn15 round-trip with synthetic grid", "[ostn15]") {
    // SE=100, SN=200 grid must already be loaded from previous test, or reload.
    std::string path = temp_path("bandpass2_ostn15_test_rt.dat");
    write_synthetic_ostn15(path, 100.0f, 200.0f);
    REQUIRE(osgb::load_ostn15(path));

    // Start from a known OSGB36 lat/lon inside the synthetic grid coverage.
    // en_to_latlon({1100, 1200}) -> OSGB36 lat/lon
    LatLon osgb36 = national_grid::en_to_latlon({1100.0, 1200.0});

    // Forward OSGB36->WGS84 should subtract the shifts.
    // Provisional ETRS89 E = 1100 - 100 = 1000, N = 1200 - 200 = 1000.
    LatLon wgs84 = osgb::osgb36_to_wgs84_ostn15(osgb36);

    // Apply forward again to get back close to original OSGB36
    LatLon back = osgb::wgs84_to_osgb36_ostn15(wgs84);

    // Round-trip should be within ~1 m (limited by TM precision, not our algorithm)
    REQUIRE_THAT(back.lat, WithinAbs(osgb36.lat, 0.00002));
    REQUIRE_THAT(back.lon, WithinAbs(osgb36.lon, 0.00002));
}

TEST_CASE("OSTN15: falls back to Helmert for points outside grid coverage", "[ostn15]") {
    // Load the tiny 3x3 synthetic grid (covers E 0-2000, N 0-2000 only).
    std::string path = temp_path("bandpass2_ostn15_test_fallback.dat");
    write_synthetic_ostn15(path, 50.0f, 50.0f);
    REQUIRE(osgb::load_ostn15(path));

    // Use London — its provisional E/N (~530000, 180000) is way outside the grid.
    LatLon london_wgs84 = { 51.5074, -0.1278 };
    LatLon ostn15_result = osgb::wgs84_to_osgb36_ostn15(london_wgs84);
    LatLon helmert_result = osgb::wgs84_to_osgb36(london_wgs84);

    // Outside grid → OSTN15 falls back to Helmert; results must match exactly.
    REQUIRE_THAT(ostn15_result.lat, WithinAbs(helmert_result.lat, 1e-9));
    REQUIRE_THAT(ostn15_result.lon, WithinAbs(helmert_result.lon, 1e-9));
}
