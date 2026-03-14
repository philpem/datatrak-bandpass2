#pragma once

// Unicode character constants for UI labels (UTF-8 encoded).
// Use with wxString::FromUTF8() when building wxString values.
//
// Do not use raw hex escape sequences (\x..) for Unicode characters.
// Define new constants here if additional symbols are needed.

namespace bp::ui {
    inline constexpr const char DEGREE[]   = u8"\u00B0";   // degree sign
    inline constexpr const char MICRO[]    = u8"\u00B5";   // micro sign (SI prefix)
    inline constexpr const char SIGMA[]    = u8"\u03C3";   // Greek small sigma

    // Pre-composed unit strings used in multiple places
    inline constexpr const char DBUVM[]    = u8"dB\u00B5V/m";  // field strength unit
    inline constexpr const char MICROSEC[] = u8"\u00B5s";       // microseconds
}
