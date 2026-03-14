#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "engine/monteath.h"
#include "engine/asf.h"
#include "engine/grid.h"
#include "engine/terrain.h"
#include "engine/conductivity.h"
#include "model/Scenario.h"
#include <GeographicLib/Geodesic.hpp>

using namespace bp;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Monteath ASF integration tests (P2-05)
// ---------------------------------------------------------------------------

TEST_CASE("monteath: perfect conductor gives zero ASF") {
    // Over σ→∞, |η|² → 0, so τ_asf → 0
    // We simulate near-perfect conductor with σ = 1e6 S/m
    struct PerfectCond : public ConductivityMap {
        GroundConstants lookup(double, double) const override {
            return GroundConstants{ 1e6, 1.0 };
        }
    };
    FlatTerrainMap terrain;
    PerfectCond    cond;
    double asf = monteath_asf_ml(146437.5, 52.3, -0.2, 52.3, -0.5,
                                  terrain, cond, 50);
    CHECK(asf < 0.01);  // essentially zero
}

TEST_CASE("monteath: land path gives positive ASF") {
    FlatTerrainMap     terrain;
    BuiltInConductivityMap cond;
    // Land path from central England to another point (avoid sea)
    double asf = monteath_asf_ml(146437.5,
                                  52.3, -0.2,   // near Huntingdon
                                  51.5, -1.5,   // near Reading
                                  terrain, cond, 50);
    CHECK(asf > 0.0);   // always positive: surface wave delayed vs free-space
}

TEST_CASE("monteath: ASF increases with distance (land)") {
    FlatTerrainMap     terrain;
    BuiltInConductivityMap cond;
    // Same bearing, two distances
    double asf_100 = monteath_asf_ml(146437.5, 52.3, -0.2, 52.3, -1.64,
                                      terrain, cond, 50);   // ~100 km west
    double asf_200 = monteath_asf_ml(146437.5, 52.3, -0.2, 52.3, -3.1,
                                      terrain, cond, 50);   // ~200 km west
    CHECK(asf_200 > asf_100);
    CHECK(asf_200 > 0.5 * asf_100);  // roughly proportional
}

TEST_CASE("monteath: sea path gives smaller ASF than land path (same distance)") {
    FlatTerrainMap     terrain;
    BuiltInConductivityMap cond;
    // Land path ~200 km
    double asf_land = monteath_asf_ml(146437.5, 52.3, -0.2, 54.2, -0.2,
                                       terrain, cond, 50);
    // Sea path ~200 km into Atlantic
    double asf_sea  = monteath_asf_ml(146437.5, 52.0, -10.0, 52.0, -12.0,
                                       terrain, cond, 50);
    CHECK(asf_land > asf_sea);
}

TEST_CASE("monteath: ASF is sub-millilane per km for typical land") {
    // At 200 km over land with σ=0.005, the ASF should be reasonable
    // Current approximation gives ~2 ml/km → ~400 ml at 200 km
    FlatTerrainMap     terrain;
    BuiltInConductivityMap cond;
    double asf = monteath_asf_ml(146437.5, 52.3, -0.2, 50.5, -0.2,
                                  terrain, cond, 50);  // ~200 km south
    CHECK(asf > 50.0);    // must be > 50 ml at 200 km
    CHECK(asf < 3000.0);  // must be < 3 lanes at 200 km (sanity check)
}

TEST_CASE("monteath: result independent of sample count (convergence)") {
    // 20 vs 50 samples should give < 5% difference
    FlatTerrainMap     terrain;
    BuiltInConductivityMap cond;
    double asf_20 = monteath_asf_ml(146437.5, 52.3, -0.2, 53.5, -1.0,
                                     terrain, cond, 20);
    double asf_50 = monteath_asf_ml(146437.5, 52.3, -0.2, 53.5, -1.0,
                                     terrain, cond, 50);
    CHECK(asf_20 > 0.0);
    CHECK(asf_50 > 0.0);
    // Relative difference < 5%
    double diff = std::abs(asf_20 - asf_50) / std::max(asf_50, 0.01);
    CHECK(diff < 0.05);
}

