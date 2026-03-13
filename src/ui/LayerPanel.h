#pragma once
#include <functional>
#include <map>
#include <string>
#include <wx/scrolwin.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>

namespace bp {

class LayerPanel : public wxScrolledWindow {
public:
    explicit LayerPanel(wxWindow* parent);

    std::function<void(const std::string& layer, bool visible)> on_toggle;

    // Returns a map of layer name → current visibility
    std::map<std::string, bool> GetVisibleLayers() const;

private:
    void OnToggle(wxCommandEvent& evt);

    struct LayerEntry {
        std::string  name;
        wxCheckBox*  checkbox = nullptr;
    };
    std::vector<LayerEntry> layers_;
};

} // namespace bp
