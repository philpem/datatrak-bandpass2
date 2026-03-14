// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonitorCalib.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <map>
#include <set>
#include <algorithm>
#include <numeric>

namespace bp {
namespace almanac {

// ---------------------------------------------------------------------------
// import_monitor_log
// ---------------------------------------------------------------------------
MonitorStation import_monitor_log(const std::filesystem::path& path,
                                  const std::string& station_name,
                                  double lat, double lon)
{
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open monitor log: " + path.string());

    MonitorStation ms;
    ms.name = station_name;
    ms.lat  = lat;
    ms.lon  = lon;

    std::string line;
    int line_no = 0;
    while (std::getline(f, line)) {
        ++line_no;
        // Trim
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line[0] == '#') {
            // Optional header fields:  # Key: value
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(1, colon - 1);
                std::string val = line.substr(colon + 1);
                // Trim key and val
                auto trim = [](std::string& s) {
                    size_t a = s.find_first_not_of(" \t");
                    size_t b = s.find_last_not_of(" \t\r\n");
                    s = (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
                };
                trim(key);
                trim(val);
                if (key == "Monitor" || key == "Station") ms.name = val;
                else if (key == "Lat")  try { ms.lat = std::stod(val); } catch (...) {}
                else if (key == "Lon")  try { ms.lon = std::stod(val); } catch (...) {}
            }
            continue;
        }

        // Data line: pattern,f1plus_ml,f1minus_ml,f2plus_ml,f2minus_ml
        // pattern may be "slot1,slot2" (contains comma), so parse carefully:
        // Expected: 5 comma-separated fields, where field[0] = "X,Y" or just "X"
        // Re-join and split on comma:
        std::vector<std::string> fields;
        std::istringstream ss(line);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            size_t a = tok.find_first_not_of(" \t");
            size_t b = tok.find_last_not_of(" \t\r\n");
            fields.push_back((a == std::string::npos) ? "" : tok.substr(a, b - a + 1));
        }

        // Need exactly 6 fields: slot1, slot2, f1plus, f1minus, f2plus, f2minus
        // OR 5 fields if pattern is given as a single token like "3,1" (already split)
        // After CSV split we have: ["3","1","12","9","7","11"] — 6 tokens
        // or ["3,1","12","9","7","11"] wouldn't happen because we split on comma
        if (fields.size() != 6) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_no)
                + ": expected 6 comma-separated fields (slot1,slot2,f1p,f1m,f2p,f2m), got "
                + std::to_string(fields.size()));
        }

        MonitorStation::Correction c;
        c.pattern = fields[0] + "," + fields[1];
        try {
            c.f1plus_ml  = (int32_t)std::stoi(fields[2]);
            c.f1minus_ml = (int32_t)std::stoi(fields[3]);
            c.f2plus_ml  = (int32_t)std::stoi(fields[4]);
            c.f2minus_ml = (int32_t)std::stoi(fields[5]);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                path.string() + ":" + std::to_string(line_no)
                + ": integer parse error: " + e.what());
        }
        ms.corrections.push_back(c);
    }

    return ms;
}

// ---------------------------------------------------------------------------
// apply_monitor_corrections
//
// For each pattern that appears in any monitor's corrections:
//   1. Collect all correction deltas from all monitors
//   2. Average them (integer round)
//   3. po_new = po_current + mean_delta
// ---------------------------------------------------------------------------
std::vector<PatternOffset> apply_monitor_corrections(const Scenario& scenario)
{
    // Start from existing pattern_offsets
    std::map<std::string, PatternOffset> po_map;
    for (const auto& po : scenario.pattern_offsets)
        po_map[po.pattern] = po;

    // Collect corrections per pattern across all monitors
    // map: pattern → {sum_f1p, sum_f1m, sum_f2p, sum_f2m, count}
    struct Accum { int64_t f1p=0, f1m=0, f2p=0, f2m=0; int n=0; };
    std::map<std::string, Accum> acc;

    for (const auto& ms : scenario.monitor_stations) {
        for (const auto& c : ms.corrections) {
            auto& a = acc[c.pattern];
            a.f1p += c.f1plus_ml;
            a.f1m += c.f1minus_ml;
            a.f2p += c.f2plus_ml;
            a.f2m += c.f2minus_ml;
            ++a.n;
        }
    }

    // Apply mean corrections
    for (auto& [pat, a] : acc) {
        if (a.n == 0) continue;
        auto& po = po_map[pat];  // creates entry if absent
        po.pattern    = pat;
        po.f1plus_ml  += (int32_t)std::llround((double)a.f1p / a.n);
        po.f1minus_ml += (int32_t)std::llround((double)a.f1m / a.n);
        po.f2plus_ml  += (int32_t)std::llround((double)a.f2p / a.n);
        po.f2minus_ml += (int32_t)std::llround((double)a.f2m / a.n);
    }

    // Return as vector in original insertion order + new patterns appended
    std::vector<PatternOffset> result;
    std::set<std::string> seen;
    for (const auto& orig : scenario.pattern_offsets) {
        result.push_back(po_map[orig.pattern]);
        seen.insert(orig.pattern);
    }
    for (auto& [pat, po] : po_map) {
        if (seen.find(pat) == seen.end())
            result.push_back(po);
    }
    return result;
}

