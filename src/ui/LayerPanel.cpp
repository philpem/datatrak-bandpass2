#include "LayerPanel.h"
#include <wx/stattext.h>

namespace bp {

static const struct { const char* key; const char* label; bool default_on; } LAYER_DEFS[] = {
    { "groundwave",   "Groundwave field strength",   true  },
    { "snr",          "SNR per transmitter",          true  },
    { "gdr",          "GDR",                          true  },
    { "whdop",        "WHDOP",                        true  },
    { "repeatable",   "Repeatable accuracy",          true  },
    { "asf",          "ASF / absolute accuracy",      true  },
    { "asf_gradient", "ASF gradient (monitor siting)",false },
    { "confidence",   "Confidence factor",            false },
};

LayerPanel::LayerPanel(wxWindow* parent)
    : wxScrolledWindow(parent)
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(this, wxID_ANY, "Map Layers"), 0, wxALL, 6);

    for (auto& def : LAYER_DEFS) {
        auto* cb = new wxCheckBox(this, wxID_ANY, def.label);
        cb->SetValue(def.default_on);
        cb->SetName(def.key);
        cb->Bind(wxEVT_CHECKBOX, &LayerPanel::OnToggle, this);
        sizer->Add(cb, 0, wxLEFT | wxBOTTOM, 8);

        LayerEntry entry;
        entry.name     = def.key;
        entry.checkbox = cb;
        layers_.push_back(entry);
    }

    SetSizer(sizer);
    SetScrollRate(0, 10);
    FitInside();
}

void LayerPanel::OnToggle(wxCommandEvent& evt) {
    auto* cb = dynamic_cast<wxCheckBox*>(evt.GetEventObject());
    if (cb && on_toggle)
        on_toggle(cb->GetName().ToStdString(), cb->GetValue());
}

} // namespace bp
