#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <fstream>
#include "model/toml_io.h"
#include "model/Scenario.h"

using namespace bp;
using namespace Catch::Matchers;

static std::filesystem::path TempFile() {
    auto p = std::filesystem::temp_directory_path() / "bp2_test_XXXXXX.toml";
    return p;
}

TEST_CASE("Default frequencies are Datatrak standard", "[toml_io]") {
    Scenario s;
    s.frequencies.recompute();
    REQUIRE_THAT(s.frequencies.f1_hz, WithinAbs(146437.5, 0.001));
    REQUIRE_THAT(s.frequencies.f2_hz, WithinAbs(131250.0, 0.001));
    REQUIRE(s.frequencies.is_standard());
}

TEST_CASE("Lane width computation from frequency", "[toml_io]") {
    Scenario s;
    s.frequencies.f1_hz = 146437.5;
    s.frequencies.f2_hz = 131250.0;
    s.frequencies.recompute();
    constexpr double C = 299792458.0;
    REQUIRE_THAT(s.frequencies.lane_width_f1_m, WithinAbs(C / 146437.5, 0.01));
    REQUIRE_THAT(s.frequencies.lane_width_f2_m, WithinAbs(C / 131250.0, 0.01));
}

TEST_CASE("Round-trip at default frequencies", "[toml_io]") {
    Scenario s;
    s.frequencies.recompute();
    auto path = std::filesystem::temp_directory_path() / "bp2_roundtrip_default.toml";
    toml_io::save(s, path);
    auto s2 = toml_io::load(path);
    REQUIRE_THAT(s2.frequencies.f1_hz, WithinAbs(s.frequencies.f1_hz, 0.001));
    REQUIRE_THAT(s2.frequencies.f2_hz, WithinAbs(s.frequencies.f2_hz, 0.001));
    std::filesystem::remove(path);
}

TEST_CASE("Round-trip at non-standard frequencies", "[toml_io]") {
    Scenario s;
    s.frequencies.f1_hz = 137000.0;
    s.frequencies.f2_hz = 137000.0;
    s.frequencies.recompute();
    auto path = std::filesystem::temp_directory_path() / "bp2_roundtrip_137.toml";
    toml_io::save(s, path);
    auto s2 = toml_io::load(path);
    REQUIRE_THAT(s2.frequencies.f1_hz, WithinAbs(137000.0, 0.5));
    REQUIRE_THAT(s2.frequencies.f2_hz, WithinAbs(137000.0, 0.5));
    std::filesystem::remove(path);
}

TEST_CASE("Transmitter round-trip", "[toml_io]") {
    Scenario s;
    TransmitterSite site;
    site.name    = "Huntingdon";
    site.lat     = 52.3247;
    site.lon     = -0.1848;
    site.power_w = 40.0;
    SlotConfig sc;
    sc.slot             = 1;
    sc.is_master        = true;
    sc.station_delay_us = 0.19;
    site.slots.push_back(sc);
    s.transmitter_sites.push_back(site);

    auto path = std::filesystem::temp_directory_path() / "bp2_tx_roundtrip.toml";
    toml_io::save(s, path);
    auto s2 = toml_io::load(path);
    REQUIRE(s2.transmitter_sites.size() == 1);
    REQUIRE(s2.transmitter_sites[0].name == "Huntingdon");
    REQUIRE_THAT(s2.transmitter_sites[0].lat, WithinAbs(52.3247, 1e-6));
    REQUIRE_THAT(s2.transmitter_sites[0].lon, WithinAbs(-0.1848, 1e-6));
    REQUIRE(s2.transmitter_sites[0].slots.size() == 1);
    REQUIRE(s2.transmitter_sites[0].slots[0].slot == 1);
    REQUIRE(s2.transmitter_sites[0].slots[0].is_master);
    REQUIRE_THAT(s2.transmitter_sites[0].slots[0].station_delay_us, WithinAbs(0.19, 1e-9));
    std::filesystem::remove(path);
}

TEST_CASE("Missing [frequencies] section uses defaults", "[toml_io]") {
    auto path = std::filesystem::temp_directory_path() / "bp2_no_freq.toml";
    {
        std::ofstream f(path);
        f << "[scenario]\nname = \"test\"\n";
    }
    auto s = toml_io::load(path);
    REQUIRE_THAT(s.frequencies.f1_hz, WithinAbs(146437.5, 0.001));
    REQUIRE_THAT(s.frequencies.f2_hz, WithinAbs(131250.0, 0.001));
    std::filesystem::remove(path);
}

TEST_CASE("Multi-slot site round-trip", "[toml_io]") {
    // A single physical site carrying two slots (colocated transmitters)
    Scenario s;
    TransmitterSite site;
    site.name    = "Droitwich";
    site.lat     = 52.2981;
    site.lon     = -2.1035;
    site.power_w = 40.0;
    site.locked  = true;

    SlotConfig sc1;
    sc1.slot             = 3;
    sc1.is_master        = true;
    sc1.spo_us           = 0.0;
    sc1.station_delay_us = 0.0;

    SlotConfig sc2;
    sc2.slot             = 7;
    sc2.is_master        = false;
    sc2.master_slot      = 3;
    sc2.spo_us           = 0.0;
    sc2.station_delay_us = 0.31;

    site.slots.push_back(sc1);
    site.slots.push_back(sc2);
    s.transmitter_sites.push_back(site);

    auto path = std::filesystem::temp_directory_path() / "bp2_multislot_roundtrip.toml";
    toml_io::save(s, path);
    auto s2 = toml_io::load(path);

    REQUIRE(s2.transmitter_sites.size() == 1);
    const auto& rs = s2.transmitter_sites[0];
    REQUIRE(rs.name == "Droitwich");
    REQUIRE_THAT(rs.lat, WithinAbs(52.2981, 1e-6));
    REQUIRE_THAT(rs.lon, WithinAbs(-2.1035, 1e-6));
    REQUIRE(rs.locked == true);
    REQUIRE(rs.slots.size() == 2);

    REQUIRE(rs.slots[0].slot == 3);
    REQUIRE(rs.slots[0].is_master == true);
    REQUIRE_THAT(rs.slots[0].station_delay_us, WithinAbs(0.0, 1e-9));

    REQUIRE(rs.slots[1].slot == 7);
    REQUIRE(rs.slots[1].is_master == false);
    REQUIRE(rs.slots[1].master_slot == 3);
    REQUIRE_THAT(rs.slots[1].station_delay_us, WithinAbs(0.31, 1e-9));

    std::filesystem::remove(path);
}

