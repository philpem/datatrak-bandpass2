#include "NetworkConfigPanel.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <cmath>

namespace bp {

NetworkConfigPanel::NetworkConfigPanel(wxWindow* parent)
    : wxScrolledWindow(parent)
{
    debounce_.Bind(wxEVT_TIMER, &NetworkConfigPanel::OnDebounceTimer, this);

    auto* gs = new wxFlexGridSizer(2, 4, 6);
    gs->AddGrowableCol(1);

    // F1
    gs->Add(new wxStaticText(this, wxID_ANY, "F1 (kHz)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    f1_field_ = new wxTextCtrl(this, wxID_ANY, "146.4375");
    f1_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnFreqChanged,    this);
    f1_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);
    gs->Add(f1_field_, 1, wxEXPAND | wxBOTTOM, 2);

    // F2
    gs->Add(new wxStaticText(this, wxID_ANY, "F2 (kHz)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    f2_field_ = new wxTextCtrl(this, wxID_ANY, "131.2500");
    f2_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnFreqChanged,    this);
    f2_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);
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

    // Datum
    gs->Add(new wxStaticText(this, wxID_ANY, "Datum"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    wxArrayString datums;
    datums.Add("Helmert"); datums.Add("OSTN15");
    datum_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, datums);
    datum_->SetSelection(0);
    datum_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnOtherChanged, this);
    gs->Add(datum_, 1, wxEXPAND | wxBOTTOM, 2);

    // ── Grid bounds ──
    gs->Add(new wxStaticText(this, wxID_ANY, "Lat min"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    lat_min_field_ = new wxTextCtrl(this, wxID_ANY, "49.5");
    lat_min_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnBoundsChanged,  this);
    lat_min_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);
    gs->Add(lat_min_field_, 1, wxEXPAND | wxBOTTOM, 2);

    gs->Add(new wxStaticText(this, wxID_ANY, "Lat max"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    lat_max_field_ = new wxTextCtrl(this, wxID_ANY, "61.0");
    lat_max_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnBoundsChanged,  this);
    lat_max_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);
    gs->Add(lat_max_field_, 1, wxEXPAND | wxBOTTOM, 2);

    gs->Add(new wxStaticText(this, wxID_ANY, "Lon min"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    lon_min_field_ = new wxTextCtrl(this, wxID_ANY, "-7.0");
    lon_min_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnBoundsChanged,  this);
    lon_min_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);
    gs->Add(lon_min_field_, 1, wxEXPAND | wxBOTTOM, 2);

    gs->Add(new wxStaticText(this, wxID_ANY, "Lon max"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    lon_max_field_ = new wxTextCtrl(this, wxID_ANY, "2.5");
    lon_max_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnBoundsChanged,  this);
    lon_max_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);
    gs->Add(lon_max_field_, 1, wxEXPAND | wxBOTTOM, 2);

    // ── Point-count limit ──
    gs->Add(new wxStaticText(this, wxID_ANY, "Max points"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    max_pts_field_ = new wxTextCtrl(this, wxID_ANY, "10000");
    max_pts_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnMaxPtsChanged,  this);
    max_pts_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);
    gs->Add(max_pts_field_, 1, wxEXPAND | wxBOTTOM, 2);

    // Resolution info
    gs->Add(new wxStaticText(this, wxID_ANY, ""), 0);
    res_info_label_ = new wxStaticText(this, wxID_ANY, "");
    gs->Add(res_info_label_, 0, wxBOTTOM, 6);

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
    lat_min_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lat_min));
    lat_max_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lat_max));
    lon_min_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lon_min));
    lon_max_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lon_max));
    max_pts_field_->ChangeValue(wxString::Format("%d",   scenario_->grid.max_points));
    mode_->SetSelection(scenario_->mode == Scenario::OperationMode::Interlaced ? 1 : 0);
    datum_->SetSelection(scenario_->datum_transform == Scenario::DatumTransform::OSTN15 ? 1 : 0);
    UpdateMlDisplay();
    ValidateBoundsFields();
    ValidateMaxPtsField();
    UpdateResInfoDisplay();
}

void NetworkConfigPanel::SaveToScenario() {
    if (!scenario_) return;
    double f1 = wxAtof(f1_field_->GetValue());
    double f2 = wxAtof(f2_field_->GetValue());
    if (f1 >= F_MIN_KHZ && f1 <= F_MAX_KHZ) scenario_->frequencies.f1_hz = f1 * 1000.0;
    if (f2 >= F_MIN_KHZ && f2 <= F_MAX_KHZ) scenario_->frequencies.f2_hz = f2 * 1000.0;
    scenario_->frequencies.recompute();

    double lat_min = wxAtof(lat_min_field_->GetValue());
    double lat_max = wxAtof(lat_max_field_->GetValue());
    double lon_min = wxAtof(lon_min_field_->GetValue());
    double lon_max = wxAtof(lon_max_field_->GetValue());
    if (lat_min >= -90.0 && lat_max <= 90.0 && lat_min < lat_max &&
        lon_min >= -180.0 && lon_max <= 180.0 && lon_min < lon_max) {
        scenario_->grid.lat_min = lat_min;
        scenario_->grid.lat_max = lat_max;
        scenario_->grid.lon_min = lon_min;
        scenario_->grid.lon_max = lon_max;
    }

    long pts = 0;
    if (max_pts_field_->GetValue().ToLong(&pts) && pts >= PTS_MIN && pts <= PTS_MAX)
        scenario_->grid.max_points = (int)pts;

    scenario_->mode = (mode_->GetSelection() == 1) ? Scenario::OperationMode::Interlaced
                                                    : Scenario::OperationMode::EightSlot;
    scenario_->datum_transform = (datum_->GetSelection() == 1) ? Scenario::DatumTransform::OSTN15
                                                                : Scenario::DatumTransform::Helmert;
}