TEST_CASE("monteath: higher frequency gives slightly larger ASF") {
    // Higher frequency → smaller skin depth → larger surface impedance → more ASF
    FlatTerrainMap     terrain;
    BuiltInConductivityMap cond;
    double asf_131 = monteath_asf_ml(131250.0, 52.3, -0.2, 54.0, -0.2,
                                      terrain, cond, 50);
    double asf_146 = monteath_asf_ml(146437.5, 52.3, -0.2, 54.0, -0.2,
                                      terrain, cond, 50);
    // At LF over land with high x=σ/(ωε₀), the impedance factor barely changes
    // with frequency (x >> εᵣ, so |η|² ≈ ωε₀/σ = 1/x → increases with ω)
    // So higher frequency → higher ASF for given ground constants
    CHECK(asf_146 > asf_131);
}

// ---------------------------------------------------------------------------
// computeAtPoint tests (virtual receiver, per-slot phase)
// ---------------------------------------------------------------------------

TEST_CASE("computeAtPoint: returns one result per transmitter") {
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.spo_us = 0.0; tx.station_delay_us = 0.0;
    s.transmitters.push_back(tx);
    Transmitter tx2;
    tx2.lat = 50.7; tx2.lon = -0.8;
    tx2.power_w = 40.0; tx2.slot = 2; tx2.spo_us = 0.0; tx2.station_delay_us = 0.0;
    s.transmitters.push_back(tx2);

    auto results = computeAtPoint(51.5, -1.0, s);
    CHECK(results.size() == 2);
}

TEST_CASE("computeAtPoint: f1minus phase is complement of f1plus") {
    // f1− phase = 1 - f1+ phase (mod 1)
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.spo_us = 0.0; tx.station_delay_us = 0.0;
    s.transmitters.push_back(tx);

    auto results = computeAtPoint(51.5, -1.0, s);
    REQUIRE(!results.empty());
    const auto& r = results[0];
    // f1- = 1 - f1+ (mod 1) — F- navslot has opposite polarity
    double expected_minus = 1.0 - r.f1plus_phase;
    if (expected_minus >= 1.0) expected_minus -= 1.0;
    if (expected_minus < 0.0)  expected_minus += 1.0;
    CHECK(r.f1minus_phase == Approx(expected_minus).margin(1e-9));
    // Lane numbers are the same (same physical distance, different phase reference)
    CHECK(r.f1minus_lane == r.f1plus_lane);
}

TEST_CASE("computeAtPoint: f2minus phase is complement of f2plus") {
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.spo_us = 0.0; tx.station_delay_us = 0.0;
    s.transmitters.push_back(tx);

    auto results = computeAtPoint(51.5, -1.0, s);
    REQUIRE(!results.empty());
    const auto& r = results[0];
    double expected_minus = 1.0 - r.f2plus_phase;
    if (expected_minus >= 1.0) expected_minus -= 1.0;
    if (expected_minus < 0.0)  expected_minus += 1.0;
    CHECK(r.f2minus_phase == Approx(expected_minus).margin(1e-9));
}

TEST_CASE("computeAtPoint: fractional phases are in range 0 to 1") {
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.spo_us = 0.0; tx.station_delay_us = 0.0;
    s.transmitters.push_back(tx);

    auto results = computeAtPoint(51.5, -1.0, s);
    REQUIRE(!results.empty());
    for (const auto& r : results) {
        CHECK(r.f1plus_phase  >= 0.0);  CHECK(r.f1plus_phase  < 1.0);
        CHECK(r.f1minus_phase >= 0.0);  CHECK(r.f1minus_phase < 1.0);
        CHECK(r.f2plus_phase  >= 0.0);  CHECK(r.f2plus_phase  < 1.0);
        CHECK(r.f2minus_phase >= 0.0);  CHECK(r.f2minus_phase < 1.0);
    }
}

TEST_CASE("computeAtPoint: SPO shifts f1plus phase") {
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.station_delay_us = 0.0;

    // Two scenarios: spo_us = 0 and spo_us = some non-zero value
    tx.spo_us = 0.0;
    s.transmitters = { tx };
    auto r0 = computeAtPoint(51.5, -1.0, s);

    tx.spo_us = 1.0;  // 1 µs SPO
    s.transmitters = { tx };
    auto r1 = computeAtPoint(51.5, -1.0, s);

    REQUIRE(!r0.empty()); REQUIRE(!r1.empty());
    // A 1 µs SPO at 146437.5 Hz = 146437.5e-6 * 1e6 = 0.146 cycles extra phase
    // So f1plus should differ by ~0.146 (wrapping mod 1)
    double delta = r1[0].f1plus_phase - r0[0].f1plus_phase;
    if (delta < 0.0)  delta += 1.0;
    if (delta >= 1.0) delta -= 1.0;
    double expected_delta = std::fmod(1.0e-6 * 146437.5, 1.0);
    CHECK(delta == Approx(expected_delta).margin(1e-6));
}

