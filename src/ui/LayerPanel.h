#pragma once
#include <functional>
#include <string>
#include <wx/panel.h>
#include <wx/choice.h>
#include <wx/slider.h>
#include <wx/sizer.h>

namespace bp {

class LayerPanel : public wxPanel {
public:
    explicit LayerPanel(wxWindow* parent);

    // Fired when the user selects a different layer.
    // layer is the layer key (e.g. "groundwave"); empty string means "None".
    std::function<void(const std::string& layer)> on_select;

    // Fired when the opacity slider is moved.  opacity is in [0.0, 1.0].
    std::function<void(float opacity)> on_opacity_changed;

    // Returns the currently selected layer key, or "" if None is selected.
    std::string GetSelectedLayer() const;

private:
    void OnSelect(wxCommandEvent& evt);
    void OnOpacity(wxCommandEvent& evt);

    wxChoice* choice_     = nullptr;
    wxSlider* opacity_sl_ = nullptr;
    std::string selected_;
};

} // namespace bp