void NetworkConfigPanel::SetBoundsFromMap(double lat_min, double lat_max,
                                           double lon_min, double lon_max) {
    lat_min_field_->ChangeValue(wxString::Format("%.4f", lat_min));
    lat_max_field_->ChangeValue(wxString::Format("%.4f", lat_max));
    lon_min_field_->ChangeValue(wxString::Format("%.4f", lon_min));
    lon_max_field_->ChangeValue(wxString::Format("%.4f", lon_max));
    ValidateBoundsFields();
    UpdateResInfoDisplay();
}

void NetworkConfigPanel::OnFreqChanged(wxCommandEvent& /*evt*/) {
    ValidateFreqFields();
    UpdateMlDisplay();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnBoundsChanged(wxCommandEvent& /*evt*/) {
    ValidateBoundsFields();
    UpdateResInfoDisplay();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnMaxPtsChanged(wxCommandEvent& /*evt*/) {
    ValidateMaxPtsField();
    UpdateResInfoDisplay();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnOtherChanged(wxCommandEvent& /*evt*/) {
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::FlushPending() {
    if (!debounce_.IsRunning()) return;
    debounce_.Stop();
    SaveToScenario();
}

void NetworkConfigPanel::OnFieldKillFocus(wxFocusEvent& evt) {
    evt.Skip();
    if (!debounce_.IsRunning()) return;
    debounce_.Stop();
    if (!scenario_ || !on_changed) return;
    SaveToScenario();
    on_changed(*scenario_);
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

void NetworkConfigPanel::ValidateBoundsFields() {
    double lat_min = wxAtof(lat_min_field_->GetValue());
    double lat_max = wxAtof(lat_max_field_->GetValue());
    double lon_min = wxAtof(lon_min_field_->GetValue());
    double lon_max = wxAtof(lon_max_field_->GetValue());
    bool lat_ok = (lat_min >= -90.0 && lat_max <= 90.0 && lat_min < lat_max);
    bool lon_ok = (lon_min >= -180.0 && lon_max <= 180.0 && lon_min < lon_max);
    lat_min_field_->SetBackgroundColour(lat_ok ? wxNullColour : *wxRED); lat_min_field_->Refresh();
    lat_max_field_->SetBackgroundColour(lat_ok ? wxNullColour : *wxRED); lat_max_field_->Refresh();
    lon_min_field_->SetBackgroundColour(lon_ok ? wxNullColour : *wxRED); lon_min_field_->Refresh();
    lon_max_field_->SetBackgroundColour(lon_ok ? wxNullColour : *wxRED); lon_max_field_->Refresh();
}

void NetworkConfigPanel::ValidateMaxPtsField() {
    long pts = 0;
    bool ok = max_pts_field_->GetValue().ToLong(&pts) && pts >= PTS_MIN && pts <= PTS_MAX;
    max_pts_field_->SetBackgroundColour(ok ? wxNullColour : *wxRED);
    max_pts_field_->Refresh();
}

void NetworkConfigPanel::UpdateResInfoDisplay() {
    long pts = 0;
    if (!max_pts_field_->GetValue().ToLong(&pts) || pts < PTS_MIN || pts > PTS_MAX) {
        res_info_label_->SetLabel("");
        return;
    }
    double lat_min = wxAtof(lat_min_field_->GetValue());
    double lat_max = wxAtof(lat_max_field_->GetValue());
    double lon_min = wxAtof(lon_min_field_->GetValue());
    double lon_max = wxAtof(lon_max_field_->GetValue());
    if (lat_min >= lat_max || lon_min >= lon_max) {
        res_info_label_->SetLabel("");
        return;
    }
    double mid_lat = (lat_min + lat_max) / 2.0;
    double cos_mid = std::cos(mid_lat * M_PI / 180.0);
    double lat_range_km = (lat_max - lat_min) * 110.574;
    double lon_range_km = (cos_mid > 1e-6) ? (lon_max - lon_min) * 111.320 * cos_mid : lat_range_km;
    double area_km2 = std::max(lat_range_km * lon_range_km, 1.0);
    double res_km = std::sqrt(area_km2 / pts);
    if (res_km < 0.05) res_km = 0.05;
    wxString label = wxString::Format("~%.2f km spacing", res_km);
    res_info_label_->SetLabel(label);
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
