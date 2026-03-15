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

    // Called after 500 ms debounce when any frequency/config field changes.
    // Not called when the resolution field is red (point count over limit).
    std::function<void(const Scenario&)> on_changed;

private:
    void OnFreqChanged(wxCommandEvent& evt);
    void OnBoundsChanged(wxCommandEvent& evt);
    void OnResChanged(wxCommandEvent& evt);
    void OnOtherChanged(wxCommandEvent& evt);
    void OnDebounceTimer(wxTimerEvent& evt);
    void OnFieldKillFocus(wxFocusEvent& evt);
    void ValidateFreqFields();
    void ValidateBoundsFields();
    void ValidateResField();
    void UpdateMlDisplay();
    void UpdateResCountDisplay();
    bool IsResValid() const;   // true iff resolution is in range AND point count is within limit

    wxTextCtrl*   f1_field_        = nullptr;
    wxTextCtrl*   f2_field_        = nullptr;
    wxStaticText* ml_label_        = nullptr;
    // Grid bounds
    wxTextCtrl*   lat_min_field_   = nullptr;
    wxTextCtrl*   lat_max_field_   = nullptr;
    wxTextCtrl*   lon_min_field_   = nullptr;
    wxTextCtrl*   lon_max_field_   = nullptr;

    // Grid resolution
    wxTextCtrl*   res_field_       = nullptr;
    wxStaticText* res_count_label_ = nullptr;

    wxChoice*     datum_           = nullptr;

    wxTimer      debounce_;
    Scenario*    scenario_  = nullptr;

    static constexpr double F_MIN_KHZ    =   30.0;
    static constexpr double F_MAX_KHZ    =  300.0;
    static constexpr double RES_MIN_KM   =    0.001;  // sanity floor only; point count is the real limit
    static constexpr double RES_MAX_KM   = 1000.0;
    // UK full-coverage at 1 km spacing ≈ 766k points; use 800k as hard ceiling.
    static constexpr int    MAX_GRID_PTS = 800000;
};

} // namespace bp
