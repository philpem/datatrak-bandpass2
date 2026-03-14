#include "toml_io.h"
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
        std::string crs = str((*sc)["display_crs"], "osgb_ng");
        s.display_crs = (crs == "wgs84") ? Scenario::DisplayCRS::WGS84
                                         : Scenario::DisplayCRS::OSGB_NG;
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

    // [[transmitters]]
    if (auto arr = tbl["transmitters"].as_array()) {
        for (auto& elem : *arr) {
            if (auto t = elem.as_table()) {
                Transmitter tx;
                tx.name             = str((*t)["name"], "");
                if (auto v = (*t)["lat"].value<double>())              tx.lat              = *v;
                if (auto v = (*t)["lon"].value<double>())              tx.lon              = *v;
                if (auto v = (*t)["power_w"].value<double>())          tx.power_w          = *v;
                if (auto v = (*t)["height_m"].value<double>())         tx.height_m         = *v;
                if (auto v = (*t)["slot"].value<int64_t>())            tx.slot             = (int)*v;
                if (auto v = (*t)["is_master"].value<bool>())          tx.is_master        = *v;
                if (auto v = (*t)["master_slot"].value<int64_t>())     tx.master_slot      = (int)*v;
                if (auto v = (*t)["spo_us"].value<double>())           tx.spo_us           = *v;
                if (auto v = (*t)["station_delay_us"].value<double>()) tx.station_delay_us = *v;
                s.transmitters.push_back(tx);
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
        if      (src == "itu_p832") s.conductivity_source = Scenario::ConductivitySource::ItuP832;
        else if (src == "bgs")      s.conductivity_source = Scenario::ConductivitySource::BGS;
        else if (src.size() > 1 && src[0] == '/') {
            s.conductivity_source = Scenario::ConductivitySource::File;
            s.conductivity_file   = src;
        } else {
            s.conductivity_source = Scenario::ConductivitySource::BuiltIn;
        }
    }

    // [terrain]
    if (auto t = tbl["terrain"].as_table()) {
        std::string src = str((*t)["source"], "flat");
        if      (src == "srtm")      s.terrain_source = Scenario::TerrainSource::SRTM;
        else if (src.size() > 1 && src[0] == '/')  {
            s.terrain_source = Scenario::TerrainSource::File;
            s.terrain_file   = src;
        } else {
            s.terrain_source = Scenario::TerrainSource::Flat;
        }
    }

    // [datum]
    if (auto d = tbl["datum"].as_table()) {
        std::string tr = str((*d)["transform"], "helmert");
        s.datum_transform = (tr == "ostn15") ? Scenario::DatumTransform::OSTN15
                                             : Scenario::DatumTransform::Helmert;
    }

    return s;
}

void save(const Scenario& s, const std::filesystem::path& path) {
    auto tbl = toml::table{
        {"scenario", toml::table{
            {"name",        s.name},
            {"created",     s.created},
            {"display_crs", (s.display_crs == Scenario::DisplayCRS::WGS84) ? "wgs84" : "osgb_ng"},
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
        {"datum", toml::table{
            {"transform", (s.datum_transform == Scenario::DatumTransform::OSTN15) ? "ostn15" : "helmert"},
        }},
    };

    // Transmitters as array of tables
    auto tx_arr = toml::array{};
    for (const auto& tx : s.transmitters) {
        tx_arr.push_back(toml::table{
            {"name",             tx.name},
            {"lat",              tx.lat},
            {"lon",              tx.lon},
            {"power_w",          tx.power_w},
            {"height_m",         tx.height_m},
            {"slot",             (int64_t)tx.slot},
            {"is_master",        tx.is_master},
            {"master_slot",      (int64_t)tx.master_slot},
            {"spo_us",           tx.spo_us},
            {"station_delay_us", tx.station_delay_us},
        });
    }
    tbl.insert("transmitters", tx_arr);

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

    // Conductivity / terrain
    std::string cond_src = "builtin";
    if      (s.conductivity_source == Scenario::ConductivitySource::ItuP832) cond_src = "itu_p832";
    else if (s.conductivity_source == Scenario::ConductivitySource::BGS)     cond_src = "bgs";
    else if (s.conductivity_source == Scenario::ConductivitySource::File)    cond_src = s.conductivity_file;
    tbl.insert("conductivity", toml::table{{"source", cond_src}});

    std::string terr_src = "flat";
    if      (s.terrain_source == Scenario::TerrainSource::SRTM) terr_src = "srtm";
    else if (s.terrain_source == Scenario::TerrainSource::File) terr_src = s.terrain_file;
    tbl.insert("terrain", toml::table{{"source", terr_src}});

    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot open file for writing: " + path.string());
    f << tbl;
}

} // namespace toml_io
} // namespace bp
