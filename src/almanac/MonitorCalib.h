#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MonitorCalib — P5-14: Monitor station calibration import and correction
//                P5-15: Model vs measurement diagnostic
//
// Monitor stations at surveyed positions measure observed lane readings and
// report correction deltas.  BANDPASS II imports this data to:
//   1. Update stored po values:  po_new = po_current + delta_measured
//   2. Re-run absolute accuracy with corrected values (handled in asf.cpp)
//   3. Overlay corrected vs predicted as delta-error map
//
// Multi-monitor consistency check: if two monitors give corrections for the
// same pattern pair differing by > threshold_ml, flag as inconsistent.

#include "../model/Scenario.h"
#include "../model/MonitorStation.h"
#include <filesystem>
#include <string>
#include <vector>

namespace bp {
namespace almanac {

// ---------------------------------------------------------------------------
// Diagnostic report (P5-15)
// ---------------------------------------------------------------------------
struct ConsistencyIssue {
    std::string pattern;        // e.g. "3,1"
    std::string monitor1;       // name of first monitor
    std::string monitor2;       // name of second monitor
    int32_t     max_delta_ml;   // largest discrepancy (any component)
};

enum class DiagnosticFlag {
    UniformOffset,      // same correction bias on all patterns → conductivity suspect
    LocalisedAnomaly,   // one pattern has disproportionately large correction
    SuspectMonitor,     // one monitor disagrees with all others on same pattern
};

struct DiagnosticItem {
    DiagnosticFlag flag;
    std::string    monitor_or_pattern;  // which monitor or pattern triggered this
    std::string    detail;
};

struct DiagnosticReport {
    std::vector<ConsistencyIssue> inconsistencies;
    std::vector<DiagnosticItem>   items;
    std::string                   summary;  // human-readable overview
};

// ---------------------------------------------------------------------------
// Import a monitor station correction log.
//
// File format (comments with #, blank lines ignored):
//
//   # Monitor: <station_name>       (optional header fields)
//   # Lat:     <decimal_deg>
//   # Lon:     <decimal_deg>
//   # Date:    <ISO date>
//   # Columns: pattern,f1plus_ml,f1minus_ml,f2plus_ml,f2minus_ml
//   3,1,12,9,7,11
//   8,2,-5,-3,-8,-6
//
// Returns a MonitorStation with corrections populated.
// lat/lon in the file (# Lat/Lon headers) override the supplied defaults.
// Throws std::runtime_error on parse failure.
MonitorStation import_monitor_log(const std::filesystem::path& path,
                                  const std::string& station_name = "Imported",
                                  double             lat = 0.0,
                                  double             lon = 0.0);

// ---------------------------------------------------------------------------
// Apply monitor corrections to scenario.pattern_offsets.
//
//   po_new[pat] = po_current[pat] + round(mean(corrections from all monitors))
//
// If a pattern has no existing PatternOffset entry, one is created.
// Returns the updated pattern_offsets vector (does not modify the scenario).
std::vector<PatternOffset> apply_monitor_corrections(const Scenario& scenario);

// ---------------------------------------------------------------------------
// Multi-monitor consistency check + pattern-of-divergence diagnostic (P5-15).
//
// Consistency: flags patterns where any two monitors differ by > threshold_ml
// in any component.
//
// Diagnostic:
//   UniformOffset    — a monitor whose mean correction is large compared with
//                      the inter-pattern standard deviation (whole-path bias,
//                      typically wrong conductivity data for that monitor's paths).
//   LocalisedAnomaly — a pattern whose correction is > 2σ above the mean
//                      across all patterns (localised path anomaly).
//   SuspectMonitor   — a monitor that is the sole outlier for ≥ 1 pattern where
//                      other monitors agree closely (suspect survey / hardware).
DiagnosticReport check_consistency(const Scenario& scenario,
                                   int32_t         threshold_ml = 20);

} // namespace almanac
} // namespace bp
