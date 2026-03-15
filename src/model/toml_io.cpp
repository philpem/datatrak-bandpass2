#include "toml_io.h"
#include "DataPaths.h"
#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>

namespace bp {
namespace toml_io {

static std::string str(toml::node_view<toml::node> v,
                       const std::string& def = "") {
    if (auto s = v.value<std::string>()) return *s;
    return def;
}

Scenario load(const std::filesystem::path& path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(std::string("TOML parse error: ") + e.what());
    }

    Scenario s;

    // [scenario]
    if (auto sc = tbl["scenario"].as_table()) {
        s.name    = str((*sc)["name"],    "Untitled");
        s.created = str((*sc)["created"], "");
    }

    // [grid]
    if (auto g = tbl["grid"].as_table()) {
        if (auto v = (*g)["lat_min"].value<double>())       s.grid.lat_min       = *v;
        if (auto v = (*g)["lat_max"].value<double>())       s.grid.lat_max       = *v;
        if (auto v = (*g)["lon_min"].value<double>())       s.grid.lon_min       = *v;
        if (auto v = (*g)["lon_max"].value<double>())       s.grid.lon_max       = *v;
        if (auto v = (*g)["resolution_km"].value<double>()) s.grid.resolution_km = *v;
    }

    // [frequencies] — stored as kHz, converted to Hz
    if (auto f = tbl["frequencies"].as_table()) {
        if (auto v = (*f)["f1_khz"].value<double>()) s.frequencies.f1_hz = *v * 1000.0;
        if (auto v = (*f)["f2_khz"].value<double>()) s.frequencies.f2_hz = *v * 1000.0;
    }
    s.frequencies.recompute();

    // [receiver]
    if (auto r = tbl["receiver"].as_table()) {
        std::string mode = str((*r)["mode"], "simple");
        s.receiver.mode = (mode == "advanced") ? ReceiverModel::Mode::Advanced
                                               : ReceiverModel::Mode::Simple;
        if (auto v = (*r)["noise_floor_dbuvpm"].value<double>())   s.receiver.noise_floor_dbuvpm   = *v;
        if (auto v = (*r)["vehicle_noise_dbuvpm"].value<double>())  s.receiver.vehicle_noise_dbuvpm = *v;
        if (auto v = (*r)["max_range_km"].value<double>())          s.receiver.max_range_km         = *v;
        if (auto v = (*r)["min_stations"].value<int64_t>())         s.receiver.min_stations         = (int)*v;
        if (auto v = (*r)["vp_ms"].value<double>())                 s.receiver.vp_ms                = *v;
        std::string ell = str((*r)["ellipsoid"], "airy1830");
        s.receiver.ellipsoid = (ell == "wgs84") ? ReceiverModel::Ellipsoid::WGS84
                                                : ReceiverModel::Ellipsoid::Airy1830;
    }

    // [[transmitter_sites]]  (current format)
    if (auto arr = tbl["transmitter_sites"].as_array()) {
        for (auto& elem : *arr) {
            if (auto t = elem.as_table()) {
                TransmitterSite site;
                site.name = str((*t)["name"], "");
                if (auto v = (*t)["lat"].value<double>())      site.lat      = *v;
                if (auto v = (*t)["lon"].value<double>())      site.lon      = *v;
                if (auto v = (*t)["power_w"].value<double>())  site.power_w  = *v;
                if (auto v = (*t)["height_m"].value<double>()) site.height_m = *v;
                if (auto v = (*t)["locked"].value<bool>())     site.locked   = *v;
                if (auto slots_arr = (*t)["slots"].as_array()) {
                    for (auto& se : *slots_arr) {
                        if (auto st = se.as_table()) {
                            SlotConfig sc;
                            if (auto v = (*st)["slot"].value<int64_t>())            sc.slot             = (int)*v;
                            if (auto v = (*st)["is_master"].value<bool>())          sc.is_master        = *v;
                            if (auto v = (*st)["master_slot"].value<int64_t>())     sc.master_slot      = (int)*v;
                            if (auto v = (*st)["spo_us"].value<double>())           sc.spo_us           = *v;
                            if (auto v = (*st)["station_delay_us"].value<double>()) sc.station_delay_us = *v;
                            site.slots.push_back(sc);
                        }
                    }
                }
                s.transmitter_sites.push_back(site);
            }
        }
    }
    // [[transmitters]]  (legacy format — migrate each entry to a single-slot site)
    else if (auto arr = tbl["transmitters"].as_array()) {
        for (auto& elem : *arr) {
            if (auto t = elem.as_table()) {
                TransmitterSite site;
                site.name = str((*t)["name"], "");
                if (auto v = (*t)["lat"].value<double>())      site.lat      = *v;
                if (auto v = (*t)["lon"].value<double>())      site.lon      = *v;
                if (auto v = (*t)["power_w"].value<double>())  site.power_w  = *v;
                if (auto v = (*t)["height_m"].value<double>()) site.height_m = *v;
                if (auto v = (*t)["locked"].value<bool>())     site.locked   = *v;
                SlotConfig sc;
                if (auto v = (*t)["slot"].value<int64_t>())            sc.slot             = (int)*v;
                if (auto v = (*t)["is_master"].value<bool>())          sc.is_master        = *v;
                if (auto v = (*t)["master_slot"].value<int64_t>())     sc.master_slot      = (int)*v;
                if (auto v = (*t)["spo_us"].value<double>())           sc.spo_us           = *v;
                if (auto v = (*t)["station_delay_us"].value<double>()) sc.station_delay_us = *v;
                site.slots.push_back(sc);
                s.transmitter_sites.push_back(site);
            }
        }
    }

