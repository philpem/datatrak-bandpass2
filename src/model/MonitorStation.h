#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace bp {

struct MonitorStation {
    std::string name;
    double lat          = 0.0;
    double lon          = 0.0;
    double osgb_easting  = 0.0;
    double osgb_northing = 0.0;

    struct Correction {
        std::string pattern;
        int32_t f1plus_ml  = 0;
        int32_t f1minus_ml = 0;
        int32_t f2plus_ml  = 0;
        int32_t f2minus_ml = 0;
    };
    std::vector<Correction> corrections;
};

} // namespace bp
