#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PoRefDialog — P5-10: Pattern offset reference UI (Modes 1 & 2)
//
// Mode 1 (Baseline midpoint): computes po at the geometric midpoint between
//   each slave station and its master.  No user input beyond clicking OK.
//
// Mode 2 (User marker): user enters a reference coordinate (WGS84 decimal
//   degrees or OSGB National Grid reference).  po is computed at that point.
//   Multiple markers can be added; the average po is taken.
//   The RMS spread across markers is shown as a quality indicator.

#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/textctrl.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <vector>
#include <string>
#include "../model/Scenario.h"

namespace bp {

class PoRefDialog : public wxDialog {
public:
    // On successful OK, result_offsets holds the computed PatternOffset list.
    std::vector<PatternOffset> result_offsets;

    PoRefDialog(wxWindow* parent, const Scenario& scenario);

private:
    void OnModeChanged(wxCommandEvent& evt);
    void OnAddMarker(wxCommandEvent& evt);
    void OnRemoveMarker(wxCommandEvent& evt);
    void OnOK(wxCommandEvent& evt);
    void UpdateUI();

    const Scenario& scenario_;

    // Mode selection
    wxRadioButton* rb_mode1_ = nullptr;
    wxRadioButton* rb_mode2_ = nullptr;

    // Mode 2 controls
    wxTextCtrl*  coord_entry_  = nullptr;
    wxButton*    btn_add_      = nullptr;
    wxButton*    btn_remove_   = nullptr;
    wxListCtrl*  marker_list_  = nullptr;
    wxStaticText* rms_label_  = nullptr;

    // Stored markers for mode 2
    struct Marker { double lat, lon; std::string label; };
    std::vector<Marker> markers_;
};

} // namespace bp
