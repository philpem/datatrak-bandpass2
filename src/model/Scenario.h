#pragma once
#include <string>
#include <vector>
#include "Transmitter.h"
#include "ReceiverModel.h"
#include "MonitorStation.h"
#include "SlotPhaseResult.h"

namespace bp {

struct Frequencies {
    double f1_hz = 146'437.5;
    double f2_hz = 131'250.0;
    // Derived — recomputed whenever f1_hz or f2_hz changes
    double lane_width_f1_m = 0.0;
    double lane_width_f2_m = 0.0;

    void recompute() {
        constexpr double C = 299'792'458.0;
        lane_width_f1_m = C / f1_hz;
        lane_width_f2_m = C / f2_hz;
    }

    bool is_standard() const {
        return (f1_hz == 146'437.5 && f2_hz == 131'250.0);
    }

    // Returns true iff both frequencies are within the hard 30–300 kHz limits.
    bool is_valid_range() const {
        return f1_hz >= 30e3 && f1_hz <= 300e3 &&
               f2_hz >= 30e3 && f2_hz <= 300e3;
    }
};

struct GridDef {
    double lat_min       = 49.5;
    double lat_max       = 61.0;
    double lon_min       = -7.0;
    double lon_max       =  2.5;
    double resolution_km = 10.0;
};

struct PatternOffset {
    std::string pattern;
    int32_t f1plus_ml  = 0;
    int32_t f1minus_ml = 0;
    int32_t f2plus_ml  = 0;
    int32_t f2minus_ml = 0;
};

struct Scenario {
    std::string name    = "Untitled";
    std::string created;

    GridDef                     grid;
    Frequencies                 frequencies;
    ReceiverModel               receiver;
    std::vector<TransmitterSite> transmitter_sites;

    // Flat (site × slot) list for the physics pipeline and almanac export.
    // Each element merges site-level properties (position, power, height) with
    // one SlotConfig; the order is site-major, slot-minor.
    std::vector<Transmitter> flatTransmitters() const {
        std::vector<Transmitter> result;
        for (const auto& site : transmitter_sites) {
            for (const auto& sc : site.slots) {
                Transmitter tx;
                tx.name             = site.name;
                tx.lat              = site.lat;
                tx.lon              = site.lon;
                tx.osgb_easting     = site.osgb_easting;
                tx.osgb_northing    = site.osgb_northing;
                tx.power_w          = site.power_w;
                tx.height_m         = site.height_m;
                tx.locked           = site.locked;
                tx.slot             = sc.slot;
                tx.is_master        = sc.is_master;
                tx.master_slot      = sc.master_slot;
                tx.spo_us           = sc.spo_us;
                tx.station_delay_us = sc.station_delay_us;
                result.push_back(tx);
            }
        }
        return result;
    }
    std::vector<MonitorStation> monitor_stations;
    std::vector<PatternOffset>  pattern_offsets;

    enum class ConductivitySource { BuiltIn, ItuP832, BGS, File };
    ConductivitySource conductivity_source = ConductivitySource::BuiltIn;
    std::string        conductivity_file;

    enum class TerrainSource { Flat, File };
    TerrainSource terrain_source = TerrainSource::Flat;
    std::string   terrain_file;  // GeoTIFF path or directory of .hgt tiles


    // Groundwave propagation model selection.
    // Homogeneous:      single midpoint conductivity, P.368 polynomial (fastest).
    // Millington:       mixed-path Millington averaging, P.368 polynomial.
    // GRWAVE:           mixed-path Millington averaging, full P.368 GRWAVE
    //                   residue series (most accurate, slowest).
    enum class PropagationModel { Homogeneous, Millington, GRWAVE };
    PropagationModel propagation_model = PropagationModel::Millington;

    // When true (default), computeASF() pre-computes an ntx*n Airy distance
    // table (~16 bytes per TX-RX pair) to avoid redundant geodesic calls.
    // Disable to trade speed for lower memory usage on large grids.
    bool precompute_airy_cache = true;
};

} // namespace bp
