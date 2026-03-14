#pragma once
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <functional>
#include <wx/event.h>
#include "grid.h"
#include "../model/Scenario.h"

namespace bp {

struct ComputeRequest {
    std::shared_ptr<const Scenario> scenario;
    uint64_t                        request_id  = 0;
    bool                            is_shutdown = false;
};

struct ComputeResult {
    uint64_t                        request_id = 0;
    std::shared_ptr<const GridData> data;
    std::string                     error;    // empty on success
    std::string                     warning;  // non-empty if degraded (e.g. GDAL unavailable)
};

// Custom wxEvents for worker → UI communication
wxDECLARE_EVENT(EVT_COMPUTE_RESULT,   wxCommandEvent);
// EVT_COMPUTE_PROGRESS: GetString() = stage name, GetInt() = percent 0–100
wxDECLARE_EVENT(EVT_COMPUTE_PROGRESS, wxCommandEvent);

class ComputeManager {
public:
    // result_sink receives EVT_COMPUTE_RESULT events on the UI thread
    explicit ComputeManager(wxEvtHandler* result_sink);
    ~ComputeManager();

    void PostRequest(std::shared_ptr<const Scenario> scenario);
    void Shutdown();

private:
    void WorkerLoop();
    ComputeResult RunPipeline(const Scenario& scenario,
                              const std::atomic<bool>& cancel);
    void PostProgress(const char* stage, int pct);

    std::queue<ComputeRequest> queue_;
    std::mutex                 queue_mutex_;
    std::condition_variable    queue_cv_;
    std::atomic<bool>          cancel_flag_{false};
    std::atomic<uint64_t>      request_counter_{0};
    std::thread                worker_;
    wxEvtHandler*              result_sink_;
};

} // namespace bp
