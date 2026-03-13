#pragma once
#include <wx/panel.h>

namespace bp {

struct Scenario;

// P3-09: Field-strength vs range plot panel.
// Shows groundwave field strength (dBuV/m) vs range (km) for each
// transmitter in the scenario, plus noise floor reference lines.
// Redraws whenever SetScenario() is called or the panel is resized.
class ResultsPanel : public wxPanel {
public:
    explicit ResultsPanel(wxWindow* parent);

    // Set the scenario whose transmitters/receiver will be plotted.
    // The pointer must remain valid for the lifetime of this panel.
    void SetScenario(const Scenario* scenario);

private:
    void OnPaint(wxPaintEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void DrawPlot(wxDC& dc);

    const Scenario* scenario_ = nullptr;
};

} // namespace bp
