#include "compute_manager.h"
#include "groundwave.h"
#include "grwave.h"
#include <cmath>
#include <chrono>
#include <cstdio>
#include "skywave.h"
#include "noise.h"
#include "snr.h"
#include "whdop.h"
#include "asf.h"
#include <wx/app.h>

namespace bp {

wxDEFINE_EVENT(EVT_COMPUTE_RESULT,   wxCommandEvent);
wxDEFINE_EVENT(EVT_COMPUTE_PROGRESS, wxCommandEvent);

ComputeManager::ComputeManager(wxEvtHandler* result_sink)
    : result_sink_(result_sink)
{
    worker_ = std::thread([this]{ WorkerLoop(); });
}

ComputeManager::~ComputeManager() {
    Shutdown();
}

void ComputeManager::PostRequest(std::shared_ptr<const Scenario> scenario) {
    cancel_flag_.store(true);
    uint64_t id = ++request_counter_;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!queue_.empty()) queue_.pop();  // discard stale requests
        ComputeRequest req;
        req.scenario   = std::move(scenario);
        req.request_id = id;
        queue_.push(std::move(req));
    }
    queue_cv_.notify_one();
}

void ComputeManager::Shutdown() {
    cancel_flag_.store(true);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        ComputeRequest shutdown;
        shutdown.is_shutdown = true;
        queue_.push(shutdown);
    }
    queue_cv_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void ComputeManager::PostProgress(const char* stage, int pct) {
    if (!result_sink_) return;
    auto* evt = new wxCommandEvent(EVT_COMPUTE_PROGRESS);
    evt->SetString(wxString::FromUTF8(stage));
    evt->SetInt(pct);
    wxQueueEvent(result_sink_, evt);
}

void ComputeManager::WorkerLoop() {
    uint64_t current_id = 0;
    while (true) {
        ComputeRequest req;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]{ return !queue_.empty(); });
            req = queue_.front();
            queue_.pop();
        }

        if (req.is_shutdown) break;
        if (req.request_id < current_id) continue;  // superseded
        current_id = req.request_id;

        // Reset cancel flag here (not in PostRequest) so that the old
        // computation is guaranteed to observe cancel==true and abort
        // before the new computation begins.
        cancel_flag_.store(false);

        ComputeResult result = RunPipeline(*req.scenario, cancel_flag_);
        result.request_id = req.request_id;

        if (!cancel_flag_.load() && result_sink_) {
            auto* evt = new wxCommandEvent(EVT_COMPUTE_RESULT);
            auto* heap_result = new ComputeResult(std::move(result));
            evt->SetClientData(heap_result);
            wxQueueEvent(result_sink_, evt);
        }
    }
}

