#include "LayerPanel.h"
#include <wx/stattext.h>

namespace bp {

static const struct { const char* key; const char* label; bool default_sel; } LAYER_DEFS[] = {
    { "groundwave",       "Groundwave field strength",     true  },
    { "snr",              "SNR per transmitter",           false },
    { "gdr",              "GDR",                           false },
    { "whdop",            "WHDOP",                         false },
    { "repeatable",       "Repeatable accuracy",           false },
    { "asf",              "ASF",                           false },
    { "absolute_accuracy","Absolute accuracy",             false },
    { "asf_gradient",     "ASF gradient (monitor siting)", false },
    { "confidence",       "Confidence factor",             false },
};

LayerPanel::LayerPanel(wxWindow* parent)
    : wxScrolledWindow(parent)
    , selected_("groundwave")
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(this, wxID_ANY, "Map Layer (one at a time)"),
               0, wxLEFT | wxTOP | wxBOTTOM, 6);

    // "None" radio starts the wxRB_GROUP
    none_radio_ = new wxRadioButton(this, wxID_ANY, "(none)",
                                    wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    none_radio_->SetName("");          // empty name = "none" in GetSelectedLayer
    none_radio_->SetValue(false);      // groundwave selected by default, not none
    none_radio_->Bind(wxEVT_RADIOBUTTON, &LayerPanel::OnSelect, this);
    sizer->Add(none_radio_, 0, wxLEFT | wxBOTTOM, 8);

    for (const auto& def : LAYER_DEFS) {
        auto* rb = new wxRadioButton(this, wxID_ANY,
                                     wxString::FromUTF8(def.label));
        rb->SetName(wxString::FromUTF8(def.key));
        rb->SetValue(def.default_sel);
        rb->Bind(wxEVT_RADIOBUTTON, &LayerPanel::OnSelect, this);
        sizer->Add(rb, 0, wxLEFT | wxBOTTOM, 8);

        LayerEntry entry;
        entry.name  = def.key;
        entry.radio = rb;
        layers_.push_back(entry);
    }

    SetSizer(sizer);
    SetScrollRate(0, 10);
    FitInside();
}

void LayerPanel::OnSelect(wxCommandEvent& evt) {
    auto* rb = dynamic_cast<wxRadioButton*>(evt.GetEventObject());
    if (!rb) return;
    selected_ = rb->GetName().ToStdString();
    if (on_select) on_select(selected_);
}

std::string LayerPanel::GetSelectedLayer() const {
    return selected_;
}

} // namespace bp
