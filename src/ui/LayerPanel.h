#pragma once
#include <functional>
#include <string>
#include <wx/panel.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>

namespace bp {

class LayerPanel : public wxPanel {
public:
    explicit LayerPanel(wxWindow* parent);

    std::function<void(const std::string& layer, bool visible)> on_toggle;

private:
    void OnToggle(wxCommandEvent& evt);

    struct LayerEntry {
        std::string  name;
        wxCheckBox*  checkbox = nullptr;
    };
    std::vector<LayerEntry> layers_;
};

} // namespace bp