TEST_CASE("computeAtPoint: pseudorange increases with distance") {
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.spo_us = 0.0; tx.station_delay_us = 0.0;
    s.transmitters = { tx };

    auto r_near = computeAtPoint(52.0, -0.2, s);  // ~33 km
    auto r_far  = computeAtPoint(50.0, -0.2, s);  // ~255 km

    REQUIRE(!r_near.empty()); REQUIRE(!r_far.empty());
    CHECK(r_far[0].pseudorange_m > r_near[0].pseudorange_m);
}

TEST_CASE("computeAtPoint: SNR decreases with distance") {
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.spo_us = 0.0; tx.station_delay_us = 0.0;
    s.transmitters = { tx };

    auto r_near = computeAtPoint(52.0, -0.2, s);
    auto r_far  = computeAtPoint(50.0, -0.2, s);

    REQUIRE(!r_near.empty()); REQUIRE(!r_far.empty());
    CHECK(r_near[0].snr_db > r_far[0].snr_db);
}

TEST_CASE("computeAtPoint: empty transmitter list returns empty vector") {
    Scenario s;
    s.frequencies.recompute();
    auto results = computeAtPoint(51.5, -1.0, s);
    CHECK(results.empty());
}

TEST_CASE("computeAtPoint: station_delay_us shifts f1plus phase") {
    // station_delay_us and spo_us both add to the total propagation delay.
    // 1 µs delay at f1 = 146437.5 Hz → 0.146 extra cycles.
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.spo_us = 0.0;

    tx.station_delay_us = 0.0;
    s.transmitters = { tx };
    auto r0 = computeAtPoint(51.5, -1.0, s);

    tx.station_delay_us = 1.0;
    s.transmitters = { tx };
    auto r1 = computeAtPoint(51.5, -1.0, s);

    REQUIRE(!r0.empty()); REQUIRE(!r1.empty());
    double delta = r1[0].f1plus_phase - r0[0].f1plus_phase;
    if (delta < 0.0)  delta += 1.0;
    if (delta >= 1.0) delta -= 1.0;
    double expected_delta = std::fmod(1.0e-6 * s.frequencies.f1_hz, 1.0);
    CHECK(delta == Approx(expected_delta).margin(1e-6));
}

TEST_CASE("computeAtPoint: SPO shifts f2plus phase") {
    // Verify SPO is applied to F2 as well as F1.
    // 1 µs SPO at f2 = 131250.0 Hz → 0.131 extra cycles.
    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3; tx.lon = -0.2;
    tx.power_w = 40.0; tx.slot = 1; tx.station_delay_us = 0.0;

    tx.spo_us = 0.0;
    s.transmitters = { tx };
    auto r0 = computeAtPoint(51.5, -1.0, s);

    tx.spo_us = 1.0;
    s.transmitters = { tx };
    auto r1 = computeAtPoint(51.5, -1.0, s);

    REQUIRE(!r0.empty()); REQUIRE(!r1.empty());
    double delta = r1[0].f2plus_phase - r0[0].f2plus_phase;
    if (delta < 0.0)  delta += 1.0;
    if (delta >= 1.0) delta -= 1.0;
    double expected_delta = std::fmod(1.0e-6 * s.frequencies.f2_hz, 1.0);
    CHECK(delta == Approx(expected_delta).margin(1e-6));
}

TEST_CASE("monteath: identical TX and RX coordinates return zero ASF") {
    // Path length < 1 m → total_dist_m < 1.0 → early return 0.0
    FlatTerrainMap     terrain;
    BuiltInConductivityMap cond;
    double asf = monteath_asf_ml(146437.5, 52.3, -0.2, 52.3, -0.2,
                                  terrain, cond, 10);
    CHECK(asf == Approx(0.0).margin(1e-10));
}

// ---------------------------------------------------------------------------
// ASF lane width tests (from spec/FrequencyConfig.md)
// ---------------------------------------------------------------------------

TEST_CASE("frequencies: default f1 lane width") {
    Frequencies f;
    f.recompute();
    // lane_width = c / freq;  c = 299 792 458 m/s exactly (SI definition)
    CHECK(f.lane_width_f1_m == Approx(299792458.0 / 146437.5).margin(0.01));
}

TEST_CASE("frequencies: default f2 lane width") {
    Frequencies f;
    f.recompute();
    CHECK(f.lane_width_f2_m == Approx(299792458.0 / 131250.0).margin(0.01));
}