    // [[monitor_stations]]
    if (auto arr = tbl["monitor_stations"].as_array()) {
        for (auto& elem : *arr) {
            if (auto ms = elem.as_table()) {
                MonitorStation m;
                m.name = str((*ms)["name"], "");
                if (auto v = (*ms)["lat"].value<double>()) m.lat = *v;
                if (auto v = (*ms)["lon"].value<double>()) m.lon = *v;
                if (auto corrs = (*ms)["corrections"].as_array()) {
                    for (auto& ce : *corrs) {
                        if (auto ct = ce.as_table()) {
                            MonitorStation::Correction c;
                            c.pattern = str((*ct)["pattern"], "");
                            if (auto v = (*ct)["f1plus_ml"].value<int64_t>())  c.f1plus_ml  = (int32_t)*v;
                            if (auto v = (*ct)["f1minus_ml"].value<int64_t>()) c.f1minus_ml = (int32_t)*v;
                            if (auto v = (*ct)["f2plus_ml"].value<int64_t>())  c.f2plus_ml  = (int32_t)*v;
                            if (auto v = (*ct)["f2minus_ml"].value<int64_t>()) c.f2minus_ml = (int32_t)*v;
                            m.corrections.push_back(c);
                        }
                    }
                }
                s.monitor_stations.push_back(m);
            }
        }
    }

    // [[pattern_offsets]]
    if (auto arr = tbl["pattern_offsets"].as_array()) {
        for (auto& elem : *arr) {
            if (auto po = elem.as_table()) {
                PatternOffset p;
                p.pattern = str((*po)["pattern"], "");
                if (auto v = (*po)["f1plus_ml"].value<int64_t>())  p.f1plus_ml  = (int32_t)*v;
                if (auto v = (*po)["f1minus_ml"].value<int64_t>()) p.f1minus_ml = (int32_t)*v;
                if (auto v = (*po)["f2plus_ml"].value<int64_t>())  p.f2plus_ml  = (int32_t)*v;
                if (auto v = (*po)["f2minus_ml"].value<int64_t>()) p.f2minus_ml = (int32_t)*v;
                s.pattern_offsets.push_back(p);
            }
        }
    }

    // [conductivity]
    if (auto c = tbl["conductivity"].as_table()) {
        std::string src = str((*c)["source"], "builtin");
        if      (src == "builtin")  s.conductivity_source = Scenario::ConductivitySource::BuiltIn;
        else if (src == "itu_p832") {
            s.conductivity_source = Scenario::ConductivitySource::ItuP832;
            s.conductivity_file   = resolve_data_path("conductivity_p832.tif");
        } else if (src == "bgs") {
            s.conductivity_source = Scenario::ConductivitySource::BGS;
            s.conductivity_file   = resolve_data_path("conductivity_bgs.tif");
        } else {
            // Anything else is a file path (absolute or relative)
            s.conductivity_source = Scenario::ConductivitySource::File;
            s.conductivity_file   = resolve_data_path(src);
        }
    }

    // [terrain]
    if (auto t = tbl["terrain"].as_table()) {
        std::string src = str((*t)["source"], "flat");
        if (src == "flat") {
            s.terrain_source = Scenario::TerrainSource::Flat;
        } else if (src == "srtm") {
            // Legacy SRTM mode — read tile_dir and resolve it
            s.terrain_source = Scenario::TerrainSource::File;
            std::string tile_dir = str((*t)["tile_dir"], "");
            s.terrain_file = tile_dir.empty() ? "" : resolve_data_dir(tile_dir);
        } else {
            // File path (absolute or relative)
            s.terrain_source = Scenario::TerrainSource::File;
            s.terrain_file   = resolve_data_path(src);
        }
    }


    // [propagation]
    if (auto p = tbl["propagation"].as_table()) {
        std::string mdl = str((*p)["model"], "millington");
        if      (mdl == "homogeneous") s.propagation_model = Scenario::PropagationModel::Homogeneous;
        else if (mdl == "grwave")      s.propagation_model = Scenario::PropagationModel::GRWAVE;
        else                           s.propagation_model = Scenario::PropagationModel::Millington;
        if (auto v = (*p)["precompute_airy_cache"].value<bool>())
            s.precompute_airy_cache = *v;
    }

