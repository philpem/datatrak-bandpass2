#include "ReceiverPanel.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace bp {

ReceiverPanel::ReceiverPanel(wxWindow* parent)
    : wxPanel(parent)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    pos_label_ = new wxStaticText(this, wxID_ANY, "No receiver placed");
    sizer->Add(pos_label_, 0, wxLEFT | wxTOP | wxRIGHT, 6);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->AppendColumn("Slot",    wxLIST_FORMAT_RIGHT, 40);
    list_->AppendColumn("F1+",     wxLIST_FORMAT_RIGHT, 55);
    list_->AppendColumn("F1-",     wxLIST_FORMAT_RIGHT, 55);
    list_->AppendColumn("F2+",     wxLIST_FORMAT_RIGHT, 55);
    list_->AppendColumn("F2-",     wxLIST_FORMAT_RIGHT, 55);
    list_->AppendColumn("SNR dB",  wxLIST_FORMAT_RIGHT, 65);
    list_->AppendColumn("GDR dB",  wxLIST_FORMAT_RIGHT, 65);
    sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

    export_btn_ = new wxButton(this, wxID_ANY, "Export for Simulator");
    export_btn_->Bind(wxEVT_BUTTON, &ReceiverPanel::OnExport, this);
    sizer->Add(export_btn_, 0, wxALL, 6);

    SetSizer(sizer);
}

void ReceiverPanel::SetResults(const std::vector<SlotPhaseResult>& results) {
    results_ = results;
    list_->DeleteAllItems();
    for (const auto& r : results_) {
        long idx = list_->InsertItem(list_->GetItemCount(),
                                     wxString::Format("%d", r.slot));
        // Phase as integer 0–999 (fractional phase * 1000)
        list_->SetItem(idx, 1, wxString::Format("%d", (int)(r.f1plus_phase  * 1000)));
        list_->SetItem(idx, 2, wxString::Format("%d", (int)(r.f1minus_phase * 1000)));
        list_->SetItem(idx, 3, wxString::Format("%d", (int)(r.f2plus_phase  * 1000)));
        list_->SetItem(idx, 4, wxString::Format("%d", (int)(r.f2minus_phase * 1000)));
        list_->SetItem(idx, 5, wxString::Format("%.1f", r.snr_db));
        list_->SetItem(idx, 6, wxString::Format("%.1f", r.gdr_db));
    }
}

void ReceiverPanel::SetPositionText(const wxString& text) {
    pos_label_->SetLabel(text);
    Layout();
}

void ReceiverPanel::Clear() {
    results_.clear();
    list_->DeleteAllItems();
    pos_label_->SetLabel("No receiver placed");
}

void ReceiverPanel::OnExport(wxCommandEvent& /*evt*/) {
    if (on_export_simulator) on_export_simulator();
}

} // namespace bp
