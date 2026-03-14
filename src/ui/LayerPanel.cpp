#include "LayerPanel.h"
#include <wx/stattext.h>

namespace bp {

// Layer keys ending in "_log" use log-scale colour ramp against the same
// underlying GridArray as the bare key.  PushLayerToMap strips the suffix
// before looking up data and forces ScaleMode::Log regardless of UseLogScale().
static const struct { const char* key; const char* label; } LAYER_DEFS[] = {
    { "",                        "(none)"                                  },
    { "groundwave",              "Groundwave field strength"               },
    { "snr",                     "SNR per transmitter"                     },
    { "gdr",                     "GDR"                                     },
    { "whdop_log",               "WHDOP"                                   },
    { "repeatable_log",          "Repeatable accuracy"                     },
    { "asf",                     "ASF"                                     },
    { "absolute_accuracy",       "Absolute accuracy (linear)"              },
    { "absolute_accuracy_log",   "Absolute accuracy (log scale)"           },
    { "asf_gradient",            "ASF gradient / monitor siting (linear)"  },
    { "asf_gradient_log",        "ASF gradient / monitor siting (log)"     },
    { "confidence",              "Confidence factor"                       },
};

LayerPanel::LayerPanel(wxWindow* parent)
    : wxPanel(parent)
    , selected_("groundwave")
{
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(new wxStaticText(this, wxID_ANY, "Map Layer:"),
               0, wxLEFT | wxTOP, 6);

    choice_ = new wxChoice(this, wxID_ANY);
    for (const auto& def : LAYER_DEFS)
        choice_->Append(wxString::FromUTF8(def.label));
    // Default: "groundwave" is index 1
    choice_->SetSelection(1);
    choice_->Bind(wxEVT_CHOICE, &LayerPanel::OnSelect, this);
    choice_->SetToolTip(
        "GDR = Groundwave-to-Disturbance Ratio\n"
        "SNR = Signal-to-Noise Ratio\n"
        "WHDOP = Weighted Horizontal Dilution of Precision\n"
        "ASF = Additional Secondary Factor (propagation delay)\n"
        "ASF gradient = spatial rate of change of ASF [ml/km]");
    sizer->Add(choice_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

    sizer->Add(new wxStaticText(this, wxID_ANY, "Overlay opacity:"),
               0, wxLEFT | wxTOP, 6);

    // Slider: 0–100 (maps to 0.0–1.0), default 40 (= 0.40)
    opacity_sl_ = new wxSlider(this, wxID_ANY, 40, 0, 100,
                               wxDefaultPosition, wxDefaultSize,
                               wxSL_HORIZONTAL | wxSL_LABELS);
    opacity_sl_->Bind(wxEVT_SLIDER, &LayerPanel::OnOpacity, this);
    sizer->Add(opacity_sl_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

    SetSizer(sizer);
}

void LayerPanel::OnSelect(wxCommandEvent& /*evt*/) {
    int idx = choice_->GetSelection();
    if (idx == wxNOT_FOUND || idx < 0 || idx >= (int)(sizeof(LAYER_DEFS)/sizeof(LAYER_DEFS[0])))
        return;
    selected_ = LAYER_DEFS[idx].key;
    if (on_select) on_select(selected_);
}

void LayerPanel::OnOpacity(wxCommandEvent& /*evt*/) {
    float op = opacity_sl_->GetValue() / 100.0f;
    if (on_opacity_changed) on_opacity_changed(op);
}

std::string LayerPanel::GetSelectedLayer() const {
    return selected_;
}

} // namespace bp
