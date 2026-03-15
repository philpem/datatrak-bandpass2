#pragma once

namespace bp {

struct ReceiverModel {
    enum class Mode { Simple, Advanced };
    Mode mode = Mode::Simple;

    double noise_floor_dbuvpm   = 14.0;
    double vehicle_noise_dbuvpm = 27.0;
    double max_range_km         = 350.0;
    int    min_stations         = 4;
    double vp_ms                = 299'702'547.0;  // velocity of propagation, m/s (~99.97% of c)

    // Speed of light in vacuum (m/s) — used for VP percent-of-c conversions
    static constexpr double C_VACUUM = 299'792'458.0;

    double vp_percent_c() const { return (vp_ms / C_VACUUM) * 100.0; }
    void set_vp_from_percent_c(double pct) { vp_ms = (pct / 100.0) * C_VACUUM; }

    enum class Ellipsoid { Airy1830, WGS84 };
    Ellipsoid ellipsoid = Ellipsoid::Airy1830;
};

} // namespace bp
