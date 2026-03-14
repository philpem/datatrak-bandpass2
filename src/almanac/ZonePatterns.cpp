#include "ZonePatterns.h"
#include "../engine/groundwave.h"
#include "../engine/conductivity.h"
#include "../engine/noise.h"
#include "../engine/snr.h"
#include "../engine/whdop.h"
#include <nlohmann/json.hpp>
#include <GeographicLib/Geodesic.hpp>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace bp {
namespace almanac {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Parse zone centroids and names from the GeoJSON file.
// Returns a list of (zone_id, name, centroid_lat, centroid_lon) tuples.
struct ZoneDef {
    int         zone_id;
    std::string name;
    double      centroid_lat;
    double      centroid_lon;
};

// Compute the centroid of a GeoJSON polygon (first ring, ignoring holes).
static std::pair<double, double> polygon_centroid(
    const nlohmann::json& coordinates)
{
    // coordinates[0] is the outer ring: array of [lon, lat] pairs
    const auto& ring = coordinates[0];
    double sum_lat = 0.0, sum_lon = 0.0;
    int count = 0;
    for (const auto& pt : ring) {
        sum_lon += pt[0].get<double>();
        sum_lat += pt[1].get<double>();
        ++count;
    }
    if (count == 0) return {0.0, 0.0};
    // Last vertex duplicates first in closed rings — exclude it
    if (count > 1) {
        // Check if first == last
        const auto& first = ring.front();
        const auto& last  = ring.back();
        if (first[0] == last[0] && first[1] == last[1]) {
            sum_lon -= last[0].get<double>();
            sum_lat -= last[1].get<double>();
            --count;
        }
    }
    return {sum_lat / count, sum_lon / count};
}

static std::vector<ZoneDef> load_zones(const std::string& path) {
    std::vector<ZoneDef> zones;
    std::ifstream f(path);
    if (!f.is_open()) return zones;

    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return zones;
    }

    for (const auto& feature : j["features"]) {
        ZoneDef z;
        const auto& props = feature["properties"];
        z.zone_id = props["zone_id"].get<int>();
        z.name    = props["name"].get<std::string>();

        const auto& geom = feature["geometry"];
        auto [clat, clon] = polygon_centroid(geom["coordinates"]);
        z.centroid_lat = clat;
        z.centroid_lon = clon;
        zones.push_back(z);
    }

    // Sort by zone_id for deterministic output
    std::sort(zones.begin(), zones.end(), [](const ZoneDef& a, const ZoneDef& b) {
        return a.zone_id < b.zone_id;
    });
    return zones;
}

// ---------------------------------------------------------------------------
// compute_zone_patterns
// ---------------------------------------------------------------------------
std::vector<ZoneResult> compute_zone_patterns(
    const Scenario&   scenario,
    const GridData&   /*grid_data*/,
    const std::string& geojson_path)
{
    std::vector<ZoneResult> results;

    auto zones = load_zones(geojson_path);
    if (zones.empty()) return results;

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    auto cond_map = make_conductivity_map(scenario);

    const double atm_noise = atm_noise_dbuvm(scenario.frequencies.f1_hz);
    const double veh_noise = vehicle_noise_dbuvm(scenario.receiver.vehicle_noise_dbuvpm);

    for (const auto& zone : zones) {
        ZoneResult res;
        res.zone_id      = zone.zone_id;
        res.zone_name    = zone.name;
        res.centroid_lat = zone.centroid_lat;
        res.centroid_lon = zone.centroid_lon;

        // Build StationGeometry for each transmitter at this zone centroid
        std::vector<StationGeometry> stations;
        const auto flat_txs = scenario.flatTransmitters();
        stations.reserve(flat_txs.size());

        for (const auto& tx : flat_txs) {
            StationGeometry sg;
            sg.slot    = tx.slot;
            sg.lat_tx  = tx.lat;
            sg.lon_tx  = tx.lon;

            double s12, azi1, azi2;
            geod.Inverse(zone.centroid_lat, zone.centroid_lon,
                         tx.lat, tx.lon,
                         s12, azi1, azi2);
            sg.dist_km     = s12 / 1000.0;
            sg.azimuth_deg = azi1;

            if (tx.power_w > 0.0 && sg.dist_km > 0.0) {
                auto gc = cond_map->lookup(zone.centroid_lat, zone.centroid_lon);
                double gw = groundwave_field_dbuvm(
                    scenario.frequencies.f1_hz, sg.dist_km, gc, tx.power_w);
                sg.snr_db = compute_snr_db(gw, atm_noise, veh_noise);
                sg.usable = (sg.snr_db > 0.0);
            }
            stations.push_back(sg);
        }

        // Enumerate all ordered pairs (i < j) — each pair is one pattern
        struct PairResult {
            int    slot_a;
            int    slot_b;
            double whdop;
        };
        std::vector<PairResult> viable;

        for (int i = 0; i < (int)stations.size(); ++i) {
            if (!stations[i].usable) continue;
            for (int j = i + 1; j < (int)stations.size(); ++j) {
                if (!stations[j].usable) continue;
                // Build a 2-station subset for WHDOP
                std::vector<StationGeometry> pair_st = {stations[i], stations[j]};
                std::vector<int> sel;
                double whdop = compute_whdop(pair_st, 2,
                                             scenario.receiver.max_range_km, sel);
                if (whdop < 9990.0) {
                    viable.push_back({stations[i].slot, stations[j].slot, whdop});
                }
            }
        }

        // Sort by WHDOP ascending (best geometry first)
        std::sort(viable.begin(), viable.end(), [](const PairResult& a, const PairResult& b) {
            return a.whdop < b.whdop;
        });

        res.viable_count = (int)viable.size();
        res.is_gap       = (res.viable_count < 3);

        auto make_pat = [](const PairResult& p) -> std::string {
            return std::to_string(p.slot_a) + "," + std::to_string(p.slot_b);
        };

        if (viable.size() >= 1) res.set1 = make_pat(viable[0]);
        if (viable.size() >= 2) res.set2 = make_pat(viable[1]);
        if (viable.size() >= 3) res.set3 = make_pat(viable[2]);
        if (viable.size() >= 4) res.set4 = make_pat(viable[3]);
        // set4 stays "0,0" if fewer than 4 viable pairs

        results.push_back(res);
    }

    return results;
}

// ---------------------------------------------------------------------------
// generate_zp
// ---------------------------------------------------------------------------
std::string generate_zp(const std::vector<ZoneResult>& zones) {
    std::ostringstream ss;
    ss << "# Zp commands — zone patterns\n";
    ss << "# Format: Zp zone set pat1,pat2  (slot numbers)\n";
    ss << "# Zones flagged as coverage gaps have fewer than 3 viable patterns.\n";

    for (const auto& z : zones) {
        if (z.is_gap) {
            ss << "# Zone " << z.zone_id << " (" << z.zone_name
               << ") — COVERAGE GAP (" << z.viable_count << " viable pairs)\n";
        }
        // Output up to 4 sets
        auto emit = [&](int set_num, const std::string& pat) {
            if (pat == "0,0" && set_num > z.viable_count) return;
            ss << "Zp " << z.zone_id << " " << set_num << " " << pat << "\n";
        };
        emit(1, z.set1);
        emit(2, z.set2);
        emit(3, z.set3);
        emit(4, z.set4);
    }
    return ss.str();
}

} // namespace almanac
} // namespace bp
