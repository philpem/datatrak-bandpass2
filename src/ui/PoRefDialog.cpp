// SPDX-License-Identifier: GPL-3.0-or-later
#include "PoRefDialog.h"
#include "../almanac/AlmanacExport.h"
#include "../coords/NationalGrid.h"
#include "../coords/Osgb.h"
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/button.h>
#include <wx/valtext.h>
#include <wx/panel.h>
#include <cmath>
#include <stdexcept>
#include <sstream>

namespace bp {

PoRefDialog::PoRefDialog(wxWindow* parent, const Scenario& scenario)
    : wxDialog(parent, wxID_ANY, "Compute Pattern Offsets",
               wxDefaultPosition, wxSize(520, 460),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , scenario_(scenario)
{
    auto* top = new wxBoxSizer(wxVERTICAL);

    // ---- Description
    auto* desc = new wxStaticText(this, wxID_ANY,
        "Compute pattern offsets (po) for almanac export.\n"
        "Mode 1 uses the geometric midpoint of each station pair.\n"
        "Mode 2 uses a surveyed reference point you enter below.");
    top->Add(desc, 0, wxALL | wxEXPAND, 8);

    // ---- Mode selection
    auto* mode_box  = new wxStaticBoxSizer(wxVERTICAL, this,
        "Reference point for po calculation");
    rb_mode1_ = new wxRadioButton(this, wxID_ANY,
        "Baseline midpoint  (geometric mid-point of each station pair; "
        "quick estimate, no survey data needed)",
        wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    rb_mode2_ = new wxRadioButton(this, wxID_ANY,
        "Surveyed reference marker  (one or more positions you enter below; "
        "use for monitor-station or known-point calibration)");
    mode_box->Add(rb_mode1_, 0, wxALL, 4);
    mode_box->Add(rb_mode2_, 0, wxALL, 4);
    rb_mode1_->SetValue(true);
    top->Add(mode_box, 0, wxALL | wxEXPAND, 8);

    // ---- Mode 2 controls
    auto* mode2_box = new wxStaticBoxSizer(wxVERTICAL, this,
        "Surveyed reference marker(s)");

    auto* coord_row = new wxBoxSizer(wxHORIZONTAL);
    coord_row->Add(new wxStaticText(this, wxID_ANY, "Coordinate:"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    coord_entry_ = new wxTextCtrl(this, wxID_ANY, "",
                                  wxDefaultPosition, wxSize(240, -1));
    coord_entry_->SetHint("e.g.  52.3247 -0.1848  or  TL 271 707");
    btn_add_    = new wxButton(this, wxID_ANY, "Add");
    btn_remove_ = new wxButton(this, wxID_ANY, "Remove selected");
    coord_row->Add(coord_entry_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    coord_row->Add(btn_add_,    0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    coord_row->Add(btn_remove_, 0, wxALIGN_CENTER_VERTICAL);
    mode2_box->Add(coord_row, 0, wxALL | wxEXPAND, 4);

    marker_list_ = new wxListCtrl(this, wxID_ANY,
                                   wxDefaultPosition, wxSize(-1, 100),
                                   wxLC_REPORT | wxLC_SINGLE_SEL);
    marker_list_->AppendColumn("Marker",    wxLIST_FORMAT_LEFT, 180);
    marker_list_->AppendColumn("Lat",       wxLIST_FORMAT_RIGHT, 90);
    marker_list_->AppendColumn("Lon",       wxLIST_FORMAT_RIGHT, 90);
    mode2_box->Add(marker_list_, 1, wxALL | wxEXPAND, 4);

    rms_label_ = new wxStaticText(this, wxID_ANY, "");
    mode2_box->Add(rms_label_, 0, wxALL, 4);

    top->Add(mode2_box, 1, wxALL | wxEXPAND, 8);

    // ---- Buttons
    auto* btn_sizer = new wxStdDialogButtonSizer;
    auto* ok_btn    = new wxButton(this, wxID_OK,     "Compute && Apply");
    auto* cancel_btn = new wxButton(this, wxID_CANCEL, "Cancel");
    btn_sizer->AddButton(ok_btn);
    btn_sizer->AddButton(cancel_btn);
    btn_sizer->Realize();
    top->Add(btn_sizer, 0, wxALL | wxEXPAND, 8);

    SetSizerAndFit(top);
    SetMinSize(wxSize(520, 460));
    UpdateUI();

    // Bindings
    rb_mode1_->Bind(wxEVT_RADIOBUTTON, &PoRefDialog::OnModeChanged, this);
    rb_mode2_->Bind(wxEVT_RADIOBUTTON, &PoRefDialog::OnModeChanged, this);
    btn_add_->Bind(wxEVT_BUTTON,       &PoRefDialog::OnAddMarker,   this);
    btn_remove_->Bind(wxEVT_BUTTON,    &PoRefDialog::OnRemoveMarker,this);
    ok_btn->Bind(wxEVT_BUTTON,         &PoRefDialog::OnOK,          this);
}

void PoRefDialog::OnModeChanged(wxCommandEvent& /*evt*/) { UpdateUI(); }

void PoRefDialog::UpdateUI() {
    bool mode2 = rb_mode2_->GetValue();
    coord_entry_->Enable(mode2);
    btn_add_->Enable(mode2);
    btn_remove_->Enable(mode2 && marker_list_->GetSelectedItemCount() > 0);
    marker_list_->Enable(mode2);
}

void PoRefDialog::OnAddMarker(wxCommandEvent& /*evt*/) {
    std::string text = coord_entry_->GetValue().ToStdString();
    if (text.empty()) return;

    // Parse coordinate using CoordSystem auto-detect
    double lat = 0.0, lon = 0.0;
    try {
        auto latlng = national_grid::parse_coordinate(text, osgb::ostn15_loaded());
        lat = latlng.lat;
        lon = latlng.lon;
    } catch (const std::exception& e) {
        wxMessageBox(wxString::FromUTF8(e.what()), "Coordinate error",
                     wxICON_ERROR, this);
        return;
    }

    Marker m;
    m.lat   = lat;
    m.lon   = lon;
    m.label = "Marker " + std::to_string(markers_.size() + 1);
    markers_.push_back(m);

    long row = marker_list_->GetItemCount();
    marker_list_->InsertItem(row, wxString::FromUTF8(m.label));
    marker_list_->SetItem(row, 1,
        wxString::Format("%.5f", lat));
    marker_list_->SetItem(row, 2,
        wxString::Format("%.5f", lon));

    coord_entry_->Clear();
    UpdateUI();
}

void PoRefDialog::OnRemoveMarker(wxCommandEvent& /*evt*/) {
    long sel = marker_list_->GetNextItem(-1, wxLIST_NEXT_ALL,
                                          wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= (long)markers_.size()) return;
    markers_.erase(markers_.begin() + sel);
    marker_list_->DeleteItem(sel);
    UpdateUI();
}

void PoRefDialog::OnOK(wxCommandEvent& /*evt*/) {
    if (rb_mode1_->GetValue()) {
        // Mode 1: baseline midpoint
        result_offsets = almanac::compute_po_mode1(scenario_, 50);
        if (result_offsets.empty()) {
            wxMessageBox("No slave transmitters found. "
                         "Add transmitters with a master_slot set.",
                         "Pattern Offsets", wxICON_INFORMATION, this);
            return;
        }
        EndModal(wxID_OK);
        return;
    }

    // Mode 2: user markers
    if (markers_.empty()) {
        wxMessageBox("Add at least one reference marker.", "Pattern Offsets",
                     wxICON_INFORMATION, this);
        return;
    }

    // Compute po at each marker and average
    std::vector<std::vector<PatternOffset>> per_marker;
    for (const auto& m : markers_) {
        auto offsets = almanac::compute_po_at_point(scenario_, m.lat, m.lon, 50);
        if (!offsets.empty()) per_marker.push_back(offsets);
    }
    if (per_marker.empty()) {
        wxMessageBox("No slave transmitters found.", "Pattern Offsets",
                     wxICON_ERROR, this);
        return;
    }

    // Average across markers; compute per-pattern RMS spread
    size_t npat = per_marker[0].size();
    result_offsets.resize(npat);
    double rms_sum = 0.0;
    int    rms_cnt = 0;

    for (size_t p = 0; p < npat; ++p) {
        result_offsets[p].pattern = per_marker[0][p].pattern;

        double sum_f1p = 0, sum_f1m = 0, sum_f2p = 0, sum_f2m = 0;
        for (const auto& m : per_marker) {
            sum_f1p += m[p].f1plus_ml;
            sum_f1m += m[p].f1minus_ml;
            sum_f2p += m[p].f2plus_ml;
            sum_f2m += m[p].f2minus_ml;
        }
        double n = (double)per_marker.size();
        result_offsets[p].f1plus_ml  = (int32_t)std::llround(sum_f1p / n);
        result_offsets[p].f1minus_ml = (int32_t)std::llround(sum_f1m / n);
        result_offsets[p].f2plus_ml  = (int32_t)std::llround(sum_f2p / n);
        result_offsets[p].f2minus_ml = (int32_t)std::llround(sum_f2m / n);

        // Compute variance from mean for RMS
        if (per_marker.size() > 1) {
            double mean_f1p = result_offsets[p].f1plus_ml;
            for (const auto& m : per_marker) {
                double d = m[p].f1plus_ml - mean_f1p;
                rms_sum += d * d;
                ++rms_cnt;
            }
        }
    }

    // Show RMS spread
    if (rms_cnt > 0) {
        double rms = std::sqrt(rms_sum / rms_cnt);
        rms_label_->SetLabel(wxString::Format(
            "RMS spread across %zu marker(s): %.1f ml",
            per_marker.size(), rms));
    }

    EndModal(wxID_OK);
}

} // namespace bp
