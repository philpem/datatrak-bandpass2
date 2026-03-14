#pragma once
#include <algorithm>
#include <functional>
#include <vector>
#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include "../model/SlotPhaseResult.h"

namespace bp {

class ReceiverPanel : public wxPanel {
public:
    explicit ReceiverPanel(wxWindow* parent);

    void SetResults(const std::vector<SlotPhaseResult>& results);
    void SetPositionText(const wxString& text);
    void Clear();

    std::function<void()> on_export_simulator;

private:
    void OnExport(wxCommandEvent& evt);
    void OnUnitsChanged(wxCommandEvent& evt);
    void OnSortChanged(wxCommandEvent& evt);
    void RefreshTable();  // reformat from results_ using current units + sort selection

    wxStaticText* pos_label_    = nullptr;
    wxListCtrl*   list_         = nullptr;
    wxChoice*     units_choice_ = nullptr;
    wxChoice*     sort_choice_  = nullptr;
    wxButton*     export_btn_   = nullptr;
    std::vector<SlotPhaseResult> results_;
};

} // namespace bp