TEST_CASE("Legacy [[transmitters]] format migrates to single-slot sites", "[toml_io]") {
    // Write a file in the old flat format with two separate transmitter entries
    auto path = std::filesystem::temp_directory_path() / "bp2_legacy_tx.toml";
    {
        std::ofstream f(path);
        f << R"toml(
[[transmitters]]
name    = "Huntingdon"
lat     = 52.3247
lon     = -0.1848
power_w = 40.0
height_m = 50.0
slot    = 1
is_master = true
master_slot = 0
spo_us = 0.0
station_delay_us = 0.0

[[transmitters]]
name    = "Selsey"
lat     = 50.7300
lon     = -0.7900
power_w = 40.0
height_m = 50.0
slot    = 2
is_master = false
master_slot = 1
spo_us = 0.0
station_delay_us = 0.19
)toml";
    }

    auto s = toml_io::load(path);

    // Each old entry becomes a separate single-slot site
    REQUIRE(s.transmitter_sites.size() == 2);

    REQUIRE(s.transmitter_sites[0].name == "Huntingdon");
    REQUIRE(s.transmitter_sites[0].slots.size() == 1);
    REQUIRE(s.transmitter_sites[0].slots[0].slot == 1);
    REQUIRE(s.transmitter_sites[0].slots[0].is_master == true);

    REQUIRE(s.transmitter_sites[1].name == "Selsey");
    REQUIRE(s.transmitter_sites[1].slots.size() == 1);
    REQUIRE(s.transmitter_sites[1].slots[0].slot == 2);
    REQUIRE(s.transmitter_sites[1].slots[0].is_master == false);
    REQUIRE(s.transmitter_sites[1].slots[0].master_slot == 1);
    REQUIRE_THAT(s.transmitter_sites[1].slots[0].station_delay_us, WithinAbs(0.19, 1e-9));

    // flatTransmitters() must give one entry per (site × slot)
    auto flat = s.flatTransmitters();
    REQUIRE(flat.size() == 2);
    REQUIRE(flat[0].slot == 1);
    REQUIRE(flat[1].slot == 2);

    std::filesystem::remove(path);
}

TEST_CASE("Site locked field round-trips through TOML", "[toml_io]") {
    Scenario s;
    TransmitterSite site;
    site.name   = "TestSite";
    site.lat    = 51.5;
    site.lon    = -0.1;
    site.locked = true;
    SlotConfig sc;
    sc.slot = 1;
    sc.is_master = true;
    site.slots.push_back(sc);
    s.transmitter_sites.push_back(site);

    auto path = std::filesystem::temp_directory_path() / "bp2_locked_roundtrip.toml";
    toml_io::save(s, path);
    auto s2 = toml_io::load(path);
    REQUIRE(s2.transmitter_sites.size() == 1);
    REQUIRE(s2.transmitter_sites[0].locked == true);

    // Unlocked also round-trips correctly
    s.transmitter_sites[0].locked = false;
    toml_io::save(s, path);
    auto s3 = toml_io::load(path);
    REQUIRE(s3.transmitter_sites[0].locked == false);

    std::filesystem::remove(path);
}

TEST_CASE("Propagation model round-trip: Millington (default)", "[toml_io]") {
    Scenario s;
    auto path = std::filesystem::temp_directory_path() / "bp2_prop_mil.toml";
    toml_io::save(s, path);
    auto s2 = toml_io::load(path);
    REQUIRE(s2.propagation_model == Scenario::PropagationModel::Millington);
    std::filesystem::remove(path);
}

TEST_CASE("Propagation model round-trip: Homogeneous", "[toml_io]") {
    Scenario s;
    s.propagation_model = Scenario::PropagationModel::Homogeneous;
    auto path = std::filesystem::temp_directory_path() / "bp2_prop_hom.toml";
    toml_io::save(s, path);
    auto s2 = toml_io::load(path);
    REQUIRE(s2.propagation_model == Scenario::PropagationModel::Homogeneous);
    std::filesystem::remove(path);
}

TEST_CASE("Missing [propagation] section defaults to Millington", "[toml_io]") {
    auto path = std::filesystem::temp_directory_path() / "bp2_no_prop.toml";
    {
        std::ofstream f(path);
        f << "[scenario]\nname = \"test\"\n";
    }
    auto s = toml_io::load(path);
    REQUIRE(s.propagation_model == Scenario::PropagationModel::Millington);
    std::filesystem::remove(path);
}

TEST_CASE("Invalid TOML throws runtime_error", "[toml_io]") {
    auto path = std::filesystem::temp_directory_path() / "bp2_bad.toml";
    {
        std::ofstream f(path);
        f << "not valid toml @@@ !!!\n[[[bad\n";
    }
    REQUIRE_THROWS_AS(toml_io::load(path), std::runtime_error);
    std::filesystem::remove(path);
}
