#pragma once
#include <functional>
#include <wx/scrolwin.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include "../model/Scenario.h"

namespace bp {

class NetworkConfigPanel : public wxScrolledWindow {
public:
    explicit NetworkConfigPanel(wxWindow* parent);

    void SetScenario(Scenario* scenario);
    void SaveToScenario();

    // Update the bounds fields (called when the map rectangle is dragged).
    // Does NOT trigger a recompute — the caller is responsible.
    void SetBoundsFromMap(double lat_min, double lat_max,
                          double lon_min, double lon_max);

    // Flush any pending debounce: saves to scenario_ immediately and stops the
    // timer so it doesn't fire a second time.  Call before using scenario_
    // in contexts (e.g. manual compute trigger) that bypass the debounce path.
    void FlushPending();

    // Called after 500 ms debounce when any frequency/config field changes
    std::function<void(const Scenario&)> on_changed;

private:
    void OnFreqChanged(wxCommandEvent& evt);
    void OnBoundsChanged(wxCommandEvent& evt);
    void OnMaxPtsChanged(wxCommandEvent& evt);
    void OnOtherChanged(wxCommandEvent& evt);
    void OnDebounceTimer(wxTimerEvent& evt);
    void OnFieldKillFocus(wxFocusEvent& evt);
    void ValidateFreqFields();
    void ValidateBoundsFields();
    void ValidateMaxPtsField();
    void UpdateMlDisplay();
    void UpdateResInfoDisplay();

    wxTextCtrl*   f1_field_        = nullptr;
    wxTextCtrl*   f2_field_        = nullptr;
    wxStaticText* ml_label_        = nullptr;
    wxChoice*     mode_            = nullptr;

    // Grid bounds
    wxTextCtrl*   lat_min_field_   = nullptr;
    wxTextCtrl*   lat_max_field_   = nullptr;
    wxTextCtrl*   lon_min_field_   = nullptr;
    wxTextCtrl*   lon_max_field_   = nullptr;

    // Point-count complexity limit
    wxTextCtrl*   max_pts_field_   = nullptr;
    wxStaticText* res_info_label_  = nullptr;  // shows computed resolution

    wxChoice*     datum_           = nullptr;

    wxTimer      debounce_;
    Scenario*    scenario_  = nullptr;

    static constexpr double F_MIN_KHZ    =    30.0;
    static constexpr double F_MAX_KHZ    =   300.0;
    static constexpr int    PTS_MIN      =   100;
    static constexpr int    PTS_MAX      = 10000000;
};

} // namespace bp