ComputeResult ComputeManager::RunPipeline(const Scenario& scenario,
                                           const std::atomic<bool>& cancel) {
    using Clock = std::chrono::steady_clock;
    auto pipeline_start = Clock::now();

    struct StageTiming {
        const char* name;
        double      ms;
    };
    std::vector<StageTiming> timings;
    timings.reserve(8);

    auto lap = [&](const char* name, Clock::time_point t0) {
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        timings.push_back({name, ms});
    };

    ComputeResult result;

    // Validate frequencies
    if (scenario.frequencies.f1_hz < 30e3 || scenario.frequencies.f1_hz > 300e3) {
        result.error = "F1 frequency out of range (30-300 kHz)";
        return result;
    }
    if (scenario.frequencies.f2_hz < 30e3 || scenario.frequencies.f2_hz > 300e3) {
        result.error = "F2 frequency out of range (30-300 kHz)";
        return result;
    }

    if (scenario.grid.resolution_km <= 0.0) {
        result.error = "Grid resolution must be > 0 km";
        return result;
    }

    // Reject grids that exceed the complexity limit (UK at 1 km spacing ≈ 766k points).
    // This mirrors the red-highlight validation in NetworkConfigPanel.
    {
        double mid_lat = (scenario.grid.lat_min + scenario.grid.lat_max) / 2.0;
        constexpr double DEG_PER_KM_LAT = 1.0 / 110.574;
        double deg_per_km_lon = 1.0 / (111.320 * std::cos(mid_lat * M_PI / 180.0));
        int rows = std::max(1, (int)((scenario.grid.lat_max - scenario.grid.lat_min)
                                    / (scenario.grid.resolution_km * DEG_PER_KM_LAT)) + 1);
        int cols = std::max(1, (int)((scenario.grid.lon_max - scenario.grid.lon_min)
                                    / (scenario.grid.resolution_km * deg_per_km_lon)) + 1);
        constexpr int MAX_GRID_POINTS = 800000;
        if (rows * cols > MAX_GRID_POINTS) {
            result.error = "Grid too large: reduce area or increase resolution";
            return result;
        }
    }

    if (scenario.transmitter_sites.empty()) return result;
    if (cancel.load()) return result;

    // Stage 0 – grid build
    auto t0 = Clock::now();
    PostProgress("Building grid", 0);
    auto grid = buildGrid(scenario.grid, cancel);
    if (cancel.load() || grid.points.empty()) return result;
    PostProgress("Building grid", 5);

    auto data = std::make_shared<GridData>();
    data->request_id = 0;

    static const char* LAYERS[] = {
        "groundwave", "skywave", "atm_noise", "snr", "sgr", "gdr",
        "whdop", "repeatable", "asf", "asf_gradient", "absolute_accuracy",
        "absolute_accuracy_corrected", "absolute_accuracy_delta", "confidence"
    };
    for (const char* name : LAYERS) {
        GridArray arr;
        arr.layer_name    = name;
        arr.points        = grid.points;
        arr.values.assign(grid.points.size(), 0.0);
        arr.width         = grid.width;
        arr.height        = grid.height;
        arr.lat_min       = scenario.grid.lat_min;
        arr.lat_max       = scenario.grid.lat_max;
        arr.lon_min       = scenario.grid.lon_min;
        arr.lon_max       = scenario.grid.lon_max;
        arr.resolution_km = scenario.grid.resolution_km;
        data->layers[name] = std::move(arr);
    }
    lap("Grid build", t0);

    // Stage 1 – Groundwave (ITU P.368)
    // GRWAVE residue series is ~100x slower per grid point than the polynomial,
    // so give it a much larger share of the progress bar to avoid appearing stuck.
    const bool is_grwave = scenario.propagation_model == Scenario::PropagationModel::GRWAVE;

    // Build GRWAVE lookup tables if using the GRWAVE propagation model.
    // Built here (not in computeGroundwave) so the LUTs stay alive across all
    // pipeline stages — computeASF() may also call grwave_field_dbuvm() on
    // cache misses.  One LUT per distinct frequency (~80 KB each).
    std::unique_ptr<GrwaveLUT> grwave_lut_f1, grwave_lut_f2;
    std::unique_ptr<GrwaveLUT::Scope> grwave_scope_f1, grwave_scope_f2;
    if (is_grwave) {
        t0 = Clock::now();
        bool two_freqs = (scenario.frequencies.f1_hz != scenario.frequencies.f2_hz);
        PostProgress("Building GRWAVE LUT (F1)", 5);
        auto f1_progress = [this, two_freqs](int pct) {
            int mapped = two_freqs ? (5 + pct * 5 / 100) : (5 + pct * 10 / 100);
            PostProgress("Building GRWAVE LUT (F1)", mapped);
        };
        grwave_lut_f1 = std::make_unique<GrwaveLUT>(scenario.frequencies.f1_hz, f1_progress);
        grwave_scope_f1 = std::make_unique<GrwaveLUT::Scope>(*grwave_lut_f1);

        if (two_freqs) {
            PostProgress("Building GRWAVE LUT (F2)", 10);
            auto f2_progress = [this](int pct) {
                PostProgress("Building GRWAVE LUT (F2)", 10 + pct * 5 / 100);
            };
            grwave_lut_f2 = std::make_unique<GrwaveLUT>(scenario.frequencies.f2_hz, f2_progress);
            grwave_scope_f2 = std::make_unique<GrwaveLUT::Scope>(*grwave_lut_f2);
        }
        if (cancel.load()) return result;
        lap("GRWAVE LUT", t0);
    }

    const char* gw_label =
        is_grwave                                                                ? "Groundwave (GRWAVE)" :
        scenario.propagation_model == Scenario::PropagationModel::Homogeneous    ? "Groundwave (Homogeneous)" :
                                                                                   "Groundwave (Millington)";
    const int gw_start = is_grwave ? 15 : 5;
    const int gw_end   = is_grwave ? 60 : 20;
    t0 = Clock::now();
    PostProgress(gw_label, gw_start);
    computeGroundwave(*data, scenario, cancel, [this, gw_label, gw_start, gw_end](int pct) {
        PostProgress(gw_label, gw_start + pct * (gw_end - gw_start) / 100);
    });
    if (cancel.load()) return result;
    PostProgress(gw_label, gw_end);
    lap("Groundwave", t0);

    // Stage 2 – Skywave (ITU P.684)
    const int sky_end = is_grwave ? 65 : 35;
    t0 = Clock::now();
    PostProgress("Skywave (P.684)", gw_end);
    computeSkywave(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("Skywave (P.684)", sky_end);
    lap("Skywave", t0);

    // Stage 3 – Atmospheric noise (ITU P.372)
    const int noise_end = is_grwave ? 70 : 45;
    t0 = Clock::now();
    PostProgress("Noise (P.372)", sky_end);
    computeAtmNoise(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("Noise (P.372)", noise_end);
    lap("Noise", t0);

    // Stages 4–6 – SNR / GDR / SGR
    const int snr_end = is_grwave ? 75 : 60;
    t0 = Clock::now();
    PostProgress("SNR / GDR", noise_end);
    computeSNR(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("SNR / GDR", snr_end);
    lap("SNR/GDR", t0);

    // Stages 7–9 – WHDOP and repeatable accuracy
    const int whdop_end = is_grwave ? 80 : 75;
    t0 = Clock::now();
    PostProgress("WHDOP / repeatable", snr_end);
    computeWHDOP(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("WHDOP / repeatable", whdop_end);
    lap("WHDOP", t0);

    // Stages 10–11 – ASF, absolute accuracy, confidence
    t0 = Clock::now();
    PostProgress("ASF / accuracy", whdop_end);
    computeASF(*data, scenario, cancel, [this, whdop_end](int pct) {
        PostProgress("ASF / accuracy", whdop_end + pct * (95 - whdop_end) / 100);
    });
    if (cancel.load()) return result;
    PostProgress("ASF / accuracy", 95);
    lap("ASF/accuracy", t0);

    result.data = std::move(data);

    // Print timing summary to stderr
    double total_ms = std::chrono::duration<double, std::milli>(
        Clock::now() - pipeline_start).count();
    std::fprintf(stderr, "\n=== Pipeline timing (%zu grid points) ===\n",
                 grid.points.size());
    for (const auto& st : timings)
        std::fprintf(stderr, "  %-20s %8.1f ms\n", st.name, st.ms);
    std::fprintf(stderr, "  %-20s %8.1f ms\n", "TOTAL", total_ms);
    std::fprintf(stderr, "=========================================\n");

    PostProgress("Done", 100);
    return result;
}

} // namespace bp