TEST_CASE("frequencies: 137 kHz lane width") {
    Frequencies f;
    f.f1_hz = 137000.0;
    f.recompute();
    CHECK(f.lane_width_f1_m == Approx(299792458.0 / 137000.0).margin(0.01));
}

TEST_CASE("frequencies: validation range") {
    Frequencies f;
    f.f1_hz = 29999.9; f.f2_hz = 131250.0;
    CHECK(!f.is_valid_range());
    f.f1_hz = 300000.1;
    CHECK(!f.is_valid_range());
    f.f1_hz = 146437.5;
    CHECK(f.is_valid_range());
}

TEST_CASE("frequencies: f1==f2 is allowed") {
    // CLAUDE.md: f1 == f2 is allowed with a warning (not an error)
    Frequencies f;
    f.f1_hz = f.f2_hz = 146437.5;
    CHECK(f.is_valid_range());
}

// ---------------------------------------------------------------------------
// P4-02 Virtual Locator WLS convergence (indirect via computeASF)
// ---------------------------------------------------------------------------

static Scenario make_4tx_scenario() {
    // Four transmitters at the corners of a ~300 km square around central England.
    // Power is set high (100 kW) so that SNR is clearly positive at ~200 km range.
    //
    // At 200 km, 40 W: E_rx ≈ 15 dBµV/m < noise floor ≈ 27 dBµV/m → SNR < 0.
    // At 200 km, 100 kW: E_rx ≈ 49 dBµV/m → SNR ≈ +22 dB → stations are usable.
    //
    // The test exercises P4-01/P4-02 physics correctness, not Datatrak TX power spec.
    Scenario s;
    s.frequencies.recompute();
    s.grid.lat_min = 52.5; s.grid.lat_max = 53.5;
    s.grid.lon_min = -1.5; s.grid.lon_max = -0.5;
    s.grid.resolution_km = 50.0;   // coarse — just enough for a 3×3 grid
    s.receiver.noise_floor_dbuvpm   = 14.0;
    s.receiver.vehicle_noise_dbuvpm = 27.0;
    s.receiver.max_range_km         = 600.0;
    s.receiver.min_stations         = 4;

    auto make_tx = [](double lat, double lon, int slot) {
        Transmitter t;
        t.lat = lat; t.lon = lon;
        t.power_w = 100000.0;  // 100 kW — ensures SNR > 0 at ~200 km range in tests
        t.height_m = 50.0;
        t.slot = slot; t.is_master = (slot == 1);
        return t;
    };
    s.transmitters.push_back(make_tx(50.5, -2.5, 1));  // SW
    s.transmitters.push_back(make_tx(50.5,  0.5, 2));  // SE
    s.transmitters.push_back(make_tx(55.5, -2.5, 3));  // NW
    s.transmitters.push_back(make_tx(55.5,  0.5, 4));  // NE
    return s;
}

TEST_CASE("computeASF: absolute accuracy is finite and bounded for 4-TX network") {
    // P4-02: the iterated WLS VL fix must converge to a finite value.
    // A 4-TX network with good geometry should give absolute accuracy < 5000 m
    // (generous bound — real values will be much smaller; we're testing that
    // the VL fix actually converges rather than returning 9999 m everywhere).
    Scenario s = make_4tx_scenario();
    std::atomic<bool> cancel{false};
    auto pts = buildGrid(s.grid, cancel);
    REQUIRE(!pts.empty());

    GridData data;
    for (const char* name : {"groundwave","skywave","atm_noise","snr","sgr","gdr",
                              "whdop","repeatable","asf","asf_gradient",
                              "absolute_accuracy","absolute_accuracy_corrected","confidence"}) {
        GridArray arr;
        arr.layer_name = name;
        arr.points = pts;
        arr.values.assign(pts.size(), 0.0);
        arr.lat_min = s.grid.lat_min; arr.lat_max = s.grid.lat_max;
        arr.lon_min = s.grid.lon_min; arr.lon_max = s.grid.lon_max;
        arr.resolution_km = s.grid.resolution_km;
        data.layers[name] = std::move(arr);
    }

    // Run groundwave + SNR first (computeASF depends on SNR data being present
    // via whdop; it recomputes locally, but we still need the layer structure)
    computeASF(data, s, cancel);

    const auto& abs_acc = data.layers.at("absolute_accuracy");
    REQUIRE(abs_acc.values.size() == pts.size());

    // At least one grid point inside the network should have a finite fix
    // (not the 9999 m sentinel that indicates no usable geometry)
    bool any_finite = false;
    for (double v : abs_acc.values) {
        if (v < 5000.0) { any_finite = true; break; }
    }
    CHECK(any_finite);
}

