#pragma once
#include <functional>
#include <vector>
#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include "../model/SlotPhaseResult.h"

namespace bp {

class ReceiverPanel : public wxPanel {
public:
    explicit ReceiverPanel(wxWindow* parent);

    void SetResults(const std::vector<SlotPhaseResult>& results);
    void Clear();

    std::function<void()> on_export_simulator;

private:
    void OnExport(wxCommandEvent& evt);

    wxListCtrl* list_  = nullptr;
    wxButton*   export_btn_ = nullptr;
    std::vector<SlotPhaseResult> results_;
};

} // namespace bp
