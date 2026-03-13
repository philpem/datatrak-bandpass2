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

    // Units selector
    auto* units_row = new wxBoxSizer(wxHORIZONTAL);
    units_row->Add(new wxStaticText(this, wxID_ANY, "Phase units:"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    units_choice_ = new wxChoice(this, wxID_ANY);
    units_choice_->Append(wxString::FromUTF8("Millilanes (0\xe2\x80\x93" "999)"));
    units_choice_->Append(wxString::FromUTF8("Degrees (0.0\xc2\xb0\xe2\x80\x93" "360.0\xc2\xb0)"));
    units_choice_->SetSelection(0);
    units_choice_->Bind(wxEVT_CHOICE, &ReceiverPanel::OnUnitsChanged, this);
    units_row->Add(units_choice_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(units_row, 0, wxLEFT | wxTOP | wxRIGHT, 6);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->AppendColumn("Slot",    wxLIST_FORMAT_RIGHT,  40);
    list_->AppendColumn("F1+",     wxLIST_FORMAT_RIGHT,  60);
    list_->AppendColumn("F1\xe2\x88\x92", wxLIST_FORMAT_RIGHT,  60);
    list_->AppendColumn("F2+",     wxLIST_FORMAT_RIGHT,  60);
    list_->AppendColumn("F2\xe2\x88\x92", wxLIST_FORMAT_RIGHT,  60);
    list_->AppendColumn("SNR dB",  wxLIST_FORMAT_RIGHT,  65);
    list_->AppendColumn("GDR dB",  wxLIST_FORMAT_RIGHT,  65);
    sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

    export_btn_ = new wxButton(this, wxID_ANY, "Export for Simulator");
    export_btn_->Bind(wxEVT_BUTTON, &ReceiverPanel::OnExport, this);
    sizer->Add(export_btn_, 0, wxALL, 6);

    SetSizer(sizer);
}

void ReceiverPanel::SetResults(const std::vector<SlotPhaseResult>& results) {
    results_ = results;
    RefreshTable();
}

void ReceiverPanel::RefreshTable() {
    bool degrees = (units_choice_->GetSelection() == 1);

    // Update column headers to reflect units
    wxListItem col;
    col.SetMask(wxLIST_MASK_TEXT);
    for (int c = 1; c <= 4; ++c) {
        static const char* ml_names[] = { "F1+", "F1\xe2\x88\x92", "F2+", "F2\xe2\x88\x92" };
        static const char* deg_names[] = { "F1+ \xc2\xb0", "F1\xe2\x88\x92 \xc2\xb0",
                                            "F2+ \xc2\xb0", "F2\xe2\x88\x92 \xc2\xb0" };
        col.SetText(wxString::FromUTF8(degrees ? deg_names[c-1] : ml_names[c-1]));
        list_->SetColumn(c, col);
        list_->SetColumnWidth(c, degrees ? 70 : 60);
    }

    list_->DeleteAllItems();
    for (const auto& r : results_) {
        long idx = list_->InsertItem(list_->GetItemCount(),
                                     wxString::Format("%d", r.slot));
        if (degrees) {
            list_->SetItem(idx, 1, wxString::Format("%.1f", r.f1plus_phase  * 360.0));
            list_->SetItem(idx, 2, wxString::Format("%.1f", r.f1minus_phase * 360.0));
            list_->SetItem(idx, 3, wxString::Format("%.1f", r.f2plus_phase  * 360.0));
            list_->SetItem(idx, 4, wxString::Format("%.1f", r.f2minus_phase * 360.0));
        } else {
            list_->SetItem(idx, 1, wxString::Format("%d", (int)(r.f1plus_phase  * 1000) % 1000));
            list_->SetItem(idx, 2, wxString::Format("%d", (int)(r.f1minus_phase * 1000) % 1000));
            list_->SetItem(idx, 3, wxString::Format("%d", (int)(r.f2plus_phase  * 1000) % 1000));
            list_->SetItem(idx, 4, wxString::Format("%d", (int)(r.f2minus_phase * 1000) % 1000));
        }
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

void ReceiverPanel::OnUnitsChanged(wxCommandEvent& /*evt*/) {
    RefreshTable();
}

void ReceiverPanel::OnExport(wxCommandEvent& /*evt*/) {
    if (on_export_simulator) on_export_simulator();
}

} // namespace bp
