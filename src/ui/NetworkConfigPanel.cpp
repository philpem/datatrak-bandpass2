#include "NetworkConfigPanel.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace bp {

NetworkConfigPanel::NetworkConfigPanel(wxWindow* parent)
    : wxScrolledWindow(parent)
    , debounce_(this)
{
    debounce_.Bind(wxEVT_TIMER, &NetworkConfigPanel::OnDebounceTimer, this);

    auto* gs = new wxFlexGridSizer(2, 4, 6);
    gs->AddGrowableCol(1);

    // F1
    gs->Add(new wxStaticText(this, wxID_ANY, "F1 (kHz)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    f1_field_ = new wxTextCtrl(this, wxID_ANY, "146.4375");
    f1_field_->Bind(wxEVT_TEXT, &NetworkConfigPanel::OnFreqChanged, this);
    gs->Add(f1_field_, 1, wxEXPAND | wxBOTTOM, 2);

    // F2
    gs->Add(new wxStaticText(this, wxID_ANY, "F2 (kHz)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    f2_field_ = new wxTextCtrl(this, wxID_ANY, "131.2500");
    f2_field_->Bind(wxEVT_TEXT, &NetworkConfigPanel::OnFreqChanged, this);
    gs->Add(f2_field_, 1, wxEXPAND | wxBOTTOM, 2);

    // Millilane display
    gs->Add(new wxStaticText(this, wxID_ANY, ""), 0);
    ml_label_ = new wxStaticText(this, wxID_ANY, "1 ml(f1) = 2.047 m   1 ml(f2) = 2.285 m");
    gs->Add(ml_label_, 0, wxBOTTOM, 6);

    // Mode
    gs->Add(new wxStaticText(this, wxID_ANY, "Mode"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    wxArrayString modes;
    modes.Add("8-slot"); modes.Add("Interlaced");
    mode_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, modes);
    mode_->SetSelection(0);
    mode_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnOtherChanged, this);
    gs->Add(mode_, 1, wxEXPAND | wxBOTTOM, 2);

    // Grid resolution
    gs->Add(new wxStaticText(this, wxID_ANY, "Grid res (km)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    res_field_ = new wxTextCtrl(this, wxID_ANY, "10.0");
    res_field_->Bind(wxEVT_TEXT, &NetworkConfigPanel::OnOtherChanged, this);
    gs->Add(res_field_, 1, wxEXPAND | wxBOTTOM, 2);

    // Receiver model
    gs->Add(new wxStaticText(this, wxID_ANY, "Receiver"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    wxArrayString rxm;
    rxm.Add("Simple"); rxm.Add("Advanced");
    rx_model_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, rxm);
    rx_model_->SetSelection(0);
    rx_model_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnOtherChanged, this);
    gs->Add(rx_model_, 1, wxEXPAND | wxBOTTOM, 2);

    // Datum
    gs->Add(new wxStaticText(this, wxID_ANY, "Datum"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    wxArrayString datums;
    datums.Add("Helmert"); datums.Add("OSTN15");
    datum_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, datums);
    datum_->SetSelection(0);
    datum_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnOtherChanged, this);
    gs->Add(datum_, 1, wxEXPAND | wxBOTTOM, 2);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(this, wxID_ANY, "Network Configuration"), 0, wxALL, 6);
    sizer->Add(gs, 0, wxALL | wxEXPAND, 8);
    SetSizer(sizer);
    SetScrollRate(0, 10);
    FitInside();
}

void NetworkConfigPanel::SetScenario(Scenario* scenario) {
    scenario_ = scenario;
    if (!scenario_) return;
    f1_field_->ChangeValue(wxString::Format("%.4f", scenario_->frequencies.f1_hz / 1000.0));
    f2_field_->ChangeValue(wxString::Format("%.4f", scenario_->frequencies.f2_hz / 1000.0));
    res_field_->ChangeValue(wxString::Format("%.1f", scenario_->grid.resolution_km));
    mode_->SetSelection(scenario_->mode == Scenario::OperationMode::Interlaced ? 1 : 0);
    rx_model_->SetSelection(scenario_->receiver.mode == ReceiverModel::Mode::Advanced ? 1 : 0);
    datum_->SetSelection(scenario_->datum_transform == Scenario::DatumTransform::OSTN15 ? 1 : 0);
    UpdateMlDisplay();
}

void NetworkConfigPanel::SaveToScenario() {
    if (!scenario_) return;
    double f1 = wxAtof(f1_field_->GetValue());
    double f2 = wxAtof(f2_field_->GetValue());
    if (f1 >= F_MIN_KHZ && f1 <= F_MAX_KHZ) scenario_->frequencies.f1_hz = f1 * 1000.0;
    if (f2 >= F_MIN_KHZ && f2 <= F_MAX_KHZ) scenario_->frequencies.f2_hz = f2 * 1000.0;
    scenario_->frequencies.recompute();
    scenario_->grid.resolution_km = wxAtof(res_field_->GetValue());
    scenario_->mode = (mode_->GetSelection() == 1) ? Scenario::OperationMode::Interlaced
                                                    : Scenario::OperationMode::EightSlot;
    scenario_->receiver.mode = (rx_model_->GetSelection() == 1) ? ReceiverModel::Mode::Advanced
                                                                  : ReceiverModel::Mode::Simple;
    scenario_->datum_transform = (datum_->GetSelection() == 1) ? Scenario::DatumTransform::OSTN15
                                                                : Scenario::DatumTransform::Helmert;
}

void NetworkConfigPanel::OnFreqChanged(wxCommandEvent& /*evt*/) {
    ValidateFreqFields();
    UpdateMlDisplay();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnOtherChanged(wxCommandEvent& /*evt*/) {
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnDebounceTimer(wxTimerEvent& /*evt*/) {
    if (!scenario_ || !on_changed) return;
    SaveToScenario();
    on_changed(*scenario_);
}

void NetworkConfigPanel::ValidateFreqFields() {
    auto validate = [&](wxTextCtrl* field) {
        double v = wxAtof(field->GetValue());
        bool ok = (v >= F_MIN_KHZ && v <= F_MAX_KHZ);
        field->SetBackgroundColour(ok ? wxNullColour : *wxRED);
        field->Refresh();
    };
    validate(f1_field_);
    validate(f2_field_);
}

void NetworkConfigPanel::UpdateMlDisplay() {
    double f1_khz = wxAtof(f1_field_->GetValue());
    double f2_khz = wxAtof(f2_field_->GetValue());
    constexpr double C = 299792458.0;
    double ml_f1_m = (f1_khz > 0) ? (C / (f1_khz * 1000.0)) / 1000.0 : 0.0;
    double ml_f2_m = (f2_khz > 0) ? (C / (f2_khz * 1000.0)) / 1000.0 : 0.0;
    ml_label_->SetLabel(wxString::Format("1 ml(f1) = %.3f m   1 ml(f2) = %.3f m",
                                         ml_f1_m, ml_f2_m));
}

} // namespace bp
