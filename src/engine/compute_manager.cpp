#include "compute_manager.h"
#include "groundwave.h"
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

    if (scenario.transmitters.empty()) return result;
    if (cancel.load()) return result;

    // Stage 0 – grid build
    PostProgress("Building grid", 0);
    auto grid = buildGrid(scenario.grid, cancel);
    if (cancel.load() || grid.points.empty()) return result;
    PostProgress("Building grid", 5);

    auto data = std::make_shared<GridData>();
    data->request_id = 0;

    static const char* LAYERS[] = {
        "groundwave", "skywave", "atm_noise", "snr", "sgr", "gdr",
        "whdop", "repeatable", "asf", "asf_gradient", "absolute_accuracy",
        "absolute_accuracy_corrected", "confidence"
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

    // Stage 1 – Groundwave (ITU P.368)
    PostProgress("Groundwave (P.368)", 5);
    computeGroundwave(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("Groundwave (P.368)", 20);

    // Stage 2 – Skywave (ITU P.684)
    PostProgress("Skywave (P.684)", 20);
    computeSkywave(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("Skywave (P.684)", 35);

    // Stage 3 – Atmospheric noise (ITU P.372)
    PostProgress("Noise (P.372)", 35);
    computeAtmNoise(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("Noise (P.372)", 45);

    // Stages 4–6 – SNR / GDR / SGR
    PostProgress("SNR / GDR", 45);
    computeSNR(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("SNR / GDR", 60);

    // Stages 7–9 – WHDOP and repeatable accuracy
    PostProgress("WHDOP / repeatable", 60);
    computeWHDOP(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("WHDOP / repeatable", 75);

    // Stages 10–11 – ASF, absolute accuracy, confidence
    PostProgress("ASF / accuracy", 75);
    computeASF(*data, scenario, cancel);
    if (cancel.load()) return result;
    PostProgress("ASF / accuracy", 95);

    result.data = std::move(data);
    PostProgress("Done", 100);
    return result;
}

} // namespace bp
