#pragma once
#include <string>

namespace bp {

struct Transmitter {
    std::string name;
    double lat  = 0.0;
    double lon  = 0.0;
    // Informational OSGB easting/northing (computed from lat/lon, not stored in TOML as canonical)
    double osgb_easting  = 0.0;
    double osgb_northing = 0.0;
    double power_w          = 40.0;
    double height_m         = 50.0;
    int    slot             = 1;     // 1–24
    bool   is_master        = false;
    int    master_slot      = 0;     // 0 = none
    double spo_us           = 0.0;
    double station_delay_us = 0.0;
};

} // namespace bp
