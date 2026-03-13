#include "ParamEditor.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace bp {

static wxTextCtrl* MakeField(wxWindow* parent, const char* label, wxSizer* sizer) {
    sizer->Add(new wxStaticText(parent, wxID_ANY, wxString::FromUTF8(label)),
               0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    auto* tc = new wxTextCtrl(parent, wxID_ANY);
    sizer->Add(tc, 1, wxEXPAND | wxBOTTOM, 4);
    return tc;
}

ParamEditor::ParamEditor(wxWindow* parent)
    : wxPanel(parent)
{
    auto* outer = new wxBoxSizer(wxVERTICAL);
    notebook_ = new wxNotebook(this, wxID_ANY);

    // Transmitter page
    auto* tx_page = new wxScrolledWindow(notebook_);
    BuildTransmitterPage(tx_page);
    tx_page->SetScrollRate(0, 10);
    tx_page->FitInside();
    notebook_->AddPage(tx_page, "Transmitter");

    // Receiver page
    auto* rx_page = new wxScrolledWindow(notebook_);
    BuildReceiverPage(rx_page);
    rx_page->SetScrollRate(0, 10);
    rx_page->FitInside();
    notebook_->AddPage(rx_page, "Receiver");

    outer->Add(notebook_, 1, wxEXPAND);
    SetSizer(outer);
}

void ParamEditor::BuildTransmitterPage(wxWindow* page) {
    auto* gs = new wxFlexGridSizer(2, 4, 4);
    gs->AddGrowableCol(1);

    tx_name_   = MakeField(page, "Name",            gs);
    tx_lat_    = MakeField(page, "Lat (WGS84)",     gs);
    tx_lon_    = MakeField(page, "Lon (WGS84)",     gs);
    tx_power_  = MakeField(page, "Power (W)",       gs);
    tx_height_ = MakeField(page, "Height (m)",      gs);

    gs->Add(new wxStaticText(page, wxID_ANY, "Slot"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_slot_ = new wxSpinCtrl(page, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS, 1, 24, 1);
    gs->Add(tx_slot_, 1, wxEXPAND | wxBOTTOM, 4);

    gs->Add(new wxStaticText(page, wxID_ANY, "Master"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_master_ = new wxCheckBox(page, wxID_ANY, "");
    gs->Add(tx_master_, 0, wxBOTTOM, 4);

    gs->Add(new wxStaticText(page, wxID_ANY, "Master slot"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_mslot_ = new wxSpinCtrl(page, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, 0, 24, 0);
    gs->Add(tx_mslot_, 1, wxEXPAND | wxBOTTOM, 4);

    tx_spo_   = MakeField(page, "SPO (\xce\xbcs)",         gs);
    tx_spo_->SetToolTip("System Phase Offset: fine phase alignment applied at the "
                        "transmitter to correct the slot timing relative to the master.");
    tx_delay_ = MakeField(page, "Station delay (\xce\xbcs)", gs);
    tx_delay_->SetToolTip("Propagation delay of the reference signal from the master "
                          "transmitter to this station (synchronisation cable/radio link).");

    gs->Add(new wxStaticText(page, wxID_ANY, "Lock position"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_locked_ = new wxCheckBox(page, wxID_ANY, "");
    gs->Add(tx_locked_, 0, wxBOTTOM, 4);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(gs, 0, wxALL | wxEXPAND, 8);
    page->SetSizer(sizer);

    // Bind change events
    for (auto* tc : {tx_name_, tx_lat_, tx_lon_, tx_power_, tx_height_, tx_spo_, tx_delay_}) {
        tc->Bind(wxEVT_TEXT, &ParamEditor::OnTxField, this);
    }
    tx_slot_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&){
        wxCommandEvent e; OnTxField(e); });
    tx_master_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&){
        wxCommandEvent e; OnTxField(e); });
    tx_mslot_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&){
        wxCommandEvent e; OnTxField(e); });
    tx_locked_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (updating_ || current_tx_id_ < 0 || !on_tx_lock_changed) return;
        on_tx_lock_changed(current_tx_id_, tx_locked_->GetValue());
    });
}

void ParamEditor::BuildReceiverPage(wxWindow* page) {
    auto* gs = new wxFlexGridSizer(2, 4, 4);
    gs->AddGrowableCol(1);

    gs->Add(new wxStaticText(page, wxID_ANY, "Mode"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    wxArrayString modes;
    modes.Add("Simple"); modes.Add("Advanced");
    rx_mode_ = new wxChoice(page, wxID_ANY, wxDefaultPosition, wxDefaultSize, modes);
    rx_mode_->SetSelection(0);
    gs->Add(rx_mode_, 1, wxEXPAND | wxBOTTOM, 4);
    rx_mode_->Bind(wxEVT_CHOICE, &ParamEditor::OnRxMode, this);

    rx_noise_   = MakeField(page, "Noise floor (dB\xc2\xb5V/m)",   gs);
    rx_noise_->SetToolTip("Minimum detectable signal; sets the receive sensitivity "
                          "floor (ITU-R P.372 atmospheric noise at this site).");
    rx_vnoise_  = MakeField(page, "Vehicle noise (dB\xc2\xb5V/m)", gs);
    rx_vnoise_->SetToolTip("In-vehicle conducted/radiated noise floor. Added (power "
                           "sum) to atmospheric noise to give total receiver noise.");
    rx_range_   = MakeField(page, "Max range (km)",         gs);
    rx_range_->SetToolTip("Hard range limit; grid points beyond this distance from "
                          "every transmitter are excluded from WHDOP computation.");

    gs->Add(new wxStaticText(page, wxID_ANY, "Min stations"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    rx_minstns_ = new wxSpinCtrl(page, wxID_ANY, "4", wxDefaultPosition, wxDefaultSize,
                                  wxSP_ARROW_KEYS, 2, 8, 4);
    gs->Add(rx_minstns_, 1, wxEXPAND | wxBOTTOM, 4);

    gs->Add(new wxStaticText(page, wxID_ANY, "Lock position"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    rx_locked_ = new wxCheckBox(page, wxID_ANY, "");
    gs->Add(rx_locked_, 0, wxBOTTOM, 4);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(gs, 0, wxALL | wxEXPAND, 8);
    page->SetSizer(sizer);

    for (auto* tc : {rx_noise_, rx_vnoise_, rx_range_}) {
        tc->Bind(wxEVT_TEXT, &ParamEditor::OnRxField, this);
    }
    rx_minstns_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&){
        wxCommandEvent e; OnRxField(e); });
    rx_locked_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (updating_ || !on_rx_lock_changed) return;
        on_rx_lock_changed(rx_locked_->GetValue());
    });
}

void ParamEditor::LoadTransmitter(int id, const Transmitter& tx) {
    updating_ = true;
    current_tx_id_ = id;
    tx_name_->ChangeValue(tx.name);
    tx_lat_->ChangeValue(wxString::Format("%.6f", tx.lat));
    tx_lon_->ChangeValue(wxString::Format("%.6f", tx.lon));
    tx_power_->ChangeValue(wxString::Format("%.1f", tx.power_w));
    tx_height_->ChangeValue(wxString::Format("%.1f", tx.height_m));
    tx_slot_->SetValue(tx.slot);
    tx_master_->SetValue(tx.is_master);
    tx_mslot_->SetValue(tx.master_slot);
    tx_spo_->ChangeValue(wxString::Format("%.3f", tx.spo_us));
    tx_delay_->ChangeValue(wxString::Format("%.3f", tx.station_delay_us));
    tx_locked_->SetValue(tx.locked);
    notebook_->SetSelection(0);
    updating_ = false;
}

void ParamEditor::LoadReceiver(const ReceiverModel& rx) {
    updating_ = true;
    rx_mode_->SetSelection(rx.mode == ReceiverModel::Mode::Advanced ? 1 : 0);
    rx_noise_->ChangeValue(wxString::Format("%.1f", rx.noise_floor_dbuvpm));
    rx_vnoise_->ChangeValue(wxString::Format("%.1f", rx.vehicle_noise_dbuvpm));
    rx_range_->ChangeValue(wxString::Format("%.1f", rx.max_range_km));
    rx_minstns_->SetValue(rx.min_stations);
    notebook_->SetSelection(1);
    updating_ = false;
}

void ParamEditor::ClearSelection() {
    updating_ = true;
    current_tx_id_ = -1;
    tx_name_->ChangeValue("");
    tx_lat_->ChangeValue("");
    tx_lon_->ChangeValue("");
    updating_ = false;
}

void ParamEditor::OnTxField(wxCommandEvent& /*evt*/) {
    if (updating_ || current_tx_id_ < 0 || !on_transmitter_changed) return;
    Transmitter tx;
    tx.name             = tx_name_->GetValue().ToStdString();
    tx.lat              = wxAtof(tx_lat_->GetValue());
    tx.lon              = wxAtof(tx_lon_->GetValue());
    tx.power_w          = wxAtof(tx_power_->GetValue());
    tx.height_m         = wxAtof(tx_height_->GetValue());
    tx.slot             = tx_slot_->GetValue();
    tx.is_master        = tx_master_->GetValue();
    tx.master_slot      = tx_mslot_->GetValue();
    tx.spo_us           = wxAtof(tx_spo_->GetValue());
    tx.station_delay_us = wxAtof(tx_delay_->GetValue());
    tx.locked           = tx_locked_->GetValue();
    on_transmitter_changed(current_tx_id_, tx);
}

void ParamEditor::OnRxField(wxCommandEvent& /*evt*/) {
    if (updating_ || !on_receiver_changed) return;
    wxCommandEvent e; OnRxMode(e);
}

void ParamEditor::OnRxMode(wxCommandEvent& /*evt*/) {
    if (updating_ || !on_receiver_changed) return;
    ReceiverModel rx;
    rx.mode = (rx_mode_->GetSelection() == 1) ? ReceiverModel::Mode::Advanced
                                               : ReceiverModel::Mode::Simple;
    rx.noise_floor_dbuvpm   = wxAtof(rx_noise_->GetValue());
    rx.vehicle_noise_dbuvpm = wxAtof(rx_vnoise_->GetValue());
    rx.max_range_km         = wxAtof(rx_range_->GetValue());
    rx.min_stations         = rx_minstns_->GetValue();
    on_receiver_changed(rx);
}

} // namespace bp
