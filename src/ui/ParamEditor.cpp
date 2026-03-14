#include "ParamEditor.h"
#include "UiConstants.h"
#include <string>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <GeographicLib/Geodesic.hpp>
#include <cmath>

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
    tx_height_ = MakeField(page, "Height AGL (m)",  gs);
    tx_height_->SetToolTip("Antenna height above ground level (AGL), in metres. "
                           "This is the physical height of the radiating element above "
                           "the local terrain surface, not above sea level. "
                           "Used as the effective vertical-monopole height in the "
                           "propagation model.");

    gs->Add(new wxStaticText(page, wxID_ANY, "Slot"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_slot_ = new wxSpinCtrl(page, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS, 1, 24, 1);
    tx_slot_->SetToolTip("Slot number (1-24). Each slot is one transmission per cycle. "
                         "To model a physical site that transmits on multiple slots, "
                         "add a separate transmitter entry at the same location for each slot.");
    gs->Add(tx_slot_, 1, wxEXPAND | wxBOTTOM, 4);

    gs->Add(new wxStaticText(page, wxID_ANY, "Is master"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_master_ = new wxCheckBox(page, wxID_ANY, "");
    tx_master_->SetToolTip("Check if this transmitter is the chain master (slot-0 reference). "
                           "Only one transmitter per chain should be marked as master.");
    gs->Add(tx_master_, 0, wxBOTTOM, 4);

    gs->Add(new wxStaticText(page, wxID_ANY, "Master slot"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_mslot_choice_ = new wxChoice(page, wxID_ANY);
    tx_mslot_choice_->SetToolTip("The master transmitter whose slot timing this station slaves to. "
                                 "Set to None only if this station is itself the master.");
    tx_mslot_choice_->Append("None (is master)");
    master_slot_values_.push_back(0);
    gs->Add(tx_mslot_choice_, 1, wxEXPAND | wxBOTTOM, 4);

    // SPO row: text field + estimate button
    gs->Add(new wxStaticText(page, wxID_ANY, wxString::FromUTF8(
                std::string("SPO (") + bp::ui::MICROSEC + ")")),
            0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        tx_spo_ = new wxTextCtrl(page, wxID_ANY);
        tx_spo_->SetToolTip(wxString::FromUTF8(
            (std::string("System Phase Offset (") + bp::ui::MICROSEC +
            "): fine phase alignment applied at the "
            "transmitter to correct the slot timing relative to the master.\n\n"
            "During commissioning this is measured and trimmed. For initial planning "
            "click \"Estimate\" to compute the SPO that makes the received "
            "phase from this slave an integer number of lanes at the master site "
            "(free-space, ignoring ASF).").c_str()));
        btn_spo_calc_ = new wxButton(page, wxID_ANY, "Estimate",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        btn_spo_calc_->SetToolTip(
            "Estimate SPO from geometry: computes the value that would make the "
            "phase received from this slave at the master transmitter's position "
            "a round number of lanes (free-space propagation, current station delay "
            "included).\n\nThis is an initial planning estimate; the actual SPO is "
            "calibrated on-site during commissioning.");
        row->Add(tx_spo_,       1, wxEXPAND | wxRIGHT, 4);
        row->Add(btn_spo_calc_, 0, wxALIGN_CENTER_VERTICAL);
        gs->Add(row, 1, wxEXPAND | wxBOTTOM, 4);
    }

    tx_delay_ = MakeField(page, (std::string("Station delay (") + bp::ui::MICROSEC + ")").c_str(), gs);
    tx_delay_->SetToolTip("Propagation delay of the reference signal from the master "
                          "transmitter to this station (synchronisation cable/radio link).");

    gs->Add(new wxStaticText(page, wxID_ANY, "Lock position"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_locked_ = new wxCheckBox(page, wxID_ANY, "");
    gs->Add(tx_locked_, 0, wxBOTTOM, 4);

    tx_delete_ = new wxButton(page, wxID_ANY, "Delete Transmitter");
    tx_delete_->Enable(false);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(gs, 0, wxALL | wxEXPAND, 8);
    sizer->Add(tx_delete_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    page->SetSizer(sizer);

    tx_delete_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (current_tx_id_ >= 0 && on_transmitter_deleted)
            on_transmitter_deleted(current_tx_id_);
    });

    // Bind change events (tx_spo_ is created outside MakeField so listed explicitly)
    for (auto* tc : {tx_name_, tx_lat_, tx_lon_, tx_power_, tx_height_, tx_spo_, tx_delay_}) {
        tc->Bind(wxEVT_TEXT, &ParamEditor::OnTxField, this);
    }
    tx_slot_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&){
        wxCommandEvent e; OnTxField(e); });
    tx_master_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&){
        UpdateMasterSlotState();
        UpdateSpoCalcState();
        wxCommandEvent e; OnTxField(e); });
    tx_mslot_choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&){
        UpdateSpoCalcState();
        wxCommandEvent e; OnTxField(e); });
    btn_spo_calc_->Bind(wxEVT_BUTTON, &ParamEditor::OnCalcSPO, this);
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
    rx_mode_->SetToolTip(
        "Simple: uses a single noise floor (atmospheric only).\n"
        "Suitable for quick coverage planning where vehicle noise is not the "
        "dominant factor.\n\n"
        "Advanced: combines atmospheric and vehicle noise floors in a power sum, "
        "giving a more accurate noise model for in-vehicle positioning predictions.");
    gs->Add(rx_mode_, 1, wxEXPAND | wxBOTTOM, 4);
    rx_mode_->Bind(wxEVT_CHOICE, &ParamEditor::OnRxMode, this);

    rx_noise_   = MakeField(page, (std::string("Noise floor (") + bp::ui::DBUVM + ")").c_str(), gs);
    rx_noise_->SetToolTip("Minimum detectable signal; sets the receive sensitivity "
                          "floor (ITU-R P.372 atmospheric noise at this site).");
    rx_vnoise_  = MakeField(page, (std::string("Vehicle noise (") + bp::ui::DBUVM + ")").c_str(), gs);
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
    tx_spo_->ChangeValue(wxString::Format("%.3f", tx.spo_us));
    tx_delay_->ChangeValue(wxString::Format("%.3f", tx.station_delay_us));
    tx_locked_->SetValue(tx.locked);
    tx_delete_->Enable(true);
    notebook_->SetSelection(0);
    updating_ = false;

    RebuildMasterSlotChoices(id);
    // Select the entry matching tx.master_slot (default to index 0 = None)
    int sel = 0;
    for (int i = 0; i < (int)master_slot_values_.size(); ++i) {
        if (master_slot_values_[i] == tx.master_slot) { sel = i; break; }
    }
    tx_mslot_choice_->SetSelection(sel);
    UpdateMasterSlotState();
    UpdateSpoCalcState();
}

void ParamEditor::LoadReceiver(const ReceiverModel& rx) {
    current_rx_ = rx;   // snapshot — preserves vp_ms, ellipsoid etc. across edits
    updating_ = true;
    rx_mode_->SetSelection(rx.mode == ReceiverModel::Mode::Advanced ? 1 : 0);
    rx_noise_->ChangeValue(wxString::Format("%.1f", rx.noise_floor_dbuvpm));
    rx_vnoise_->ChangeValue(wxString::Format("%.1f", rx.vehicle_noise_dbuvpm));
    rx_range_->ChangeValue(wxString::Format("%.1f", rx.max_range_km));
    rx_minstns_->SetValue(rx.min_stations);
    notebook_->SetSelection(1);
    updating_ = false;
    UpdateRxFieldStates();
}

void ParamEditor::ClearSelection() {
    updating_ = true;
    current_tx_id_ = -1;
    tx_name_->ChangeValue("");
    tx_lat_->ChangeValue("");
    tx_lon_->ChangeValue("");
    tx_delete_->Enable(false);
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
    {
        int sel = tx_mslot_choice_->GetSelection();
        tx.master_slot = (sel >= 0 && sel < (int)master_slot_values_.size())
                         ? master_slot_values_[sel] : 0;
    }
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
    UpdateRxFieldStates();
    if (updating_ || !on_receiver_changed) return;
    // Start from the stored snapshot so that fields not shown in the form
    // (vp_ms, ellipsoid) are preserved rather than reset to defaults.
    ReceiverModel rx = current_rx_;
    rx.mode = (rx_mode_->GetSelection() == 1) ? ReceiverModel::Mode::Advanced
                                               : ReceiverModel::Mode::Simple;
    rx.noise_floor_dbuvpm   = wxAtof(rx_noise_->GetValue());
    rx.vehicle_noise_dbuvpm = wxAtof(rx_vnoise_->GetValue());
    rx.max_range_km         = wxAtof(rx_range_->GetValue());
    rx.min_stations         = rx_minstns_->GetValue();
    current_rx_ = rx;
    on_receiver_changed(rx);
}

void ParamEditor::SetTransmitterList(const std::vector<Transmitter>& txs) {
    tx_list_ = txs;
    if (current_tx_id_ < 0) return;
    // Remember the currently selected master slot value, rebuild, then restore
    int current_master = 0;
    int sel = tx_mslot_choice_->GetSelection();
    if (sel >= 0 && sel < (int)master_slot_values_.size())
        current_master = master_slot_values_[sel];
    RebuildMasterSlotChoices(current_tx_id_);
    int new_sel = 0;
    for (int i = 0; i < (int)master_slot_values_.size(); ++i) {
        if (master_slot_values_[i] == current_master) { new_sel = i; break; }
    }
    tx_mslot_choice_->SetSelection(new_sel);
}

void ParamEditor::RebuildMasterSlotChoices(int current_id) {
    tx_mslot_choice_->Clear();
    master_slot_values_.clear();

    tx_mslot_choice_->Append("None (is master)");
    master_slot_values_.push_back(0);

    for (int i = 0; i < (int)tx_list_.size(); ++i) {
        if (i == current_id) continue;  // don't offer self as master
        const auto& t = tx_list_[i];
        wxString label = wxString::Format("Slot %d - %s",
                                          t.slot, wxString::FromUTF8(t.name).c_str());
        tx_mslot_choice_->Append(label);
        master_slot_values_.push_back(t.slot);
    }
}

void ParamEditor::UpdateMasterSlotState() {
    // When this transmitter IS the master it has no upstream slot to slave to.
    tx_mslot_choice_->Enable(!tx_master_->GetValue());
}

void ParamEditor::UpdateSpoCalcState() {
    // Enable "Estimate" only when a master has been selected and this is not
    // the master itself.
    bool is_master = tx_master_->GetValue();
    int sel = tx_mslot_choice_->GetSelection();
    bool has_master = !is_master && sel > 0 && sel < (int)master_slot_values_.size();
    btn_spo_calc_->Enable(has_master);
}

void ParamEditor::OnCalcSPO(wxCommandEvent& /*evt*/) {
    // Find the master transmitter in tx_list_ by the chosen slot number.
    int sel = tx_mslot_choice_->GetSelection();
    if (sel <= 0 || sel >= (int)master_slot_values_.size()) return;
    int master_slot = master_slot_values_[sel];

    const Transmitter* master_tx = nullptr;
    for (const auto& t : tx_list_) {
        if (t.slot == master_slot) { master_tx = &t; break; }
    }
    if (!master_tx) return;

    // Current slave position (may not yet be committed to tx_list_)
    double slave_lat = wxAtof(tx_lat_->GetValue());
    double slave_lon = wxAtof(tx_lon_->GetValue());

    // WGS84 geodesic distance (slave → master).
    // Using WGS84 here (not Airy) since this is a planning estimate.
    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    double dist_m = 0.0;
    geod.Inverse(slave_lat, slave_lon, master_tx->lat, master_tx->lon, dist_m);
    dist_m = std::abs(dist_m);

    constexpr double c = 299'792'458.0;
    double station_delay_s = wxAtof(tx_delay_->GetValue()) * 1e-6;

    // Total delay from slave transmitting → received at master site (free-space)
    double total_delay_s = dist_m / c + station_delay_s;

    // Phase at master (lanes): fmod gives fractional part in [0, 1)
    double phase_lanes = std::fmod(total_delay_s * f1_hz_, 1.0);

    // Choose smallest-magnitude correction: map to [-0.5, 0.5)
    if (phase_lanes > 0.5) phase_lanes -= 1.0;

    // SPO to cancel the fractional phase
    double spo_us = -phase_lanes / f1_hz_ * 1e6;

    updating_ = true;
    tx_spo_->ChangeValue(wxString::Format("%.3f", spo_us));
    updating_ = false;
    // Fire the changed callback so the scenario is updated
    wxCommandEvent e; OnTxField(e);
}

void ParamEditor::UpdateRxFieldStates() {
    bool advanced = (rx_mode_->GetSelection() == 1);
    // Vehicle noise is only applicable in Advanced mode, where it is combined
    // with the atmospheric noise floor in a power sum.  In Simple mode the
    // noise_floor field represents the total receiver noise, so vehicle noise
    // has no separate role and the field is disabled to avoid confusion.
    rx_vnoise_->Enable(advanced);
}

} // namespace bp
