#pragma once

namespace bp {

struct ReceiverModel {
    enum class Mode { Simple, Advanced };
    Mode mode = Mode::Simple;

    double noise_floor_dbuvpm   = 14.0;
    double vehicle_noise_dbuvpm = 27.0;
    double max_range_km         = 350.0;
    int    min_stations         = 4;
    double vp_ms                = 299'300'000.0;  // velocity of propagation, m/s (Datatrak firmware)

    enum class Ellipsoid { Airy1830, WGS84 };
    Ellipsoid ellipsoid = Ellipsoid::Airy1830;
};

} // namespace bp
