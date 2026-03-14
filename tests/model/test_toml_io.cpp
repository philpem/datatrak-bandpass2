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
    Transmitter tx;
    tx.name     = "Huntingdon";
    tx.lat      = 52.3247;
    tx.lon      = -0.1848;
    tx.power_w  = 40.0;
    tx.slot     = 1;
    tx.is_master = true;
    tx.station_delay_us = 0.19;
    s.transmitters.push_back(tx);

    auto path = std::filesystem::temp_directory_path() / "bp2_tx_roundtrip.toml";
    toml_io::save(s, path);
    auto s2 = toml_io::load(path);
    REQUIRE(s2.transmitters.size() == 1);
    REQUIRE(s2.transmitters[0].name == "Huntingdon");
    REQUIRE_THAT(s2.transmitters[0].lat, WithinAbs(52.3247, 1e-6));
    REQUIRE_THAT(s2.transmitters[0].lon, WithinAbs(-0.1848, 1e-6));
    REQUIRE(s2.transmitters[0].slot == 1);
    REQUIRE(s2.transmitters[0].is_master);
    REQUIRE_THAT(s2.transmitters[0].station_delay_us, WithinAbs(0.19, 1e-9));
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