    return s;
}

void save(const Scenario& s, const std::filesystem::path& path) {
    auto tbl = toml::table{
        {"scenario", toml::table{
            {"name",        s.name},
            {"created",     s.created},
        }},
        {"grid", toml::table{
            {"lat_min",       s.grid.lat_min},
            {"lat_max",       s.grid.lat_max},
            {"lon_min",       s.grid.lon_min},
            {"lon_max",       s.grid.lon_max},
            {"resolution_km", s.grid.resolution_km},
        }},
        {"frequencies", toml::table{
            {"f1_khz", s.frequencies.f1_hz / 1000.0},
            {"f2_khz", s.frequencies.f2_hz / 1000.0},
        }},
        {"receiver", toml::table{
            {"mode",                   (s.receiver.mode == ReceiverModel::Mode::Advanced) ? "advanced" : "simple"},
            {"noise_floor_dbuvpm",     s.receiver.noise_floor_dbuvpm},
            {"vehicle_noise_dbuvpm",   s.receiver.vehicle_noise_dbuvpm},
            {"max_range_km",           s.receiver.max_range_km},
            {"min_stations",           (int64_t)s.receiver.min_stations},
            {"vp_ms",                  s.receiver.vp_ms},
            {"ellipsoid",              (s.receiver.ellipsoid == ReceiverModel::Ellipsoid::WGS84) ? "wgs84" : "airy1830"},
        }},
    };

    // Transmitter sites as array of tables
    auto tx_arr = toml::array{};
    for (const auto& site : s.transmitter_sites) {
        auto slots_arr = toml::array{};
        for (const auto& sc : site.slots) {
            slots_arr.push_back(toml::table{
                {"slot",             (int64_t)sc.slot},
                {"is_master",        sc.is_master},
                {"master_slot",      (int64_t)sc.master_slot},
                {"spo_us",           sc.spo_us},
                {"station_delay_us", sc.station_delay_us},
            });
        }
        auto site_tbl = toml::table{
            {"name",     site.name},
            {"lat",      site.lat},
            {"lon",      site.lon},
            {"power_w",  site.power_w},
            {"height_m", site.height_m},
            {"locked",   site.locked},
        };
        site_tbl.insert("slots", slots_arr);
        tx_arr.push_back(site_tbl);
    }
    tbl.insert("transmitter_sites", tx_arr);

    // Monitor stations
    auto ms_arr = toml::array{};
    for (const auto& ms : s.monitor_stations) {
        auto ms_tbl = toml::table{
            {"name", ms.name},
            {"lat",  ms.lat},
            {"lon",  ms.lon},
        };
        auto corr_arr = toml::array{};
        for (const auto& c : ms.corrections) {
            corr_arr.push_back(toml::table{
                {"pattern",    c.pattern},
                {"f1plus_ml",  (int64_t)c.f1plus_ml},
                {"f1minus_ml", (int64_t)c.f1minus_ml},
                {"f2plus_ml",  (int64_t)c.f2plus_ml},
                {"f2minus_ml", (int64_t)c.f2minus_ml},
            });
        }
        ms_tbl.insert("corrections", corr_arr);
        ms_arr.push_back(ms_tbl);
    }
    tbl.insert("monitor_stations", ms_arr);

    // Pattern offsets
    auto po_arr = toml::array{};
    for (const auto& po : s.pattern_offsets) {
        po_arr.push_back(toml::table{
            {"pattern",    po.pattern},
            {"f1plus_ml",  (int64_t)po.f1plus_ml},
            {"f1minus_ml", (int64_t)po.f1minus_ml},
            {"f2plus_ml",  (int64_t)po.f2plus_ml},
            {"f2minus_ml", (int64_t)po.f2minus_ml},
        });
    }
    tbl.insert("pattern_offsets", po_arr);

    // Conductivity / terrain — relativize paths for portability
    std::string cond_src = "builtin";
    if      (s.conductivity_source == Scenario::ConductivitySource::ItuP832) cond_src = "itu_p832";
    else if (s.conductivity_source == Scenario::ConductivitySource::BGS)     cond_src = "bgs";
    else if (s.conductivity_source == Scenario::ConductivitySource::File)
        cond_src = make_relative_data_path(s.conductivity_file);
    tbl.insert("conductivity", toml::table{{"source", cond_src}});

    std::string terr_src = "flat";
    if (s.terrain_source == Scenario::TerrainSource::File)
        terr_src = make_relative_data_path(s.terrain_file);
    tbl.insert("terrain", toml::table{{"source", terr_src}});

    // Propagation model
    std::string prop_mdl = (s.propagation_model == Scenario::PropagationModel::GRWAVE)
                           ? "grwave"
                           : (s.propagation_model == Scenario::PropagationModel::Homogeneous)
                           ? "homogeneous" : "millington";
    auto prop_tbl = toml::table{{"model", prop_mdl}};
    if (!s.precompute_airy_cache)
        prop_tbl.insert("precompute_airy_cache", false);
    tbl.insert("propagation", prop_tbl);

    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot open file for writing: " + path.string());
    f << tbl;
}

} // namespace toml_io
} // namespace bp
