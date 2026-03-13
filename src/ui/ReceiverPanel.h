#pragma once
#include <functional>
#include <vector>
#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include "../model/SlotPhaseResult.h"

namespace bp {

class ReceiverPanel : public wxPanel {
public:
    explicit ReceiverPanel(wxWindow* parent);

    void SetResults(const std::vector<SlotPhaseResult>& results);
    // Update the position label shown above the phase table.
    // text should be "WGS84: 52.32470, -0.18480  |  TL 51305 62245" or similar.
    void SetPositionText(const wxString& text);
    void Clear();

    std::function<void()> on_export_simulator;

private:
    void OnExport(wxCommandEvent& evt);

    wxStaticText* pos_label_    = nullptr;
    wxListCtrl*   list_         = nullptr;
    wxButton*     export_btn_   = nullptr;
    std::vector<SlotPhaseResult> results_;
};

} // namespace bp
