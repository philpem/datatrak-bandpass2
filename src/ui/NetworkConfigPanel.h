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

    // Flush any pending debounce: saves to scenario_ immediately and stops the
    // timer so it doesn't fire a second time.  Call before using scenario_
    // in contexts (e.g. manual compute trigger) that bypass the debounce path.
    void FlushPending();

    // Called after 500 ms debounce when any frequency/config field changes
    std::function<void(const Scenario&)> on_changed;

private:
    void OnFreqChanged(wxCommandEvent& evt);
    void OnResChanged(wxCommandEvent& evt);
    void OnOtherChanged(wxCommandEvent& evt);
    void OnDebounceTimer(wxTimerEvent& evt);
    void OnFieldKillFocus(wxFocusEvent& evt);
    void ValidateFreqFields();
    void ValidateResField();
    void UpdateMlDisplay();
    void UpdateResCountDisplay();

    wxTextCtrl*   f1_field_        = nullptr;
    wxTextCtrl*   f2_field_        = nullptr;
    wxStaticText* ml_label_        = nullptr;
    wxChoice*     mode_            = nullptr;
    wxTextCtrl*   res_field_       = nullptr;
    wxStaticText* res_count_label_ = nullptr;
    wxChoice*     datum_           = nullptr;

    wxTimer      debounce_;
    Scenario*    scenario_  = nullptr;

    static constexpr double F_MIN_KHZ  =   30.0;
    static constexpr double F_MAX_KHZ  =  300.0;
    static constexpr double RES_MIN_KM =    0.5;
    static constexpr double RES_MAX_KM = 1000.0;
};

} // namespace bp
