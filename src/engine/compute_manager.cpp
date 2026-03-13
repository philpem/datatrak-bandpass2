#include "compute_manager.h"
#include "groundwave.h"
#include "skywave.h"
#include "noise.h"
#include "snr.h"
#include "whdop.h"
#include "asf.h"
#include <wx/app.h>

namespace bp {

wxDEFINE_EVENT(EVT_COMPUTE_RESULT, wxCommandEvent);

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
        cancel_flag_.store(false);
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

        ComputeResult result = RunPipeline(*req.scenario, cancel_flag_);
        result.request_id = req.request_id;

        if (!cancel_flag_.load() && result_sink_) {
            auto* evt = new wxCommandEvent(EVT_COMPUTE_RESULT);
            // Pack result into a shared_ptr stored as client data
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

    // Nothing to compute without transmitters
    if (scenario.transmitters.empty()) return result;

    if (cancel.load()) return result;

    // Build grid (cancel-aware; returns empty vector if cancelled mid-build)
    auto grid_pts = buildGrid(scenario.grid, cancel);
    if (cancel.load() || grid_pts.empty()) return result;

    auto data = std::make_shared<GridData>();
    data->request_id = 0;

    // Register all standard layer keys with pre-filled grid points
    static const char* LAYERS[] = {
        "groundwave", "skywave", "atm_noise", "snr", "sgr", "gdr",
        "whdop", "repeatable", "asf", "asf_gradient", "absolute_accuracy",
        "absolute_accuracy_corrected", "confidence"
    };
    for (const char* name : LAYERS) {
        GridArray arr;
        arr.layer_name    = name;
        arr.points        = grid_pts;
        arr.values.assign(grid_pts.size(), 0.0);
        arr.lat_min       = scenario.grid.lat_min;
        arr.lat_max       = scenario.grid.lat_max;
        arr.lon_min       = scenario.grid.lon_min;
        arr.lon_max       = scenario.grid.lon_max;
        arr.resolution_km = scenario.grid.resolution_km;
        data->layers[name] = std::move(arr);
    }

    // --- Phase 2: propagation stages ---
    // Stage 1: Groundwave field strength (ITU P.368)
    computeGroundwave(*data, scenario, cancel);
    if (cancel.load()) return result;

    // Stage 2: Skywave field strength (ITU P.684, night-time median)
    computeSkywave(*data, scenario, cancel);
    if (cancel.load()) return result;

    // Stage 3: Atmospheric noise (ITU P.372)
    computeAtmNoise(*data, scenario, cancel);
    if (cancel.load()) return result;

    // Stage 4-6: SNR, GDR, SGR
    computeSNR(*data, scenario, cancel);
    if (cancel.load()) return result;

    // Stage 7-9: WHDOP and repeatable accuracy
    computeWHDOP(*data, scenario, cancel);
    if (cancel.load()) return result;

    // Stages 10-11: ASF and absolute accuracy
    computeASF(*data, scenario, cancel);
    if (cancel.load()) return result;
    if (cancel.load()) return result;

    result.data = std::move(data);
    return result;
}

} // namespace bp
