#pragma once
#include <functional>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include "../model/Scenario.h"

namespace bp {

class NetworkConfigPanel : public wxPanel {
public:
    explicit NetworkConfigPanel(wxWindow* parent);

    void SetScenario(Scenario* scenario);
    void SaveToScenario();

    // Called after 500 ms debounce when any frequency/config field changes
    std::function<void(const Scenario&)> on_changed;

private:
    void OnFreqChanged(wxCommandEvent& evt);
    void OnOtherChanged(wxCommandEvent& evt);
    void OnDebounceTimer(wxTimerEvent& evt);
    void ValidateFreqFields();
    void UpdateMlDisplay();

    wxTextCtrl*  f1_field_  = nullptr;
    wxTextCtrl*  f2_field_  = nullptr;
    wxStaticText* ml_label_ = nullptr;
    wxChoice*    mode_      = nullptr;
    wxTextCtrl*  res_field_ = nullptr;
    wxChoice*    rx_model_  = nullptr;
    wxChoice*    datum_     = nullptr;

    wxTimer      debounce_;
    Scenario*    scenario_  = nullptr;

    static constexpr double F_MIN_KHZ =  30.0;
    static constexpr double F_MAX_KHZ = 300.0;
};

} // namespace bp