// ---------------------------------------------------------------------------
// check_consistency
// ---------------------------------------------------------------------------
DiagnosticReport check_consistency(const Scenario& scenario, int32_t threshold_ml)
{
    DiagnosticReport report;

    const auto& monitors = scenario.monitor_stations;
    if (monitors.size() < 2) {
        report.summary = "Only one monitor station — no cross-monitor consistency check possible.";
        if (monitors.size() == 1 && !monitors[0].corrections.empty())
            report.summary += " " + std::to_string(monitors[0].corrections.size())
                            + " correction(s) from " + monitors[0].name + ".";
        return report;
    }

    // Map: pattern → list of (monitor_idx, correction)
    using CorrList = std::vector<std::pair<size_t, MonitorStation::Correction>>;
    std::map<std::string, CorrList> by_pattern;
    for (size_t mi = 0; mi < monitors.size(); ++mi)
        for (const auto& c : monitors[mi].corrections)
            by_pattern[c.pattern].emplace_back(mi, c);

    // Per-monitor: collect all correction magnitudes for uniform-offset check
    // map: monitor_idx → list of |mean component| corrections
    std::map<size_t, std::vector<double>> monitor_corr_mag;

    // Per-pattern: collect mean correction magnitudes for anomaly check
    std::vector<double> pattern_mean_mags;
    std::vector<std::string> pattern_names;

    for (auto& [pat, list] : by_pattern) {
        if (list.empty()) continue;

        // Compute mean correction for this pattern
        double mean_f1p = 0, mean_f1m = 0, mean_f2p = 0, mean_f2m = 0;
        for (auto& [mi, c] : list) {
            mean_f1p += c.f1plus_ml;
            mean_f1m += c.f1minus_ml;
            mean_f2p += c.f2plus_ml;
            mean_f2m += c.f2minus_ml;
        }
        double n = (double)list.size();
        mean_f1p /= n; mean_f1m /= n; mean_f2p /= n; mean_f2m /= n;
        double mean_mag = (std::abs(mean_f1p) + std::abs(mean_f1m)
                         + std::abs(mean_f2p) + std::abs(mean_f2m)) / 4.0;
        pattern_mean_mags.push_back(mean_mag);
        pattern_names.push_back(pat);

        // Check all pairs of monitors for this pattern
        for (size_t a = 0; a < list.size(); ++a) {
            for (size_t b = a + 1; b < list.size(); ++b) {
                const auto& ca = list[a].second;
                const auto& cb = list[b].second;
                int32_t d1 = std::abs(ca.f1plus_ml  - cb.f1plus_ml);
                int32_t d2 = std::abs(ca.f1minus_ml - cb.f1minus_ml);
                int32_t d3 = std::abs(ca.f2plus_ml  - cb.f2plus_ml);
                int32_t d4 = std::abs(ca.f2minus_ml - cb.f2minus_ml);
                int32_t max_d = std::max({d1, d2, d3, d4});
                if (max_d > threshold_ml) {
                    ConsistencyIssue issue;
                    issue.pattern     = pat;
                    issue.monitor1    = monitors[list[a].first].name;
                    issue.monitor2    = monitors[list[b].first].name;
                    issue.max_delta_ml = max_d;
                    report.inconsistencies.push_back(issue);
                }
            }

            // Accumulate per-monitor magnitude (deviation from pattern mean)
            const auto& c = list[a].second;
            double dev = (std::abs(c.f1plus_ml  - mean_f1p)
                        + std::abs(c.f1minus_ml - mean_f1m)
                        + std::abs(c.f2plus_ml  - mean_f2p)
                        + std::abs(c.f2minus_ml - mean_f2m)) / 4.0;
            monitor_corr_mag[list[a].first].push_back(dev);
        }
        // Also account for single-monitor patterns
        if (list.size() == 1) {
            size_t mi = list[0].first;
            double self_mag = (std::abs(list[0].second.f1plus_ml)
                             + std::abs(list[0].second.f1minus_ml)
                             + std::abs(list[0].second.f2plus_ml)
                             + std::abs(list[0].second.f2minus_ml)) / 4.0;
            monitor_corr_mag[mi].push_back(self_mag);
        }
    }

    // Localised anomaly: patterns where mean_mag > mean + 2*std
    if (pattern_mean_mags.size() > 2) {
        double global_mean = std::accumulate(pattern_mean_mags.begin(),
                                             pattern_mean_mags.end(), 0.0)
                           / (double)pattern_mean_mags.size();
        double var = 0.0;
        for (double v : pattern_mean_mags)
            var += (v - global_mean) * (v - global_mean);
        double std_dev = std::sqrt(var / (double)pattern_mean_mags.size());

        for (size_t pi = 0; pi < pattern_mean_mags.size(); ++pi) {
            if (pattern_mean_mags[pi] > global_mean + 2.0 * std_dev) {
                DiagnosticItem item;
                item.flag = DiagnosticFlag::LocalisedAnomaly;
                item.monitor_or_pattern = pattern_names[pi];
                item.detail = "Pattern " + pattern_names[pi]
                    + ": mean correction " + std::to_string((int)pattern_mean_mags[pi])
                    + " ml is > 2σ above the network mean ("
                    + std::to_string((int)global_mean) + " ± "
                    + std::to_string((int)std_dev) + " ml). "
                    "Possible localised path anomaly between those stations.";
                report.items.push_back(item);
            }
        }
    }

    // Uniform offset: monitor where mean deviation across all patterns is large
    // relative to inter-pattern std (suggests a systematic bias on all paths)
    for (auto& [mi, devs] : monitor_corr_mag) {
        if (devs.empty()) continue;
        double mean_dev = std::accumulate(devs.begin(), devs.end(), 0.0)
                        / (double)devs.size();
        // Heuristic: flag if mean deviation > 30 ml (adjustable)
        if (mean_dev > 30.0) {
            DiagnosticItem item;
            item.flag = DiagnosticFlag::UniformOffset;
            item.monitor_or_pattern = monitors[mi].name;
            item.detail = monitors[mi].name
                + ": mean correction deviation = " + std::to_string((int)mean_dev)
                + " ml across all patterns. "
                "Uniform bias suggests wrong conductivity data for this monitor's paths.";
            report.items.push_back(item);
        }
    }

    // Suspect monitor: a monitor that is the sole outlier on ≥ 1 pattern
    // (other monitors all agree but this one disagrees by > threshold)
    std::map<size_t, int> suspect_count;
    for (auto& [pat, list] : by_pattern) {
        if (list.size() < 3) continue;  // need at least 3 monitors to identify outlier
        // For each monitor in this pattern: compute its deviation from the others' mean
        for (size_t a = 0; a < list.size(); ++a) {
            double sum_f1p = 0, sum_f1m = 0, sum_f2p = 0, sum_f2m = 0;
            int cnt = 0;
            for (size_t b = 0; b < list.size(); ++b) {
                if (b == a) continue;
                sum_f1p += list[b].second.f1plus_ml;
                sum_f1m += list[b].second.f1minus_ml;
                sum_f2p += list[b].second.f2plus_ml;
                sum_f2m += list[b].second.f2minus_ml;
                ++cnt;
            }
            if (cnt == 0) continue;
            double d = (std::abs(list[a].second.f1plus_ml  - sum_f1p / cnt)
                      + std::abs(list[a].second.f1minus_ml - sum_f1m / cnt)
                      + std::abs(list[a].second.f2plus_ml  - sum_f2p / cnt)
                      + std::abs(list[a].second.f2minus_ml - sum_f2m / cnt)) / 4.0;
            if (d > threshold_ml)
                suspect_count[list[a].first]++;
        }
    }
    for (auto& [mi, cnt] : suspect_count) {
        DiagnosticItem item;
        item.flag = DiagnosticFlag::SuspectMonitor;
        item.monitor_or_pattern = monitors[mi].name;
        item.detail = monitors[mi].name
            + " is the sole outlier on " + std::to_string(cnt) + " pattern(s). "
            "Check this monitor's survey position and hardware.";
        report.items.push_back(item);
    }

    // Build summary
    std::ostringstream ss;
    ss << monitors.size() << " monitor station(s), "
       << by_pattern.size() << " pattern(s) covered. ";
    if (report.inconsistencies.empty()) {
        ss << "All consistency checks passed (threshold " << threshold_ml << " ml).";
    } else {
        ss << report.inconsistencies.size() << " inconsistency/ies found "
           << "(threshold " << threshold_ml << " ml).";
    }
    if (!report.items.empty())
        ss << " " << report.items.size() << " diagnostic flag(s) raised.";
    report.summary = ss.str();

    return report;
}

} // namespace almanac
} // namespace bp
