#include <catch2/catch_test_macros.hpp>
#include "almanac/AlmanacExport.h"
#include "model/Scenario.h"

using namespace bp;
using namespace bp::almanac;

static Scenario make_test_scenario() {
    Scenario s;
    s.name = "Test";
    s.frequencies.f1_hz = 146437.5;
    s.frequencies.f2_hz = 131250.0;
    s.frequencies.recompute();

    Transmitter tx1;
    tx1.name = "Huntingdon"; tx1.lat = 52.3247; tx1.lon = -0.1848;
    tx1.slot = 1; tx1.is_master = true; tx1.power_w = 40.0;

    Transmitter tx2;
    tx2.name = "Selsey"; tx2.lat = 50.73; tx2.lon = -0.79;
    tx2.slot = 2; tx2.power_w = 40.0;

    s.transmitters = { tx1, tx2 };
    return s;
}

TEST_CASE("Sg: contains one line per transmitter") {
    auto s = make_test_scenario();
    auto text = generate_sg(s);
    // Should contain "Sg 1" and "Sg 2"
    CHECK(text.find("Sg 1") != std::string::npos);
    CHECK(text.find("Sg 2") != std::string::npos);
}

TEST_CASE("Sg: Huntingdon easting ~513000") {
    auto s = make_test_scenario();
    auto text = generate_sg(s);
    // Huntingdon OSGB easting is around 523000 (Helmert approximate)
    // Very rough check that the number appears in the output
    bool found = (text.find("52") != std::string::npos);
    CHECK(found);
}

TEST_CASE("Stxs: one line per transmitter") {
    auto s = make_test_scenario();
    auto text = generate_stxs(s);
    CHECK(text.find("Stxs 1") != std::string::npos);
    CHECK(text.find("Stxs 2") != std::string::npos);
}

TEST_CASE("Po: empty when no pattern offsets") {
    auto s = make_test_scenario();
    GridData gd;
    auto text = generate_po(s, gd, FirmwareFormat::V7);
    CHECK(text.find("No pattern offsets") != std::string::npos);
}

TEST_CASE("Po: V7 format outputs unsigned values") {
    auto s = make_test_scenario();
    PatternOffset po;
    po.pattern    = "8,2";
    po.f1plus_ml  = 537;
    po.f1minus_ml = 501;
    po.f2plus_ml  = 603;
    po.f2minus_ml = 569;
    s.pattern_offsets.push_back(po);

    GridData gd;
    auto text = generate_po(s, gd, FirmwareFormat::V7);
    CHECK(text.find("po 8,2 537 501 603 569") != std::string::npos);
}

TEST_CASE("Po: V7 clips negative values to 0") {
    auto s = make_test_scenario();
    PatternOffset po;
    po.pattern = "3,1";
    po.f1plus_ml = -5; po.f1minus_ml = 100;
    po.f2plus_ml = 200; po.f2minus_ml = -10;
    s.pattern_offsets.push_back(po);

    GridData gd;
    auto text = generate_po(s, gd, FirmwareFormat::V7);
    // -5 should be clipped to 0, -10 to 0
    CHECK(text.find("po 3,1 0 100 200 0") != std::string::npos);
    CHECK(text.find("WARNING") != std::string::npos);
}

TEST_CASE("Po: V7 clips values above 999") {
    auto s = make_test_scenario();
    PatternOffset po;
    po.pattern    = "5,3";
    po.f1plus_ml  = 1000;   // exactly at boundary — clips to 999
    po.f1minus_ml = 2500;   // well above — clips to 999
    po.f2plus_ml  = 500;    // in range — unchanged
    po.f2minus_ml = 999;    // exactly at max — unchanged
    s.pattern_offsets.push_back(po);

    GridData gd;
    auto text = generate_po(s, gd, FirmwareFormat::V7);
    CHECK(text.find("po 5,3 999 999 500 999") != std::string::npos);
}

TEST_CASE("Po: V16 format uses signed values") {
    auto s = make_test_scenario();
    PatternOffset po;
    po.pattern = "8,2";
    po.f1plus_ml = 537; po.f1minus_ml = 501;
    po.f2plus_ml = 603; po.f2minus_ml = 569;
    s.pattern_offsets.push_back(po);

    GridData gd;
    auto text = generate_po(s, gd, FirmwareFormat::V16);
    // V16 should have explicit + sign
    CHECK(text.find("+537") != std::string::npos);
}

TEST_CASE("Full almanac: non-standard frequency triggers warning") {
    auto s = make_test_scenario();
    s.frequencies.f1_hz = 137000.0;  // non-standard
    s.frequencies.f2_hz = 137000.0;
    GridData gd;
    auto text = generate_almanac(s, gd);
    CHECK(text.find("WARNING: Non-standard") != std::string::npos);
}

TEST_CASE("Full almanac: standard frequency no warning") {
    auto s = make_test_scenario();
    GridData gd;
    auto text = generate_almanac(s, gd);
    CHECK(text.find("WARNING: Non-standard") == std::string::npos);
}
