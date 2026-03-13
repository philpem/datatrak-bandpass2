#pragma once
#include <functional>
#include <string>
#include <vector>
#include <wx/scrolwin.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>

namespace bp {

class LayerPanel : public wxScrolledWindow {
public:
    explicit LayerPanel(wxWindow* parent);

    // Fired when the user selects a different layer.
    // layer is the layer key (e.g. "groundwave"); empty string means "None".
    std::function<void(const std::string& layer)> on_select;

    // Returns the currently selected layer key, or "" if None is selected.
    std::string GetSelectedLayer() const;

private:
    void OnSelect(wxCommandEvent& evt);

    struct LayerEntry {
        std::string    name;
        wxRadioButton* radio = nullptr;
    };
    wxRadioButton*          none_radio_ = nullptr;
    std::vector<LayerEntry> layers_;
    std::string             selected_;   // "" = None
};

} // namespace bp