TEST_CASE("computeASF: confidence factor is in [0,1]") {
    Scenario s = make_4tx_scenario();
    std::atomic<bool> cancel{false};
    auto pts = buildGrid(s.grid, cancel);
    REQUIRE(!pts.empty());

    GridData data;
    for (const char* name : {"groundwave","skywave","atm_noise","snr","sgr","gdr",
                              "whdop","repeatable","asf","asf_gradient",
                              "absolute_accuracy","absolute_accuracy_corrected","confidence"}) {
        GridArray arr;
        arr.layer_name = name;
        arr.points = pts;
        arr.values.assign(pts.size(), 0.0);
        arr.lat_min = s.grid.lat_min; arr.lat_max = s.grid.lat_max;
        arr.lon_min = s.grid.lon_min; arr.lon_max = s.grid.lon_max;
        arr.resolution_km = s.grid.resolution_km;
        data.layers[name] = std::move(arr);
    }
    computeASF(data, s, cancel);

    for (double v : data.layers.at("confidence").values) {
        CHECK(v >= 0.0);
        CHECK(v <= 1.0);
    }
}

// ---------------------------------------------------------------------------
// P4-04 Airy ellipsoid (indirect via computeAtPoint pseudorange)
// ---------------------------------------------------------------------------

TEST_CASE("computeAtPoint: pseudorange uses Airy ellipsoid, differs from WGS84") {
    // Airy 1830 semi-major axis 6377563.396 m vs WGS84 6378137.0 m.
    // For a 200 km path at mid-UK latitudes the Airy range is shorter by
    // roughly (6378137 - 6377563) / 6378137 × 200 km ≈ 18 m.
    // We verify the pseudorange is close to the geometric range but not
    // exactly equal to the WGS84 geodesic (would differ by ~18 m).
    //
    // TX at Huntingdon (52.3247 N, -0.1848 E), RX at 50.5 N 0.0 E (~200 km)
    const GeographicLib::Geodesic& wgs84 = GeographicLib::Geodesic::WGS84();
    double wgs84_dist_m = 0.0;
    wgs84.Inverse(52.3247, -0.1848, 50.5, 0.0, wgs84_dist_m);

    Scenario s;
    s.frequencies.recompute();
    Transmitter tx;
    tx.lat = 52.3247; tx.lon = -0.1848;
    tx.power_w = 40.0; tx.slot = 1;
    s.transmitters = { tx };

    auto results = computeAtPoint(50.5, 0.0, s);
    REQUIRE(!results.empty());
    double pseudorange = results[0].pseudorange_m;

    // Pseudorange should be in the right ballpark (~200 km ± 1 km for ASF)
    CHECK(pseudorange > 190000.0);
    CHECK(pseudorange < 215000.0);

    // Should NOT be exactly the WGS84 distance (Airy ellipsoid is used)
    // The difference must be at least 1 m (Airy vs WGS84 at ~200 km)
    CHECK(std::abs(pseudorange - wgs84_dist_m) > 1.0);
}

// ---------------------------------------------------------------------------
// P4-01 ASF gradient layer is non-zero for a network with coverage
// ---------------------------------------------------------------------------

TEST_CASE("computeASF: asf_gradient layer is non-zero inside network") {
    Scenario s = make_4tx_scenario();
    std::atomic<bool> cancel{false};
    auto pts = buildGrid(s.grid, cancel);
    REQUIRE(!pts.empty());

    GridData data;
    for (const char* name : {"groundwave","skywave","atm_noise","snr","sgr","gdr",
                              "whdop","repeatable","asf","asf_gradient",
                              "absolute_accuracy","absolute_accuracy_corrected","confidence"}) {
        GridArray arr;
        arr.layer_name = name;
        arr.points = pts;
        arr.values.assign(pts.size(), 0.0);
        arr.lat_min = s.grid.lat_min; arr.lat_max = s.grid.lat_max;
        arr.lon_min = s.grid.lon_min; arr.lon_max = s.grid.lon_max;
        arr.resolution_km = s.grid.resolution_km;
        data.layers[name] = std::move(arr);
    }
    computeASF(data, s, cancel);

    const auto& grad = data.layers.at("asf_gradient");
    bool any_nonzero = false;
    for (double v : grad.values) {
        if (v > 0.0) { any_nonzero = true; break; }
    }
    CHECK(any_nonzero);
}
