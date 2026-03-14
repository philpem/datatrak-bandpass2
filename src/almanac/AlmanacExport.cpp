#include "AlmanacExport.h"
#include "ZonePatterns.h"
#include "../coords/Osgb.h"
#include "../coords/NationalGrid.h"
#include "../engine/monteath.h"
#include "../engine/conductivity.h"
#include "../engine/terrain.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <map>
#include <stdexcept>

namespace bp {
namespace almanac {

// ---------------------------------------------------------------------------
// Header block: generation parameters + frequency warnings
// ---------------------------------------------------------------------------
static std::string make_header(const Scenario& scenario, FirmwareFormat fmt) {
    const double c = 299'792'458.0;
    double f1_khz  = scenario.frequencies.f1_hz / 1000.0;
    double f2_khz  = scenario.frequencies.f2_hz / 1000.0;
    double lw1     = c / scenario.frequencies.f1_hz;  // lane width (m)
    double lw2     = c / scenario.frequencies.f2_hz;

    bool non_standard = (std::abs(f1_khz - 146.4375) > 0.001 ||
                         std::abs(f2_khz - 131.2500)  > 0.001);

    std::ostringstream ss;
    ss << "# BANDPASS II Almanac Export\n";
    ss << "# Scenario: " << scenario.name << "\n";
    ss << "# Format: " << (fmt == FirmwareFormat::V7 ? "V7" : "V16") << "\n";
    ss << "# Mode: " << (scenario.mode == Scenario::OperationMode::Interlaced
                         ? "interlaced (24 slots)" : "8-slot") << "\n";
    ss << std::fixed << std::setprecision(4);
    ss << "# F1: " << f1_khz << " kHz  lane width: "
       << std::setprecision(2) << lw1 << " m  (1 ml = "
       << lw1 / 1000.0 << " m)\n";
    ss << std::setprecision(4);
    ss << "# F2: " << f2_khz << " kHz  lane width: "
       << std::setprecision(2) << lw2 << " m  (1 ml = "
       << lw2 / 1000.0 << " m)\n";
    if (non_standard) {
        ss << "# WARNING: Non-standard frequencies. po values are in millilanes of the\n";
        ss << "#          configured frequencies, not Datatrak-standard millilanes.\n";
    }
    ss << "#\n";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Sg: station grid — OSGB easting/northing per station
// Format: Sg slot easting northing
// ---------------------------------------------------------------------------
std::string generate_sg(const Scenario& scenario) {
    std::ostringstream ss;
    ss << "# Sg commands — station grid (OSGB National Grid)\n";
    for (const auto& tx : scenario.transmitters) {
        // Convert WGS84 lat/lon to OSGB36 then Easting/Northing
        LatLon osgb36 = osgb::wgs84_to_osgb36({tx.lat, tx.lon});
        EastNorth en  = national_grid::latlon_to_en(osgb36);
        ss << "Sg " << tx.slot
           << " " << (int)std::round(en.easting)
           << " " << (int)std::round(en.northing)
           << "\n";
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// Stxs: slot → station assignment
// Format: Stxs slot addr  (addr = slot number used as station address)
// ---------------------------------------------------------------------------
std::string generate_stxs(const Scenario& scenario) {
    std::ostringstream ss;
    ss << "# Stxs commands — slot-to-station assignment\n";
    for (const auto& tx : scenario.transmitters) {
        ss << "Stxs " << tx.slot << " " << tx.slot << "\n";
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// Po: pattern offsets (ASF corrections in millilanes)
//
// V7 format (unsigned 0-999):  po pat1,pat2 f1plus f1minus f2plus f2minus
// V16 format (signed):         po pat1,pat2 +f1plus +f1minus +f2plus +f2minus
//
// If scenario.pattern_offsets is populated (from monitor calibration), use
// those directly.  Otherwise compute a nominal zero (no ASF correction applied).
// ---------------------------------------------------------------------------
std::string generate_po(const Scenario& scenario,
                         const GridData& /*grid_data*/,
                         FirmwareFormat  fmt)
{
    std::ostringstream ss;
    ss << "# Po commands — pattern offsets (millilanes)\n";

    if (scenario.pattern_offsets.empty()) {
        ss << "# No pattern offsets computed — run almanac export after propagation.\n";
        return ss.str();
    }

    for (const auto& po : scenario.pattern_offsets) {
        // Clip negative values to 0 for V7 format
        auto clip_v7 = [](int32_t v) -> int32_t {
            return std::max(int32_t(0), std::min(int32_t(999), v));
        };

        if (fmt == FirmwareFormat::V7) {
            ss << "po " << po.pattern
               << " " << clip_v7(po.f1plus_ml)
               << " " << clip_v7(po.f1minus_ml)
               << " " << clip_v7(po.f2plus_ml)
               << " " << clip_v7(po.f2minus_ml)
               << "\n";

            // Flag any clipped negative values
            if (po.f1plus_ml < 0 || po.f1minus_ml < 0 ||
                po.f2plus_ml < 0 || po.f2minus_ml < 0) {
                ss << "# WARNING: Negative po value clipped to 0 for pattern "
                   << po.pattern << "\n";
            }
        } else {
            // V16: signed, show explicit sign
            ss << "po " << po.pattern
               << " " << (po.f1plus_ml  >= 0 ? "+" : "") << po.f1plus_ml
               << " " << (po.f1minus_ml >= 0 ? "+" : "") << po.f1minus_ml
               << " " << (po.f2plus_ml  >= 0 ? "+" : "") << po.f2plus_ml
               << " " << (po.f2minus_ml >= 0 ? "+" : "") << po.f2minus_ml
               << "\n";
        }
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
std::string generate_almanac(const Scenario&    scenario,
                              const GridData&    grid_data,
                              FirmwareFormat     fmt,
                              const std::string& geojson_path)
{
    std::string out;
    out += make_header(scenario, fmt);
    out += "\n";
    out += generate_sg(scenario);
    out += "\n";
    out += generate_stxs(scenario);
    out += "\n";
    if (!geojson_path.empty()) {
        auto zones = compute_zone_patterns(scenario, grid_data, geojson_path);
        if (!zones.empty()) {
            out += generate_zp(zones);
            out += "\n";
        }
    }
    out += generate_po(scenario, grid_data, fmt);
    return out;
}

// ---------------------------------------------------------------------------
// Pattern offset computation from a reference point (P5-10)
// ---------------------------------------------------------------------------
std::vector<PatternOffset> compute_po_at_point(
    const Scenario& scenario,
    double          ref_lat,
    double          ref_lon,
    int             nsamples)
{
    auto cond_map    = make_conductivity_map(scenario);
    auto terrain_map = make_terrain_map(scenario);

    // Build master-slot → transmitter lookup
    std::map<int, const Transmitter*> master_by_slot;
    for (const auto& tx : scenario.transmitters)
        master_by_slot[tx.slot] = &tx;

    std::vector<PatternOffset> result;
    for (const auto& tx : scenario.transmitters) {
        if (tx.is_master) continue;           // masters don't have a po entry
        if (tx.master_slot <= 0) continue;    // no master assigned

        // Compute Monteath ASF from this slave station to the reference point
        double asf_f1 = monteath_asf_ml(
            scenario.frequencies.f1_hz,
            tx.lat, tx.lon, ref_lat, ref_lon,
            *terrain_map, *cond_map, nsamples);
        double asf_f2 = monteath_asf_ml(
            scenario.frequencies.f2_hz,
            tx.lat, tx.lon, ref_lat, ref_lon,
            *terrain_map, *cond_map, nsamples);

        // F+ and F- carry the same carrier over the same path → same ASF
        PatternOffset po;
        po.pattern     = std::to_string(tx.slot) + ","
                       + std::to_string(tx.master_slot);
        po.f1plus_ml   = static_cast<int32_t>(std::llround(asf_f1));
        po.f1minus_ml  = po.f1plus_ml;
        po.f2plus_ml   = static_cast<int32_t>(std::llround(asf_f2));
        po.f2minus_ml  = po.f2plus_ml;
        result.push_back(po);
    }
    return result;
}

std::vector<PatternOffset> compute_po_mode1(const Scenario& scenario,
                                             int nsamples)
{
    // Build slot → transmitter lookup
    std::map<int, const Transmitter*> by_slot;
    for (const auto& tx : scenario.transmitters)
        by_slot[tx.slot] = &tx;

    auto cond_map    = make_conductivity_map(scenario);
    auto terrain_map = make_terrain_map(scenario);

    std::vector<PatternOffset> result;
    for (const auto& tx : scenario.transmitters) {
        if (tx.is_master) continue;
        if (tx.master_slot <= 0) continue;

        const auto* master = by_slot.count(tx.master_slot)
                           ? by_slot.at(tx.master_slot) : nullptr;
        if (!master) continue;

        // Baseline midpoint between slave and master
        double ref_lat = 0.5 * (tx.lat + master->lat);
        double ref_lon = 0.5 * (tx.lon + master->lon);

        double asf_f1 = monteath_asf_ml(
            scenario.frequencies.f1_hz,
            tx.lat, tx.lon, ref_lat, ref_lon,
            *terrain_map, *cond_map, nsamples);
        double asf_f2 = monteath_asf_ml(
            scenario.frequencies.f2_hz,
            tx.lat, tx.lon, ref_lat, ref_lon,
            *terrain_map, *cond_map, nsamples);

        PatternOffset po;
        po.pattern     = std::to_string(tx.slot) + ","
                       + std::to_string(tx.master_slot);
        po.f1plus_ml   = static_cast<int32_t>(std::llround(asf_f1));
        po.f1minus_ml  = po.f1plus_ml;
        po.f2plus_ml   = static_cast<int32_t>(std::llround(asf_f2));
        po.f2minus_ml  = po.f2plus_ml;
        result.push_back(po);
    }
    return result;
}

} // namespace almanac
} // namespace bp
