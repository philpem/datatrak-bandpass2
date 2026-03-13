#include "ResultsPanel.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace bp {

ResultsPanel::ResultsPanel(wxWindow* parent)
    : wxPanel(parent)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(this, wxID_ANY,
               "Field-strength vs range plots (Phase 3)"),
               1, wxALL | wxALIGN_CENTER, 20);
    SetSizer(sizer);
}

} // namespace bp
