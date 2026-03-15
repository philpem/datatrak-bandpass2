#include "ParamEditor.h"
#include "UiConstants.h"
#include <string>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
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
    auto* outer_sizer = new wxBoxSizer(wxVERTICAL);

    // ── Site properties ────────────────────────────────────────────────────
    auto* site_box    = new wxStaticBox(page, wxID_ANY, "Site");
    auto* site_sizer  = new wxStaticBoxSizer(site_box, wxVERTICAL);
    auto* site_gs     = new wxFlexGridSizer(2, 4, 4);
    site_gs->AddGrowableCol(1);

    tx_name_   = MakeField(page, "Name",            site_gs);
    tx_lat_    = MakeField(page, "Lat (WGS84)",     site_gs);
    tx_lon_    = MakeField(page, "Lon (WGS84)",     site_gs);
    tx_power_  = MakeField(page, "Power (W)",       site_gs);
    tx_height_ = MakeField(page, "Height AGL (m)",  site_gs);
    tx_height_->SetToolTip("Antenna height above ground level (AGL), in metres.");

    site_gs->Add(new wxStaticText(page, wxID_ANY, "Lock position"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_locked_ = new wxCheckBox(page, wxID_ANY, "");
    site_gs->Add(tx_locked_, 0, wxBOTTOM, 4);

    site_sizer->Add(site_gs, 0, wxEXPAND | wxALL, 4);
    outer_sizer->Add(site_sizer, 0, wxEXPAND | wxALL, 6);

    // ── Slots ──────────────────────────────────────────────────────────────
    auto* slots_box   = new wxStaticBox(page, wxID_ANY, "Slots");
    auto* slots_sizer = new wxStaticBoxSizer(slots_box, wxVERTICAL);

    auto* list_row = new wxBoxSizer(wxHORIZONTAL);
    tx_slot_list_ = new wxListBox(page, wxID_ANY, wxDefaultPosition,
                                   wxSize(-1, 72), 0, nullptr, wxLB_SINGLE);
    list_row->Add(tx_slot_list_, 1, wxEXPAND | wxRIGHT, 4);

    auto* btn_col = new wxBoxSizer(wxVERTICAL);
    btn_add_slot_    = new wxButton(page, wxID_ANY, "Add",    wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    btn_remove_slot_ = new wxButton(page, wxID_ANY, "Remove", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    btn_add_slot_->SetToolTip("Add a new slot to this site (colocated transmitter).");
    btn_remove_slot_->SetToolTip("Remove the selected slot from this site.");
    btn_col->Add(btn_add_slot_,    0, wxBOTTOM, 4);
    btn_col->Add(btn_remove_slot_, 0);
    list_row->Add(btn_col, 0, wxALIGN_TOP);
    slots_sizer->Add(list_row, 0, wxEXPAND | wxALL, 4);
    outer_sizer->Add(slots_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // ── Per-slot properties ────────────────────────────────────────────────
    auto* slot_box    = new wxStaticBox(page, wxID_ANY, "Selected Slot");
    auto* slot_sizer  = new wxStaticBoxSizer(slot_box, wxVERTICAL);
    auto* slot_gs     = new wxFlexGridSizer(2, 4, 4);
    slot_gs->AddGrowableCol(1);

    slot_gs->Add(new wxStaticText(page, wxID_ANY, "Slot #"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_slot_num_ = new wxSpinCtrl(page, wxID_ANY, "1", wxDefaultPosition, wxDefaultSize,
                                   wxSP_ARROW_KEYS, 1, 24, 1);
    tx_slot_num_->SetToolTip("Slot number (1-24). Each slot is one transmission per cycle.");
    slot_gs->Add(tx_slot_num_, 1, wxEXPAND | wxBOTTOM, 4);

    slot_gs->Add(new wxStaticText(page, wxID_ANY, "Is master"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_master_ = new wxCheckBox(page, wxID_ANY, "");
    tx_master_->SetToolTip("Check if this slot is the chain master (slot-0 reference).");
    slot_gs->Add(tx_master_, 0, wxBOTTOM, 4);

    slot_gs->Add(new wxStaticText(page, wxID_ANY, "Master slot"),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    tx_mslot_choice_ = new wxChoice(page, wxID_ANY);
    tx_mslot_choice_->SetToolTip("The master slot this slot slaves to. Set to None only if this is the master.");
    tx_mslot_choice_->Append("None (is master)");
    master_slot_values_.push_back(0);
    slot_gs->Add(tx_mslot_choice_, 1, wxEXPAND | wxBOTTOM, 4);

    // SPO row: text field + estimate button
    slot_gs->Add(new wxStaticText(page, wxID_ANY, wxString::FromUTF8(
                std::string("SPO (") + bp::ui::MICROSEC + ")")),
            0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        tx_spo_ = new wxTextCtrl(page, wxID_ANY);
        tx_spo_->SetToolTip(wxString::FromUTF8(
            (std::string("System Phase Offset (") + bp::ui::MICROSEC + "): fine phase alignment.").c_str()));
        btn_spo_calc_ = new wxButton(page, wxID_ANY, "Estimate",
                                     wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        btn_spo_calc_->SetToolTip("Estimate SPO from geometry (free-space, initial planning value).");
        row->Add(tx_spo_,       1, wxEXPAND | wxRIGHT, 4);
        row->Add(btn_spo_calc_, 0, wxALIGN_CENTER_VERTICAL);
        slot_gs->Add(row, 1, wxEXPAND | wxBOTTOM, 4);
    }

    tx_delay_ = MakeField(page, (std::string("Station delay (") + bp::ui::MICROSEC + ")").c_str(), slot_gs);
    tx_delay_->SetToolTip("Propagation delay of the reference signal from the master transmitter to this station.");

    slot_sizer->Add(slot_gs, 0, wxEXPAND | wxALL, 4);
    outer_sizer->Add(slot_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // ── Delete site button ─────────────────────────────────────────────────
    tx_delete_ = new wxButton(page, wxID_ANY, "Delete Site");
    tx_delete_->Enable(false);
    outer_sizer->Add(tx_delete_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    page->SetSizer(outer_sizer);

    // ── Bind events ────────────────────────────────────────────────────────
    for (auto* tc : {tx_name_, tx_lat_, tx_lon_, tx_power_, tx_height_}) {
        tc->Bind(wxEVT_TEXT, &ParamEditor::OnSiteField, this);
    }
    tx_locked_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        if (updating_ || current_site_id_ < 0 || !on_site_lock_changed) return;
        on_site_lock_changed(current_site_id_, tx_locked_->GetValue());
    });

    tx_slot_list_->Bind(wxEVT_LISTBOX, &ParamEditor::OnSlotSelected, this);
    btn_add_slot_->Bind(wxEVT_BUTTON,   &ParamEditor::OnAddSlot,    this);
    btn_remove_slot_->Bind(wxEVT_BUTTON, &ParamEditor::OnRemoveSlot, this);

    for (auto* tc : {tx_spo_, tx_delay_}) {
        tc->Bind(wxEVT_TEXT, &ParamEditor::OnSlotField, this);
    }
    tx_slot_num_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&){
        wxCommandEvent e; OnSlotField(e); });
    tx_master_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&){
        UpdateMasterSlotState();
        UpdateSpoCalcState();
        wxCommandEvent e; OnSlotField(e); });
    tx_mslot_choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&){
        UpdateSpoCalcState();
        wxCommandEvent e; OnSlotField(e); });
    btn_spo_calc_->Bind(wxEVT_BUTTON, &ParamEditor::OnCalcSPO, this);

    tx_delete_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (current_site_id_ >= 0 && on_site_deleted)
            on_site_deleted(current_site_id_);
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
        "Advanced: combines atmospheric and vehicle noise floors in a power sum.");
    gs->Add(rx_mode_, 1, wxEXPAND | wxBOTTOM, 4);
    rx_mode_->Bind(wxEVT_CHOICE, &ParamEditor::OnRxMode, this);

    rx_noise_   = MakeField(page, (std::string("Noise floor (") + bp::ui::DBUVM + ")").c_str(), gs);
    rx_vnoise_  = MakeField(page, (std::string("Vehicle noise (") + bp::ui::DBUVM + ")").c_str(), gs);
    rx_range_   = MakeField(page, "Max range (km)", gs);

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

// ---------------------------------------------------------------------------
// Load / Clear
// ---------------------------------------------------------------------------

void ParamEditor::LoadSite(int id, const TransmitterSite& site) {
    // Keep updating_ true for the entire method to suppress spurious
    // wxEVT_CHOICE / wxEVT_LISTBOX events fired by Clear()/Append()/
    // SetSelection() calls.  Without this, RebuildMasterSlotChoices →
    // Clear() fires wxEVT_CHOICE → OnSlotField → on_site_changed →
    // SetSiteList → RebuildMasterSlotChoices (re-entrant), corrupting
    // state and crashing.
    updating_ = true;
    current_site_id_  = id;
    current_site_     = site;
    current_slot_idx_ = site.slots.empty() ? -1 : 0;

    tx_name_->ChangeValue(site.name);
    tx_lat_->ChangeValue(wxString::Format("%.6f", site.lat));
    tx_lon_->ChangeValue(wxString::Format("%.6f", site.lon));
    tx_power_->ChangeValue(wxString::Format("%.1f", site.power_w));
    tx_height_->ChangeValue(wxString::Format("%.1f", site.height_m));
    tx_locked_->SetValue(site.locked);
    tx_delete_->Enable(true);

    notebook_->SetSelection(0);

    RebuildMasterSlotChoices();
    UpdateSlotListBox();

    if (current_slot_idx_ >= 0) {
        tx_slot_list_->SetSelection(0);
        LoadSlotFields(0);
    } else {
        // No slots — clear slot fields
        tx_slot_num_->SetValue(1);
        tx_master_->SetValue(false);
        tx_mslot_choice_->SetSelection(0);
        tx_spo_->ChangeValue("0.000");
        tx_delay_->ChangeValue("0.000");
    }
    btn_remove_slot_->Enable(!site.slots.empty());
    updating_ = false;
}

void ParamEditor::LoadReceiver(const ReceiverModel& rx) {
    current_rx_ = rx;
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
    current_site_id_  = -1;
    current_slot_idx_ = -1;
    current_site_ = TransmitterSite{};
    tx_name_->ChangeValue("");
    tx_lat_->ChangeValue("");
    tx_lon_->ChangeValue("");
    tx_slot_list_->Clear();
    tx_delete_->Enable(false);
    updating_ = false;
}

// ---------------------------------------------------------------------------
// SetSiteList — rebuild master-slot dropdown whenever the scenario list changes
// ---------------------------------------------------------------------------

void ParamEditor::SetSiteList(const std::vector<TransmitterSite>& sites) {
    site_list_ = sites;
    if (current_site_id_ < 0) return;
    bool was_updating = updating_;
    updating_ = true;
    int prev_sel = 0;
    int cur = tx_mslot_choice_->GetSelection();
    if (cur >= 0 && cur < (int)master_slot_values_.size())
        prev_sel = master_slot_values_[cur];
    RebuildMasterSlotChoices();
    int new_sel = 0;
    for (int i = 0; i < (int)master_slot_values_.size(); ++i)
        if (master_slot_values_[i] == prev_sel) { new_sel = i; break; }
    tx_mslot_choice_->SetSelection(new_sel);
    updating_ = was_updating;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void ParamEditor::UpdateSlotListBox() {
    bool was_updating = updating_;
    updating_ = true;
    tx_slot_list_->Clear();
    for (const auto& sc : current_site_.slots) {
        wxString label = wxString::Format("Slot %d", sc.slot);
        if (sc.is_master) label += "  (Master)";
        else if (sc.master_slot > 0)
            label += wxString::Format("  \u2192 Slot %d", sc.master_slot);
        tx_slot_list_->Append(label);
    }
    updating_ = was_updating;
}

void ParamEditor::LoadSlotFields(int slot_idx) {
    if (slot_idx < 0 || slot_idx >= (int)current_site_.slots.size()) return;
    bool was_updating = updating_;
    updating_ = true;
    current_slot_idx_ = slot_idx;
    const auto& sc = current_site_.slots[slot_idx];
    tx_slot_num_->SetValue(sc.slot);
    tx_master_->SetValue(sc.is_master);
    tx_spo_->ChangeValue(wxString::Format("%.3f", sc.spo_us));
    tx_delay_->ChangeValue(wxString::Format("%.3f", sc.station_delay_us));

    // Select master-slot entry
    int sel = 0;
    for (int i = 0; i < (int)master_slot_values_.size(); ++i)
        if (master_slot_values_[i] == sc.master_slot) { sel = i; break; }
    tx_mslot_choice_->SetSelection(sel);

    updating_ = was_updating;
    UpdateMasterSlotState();
    UpdateSpoCalcState();
}

void ParamEditor::SaveCurrentSlotFields() {
    if (current_slot_idx_ < 0 ||
        current_slot_idx_ >= (int)current_site_.slots.size()) return;
    auto& sc = current_site_.slots[current_slot_idx_];
    sc.slot      = tx_slot_num_->GetValue();
    sc.is_master = tx_master_->GetValue();
    int sel = tx_mslot_choice_->GetSelection();
    sc.master_slot = (sel >= 0 && sel < (int)master_slot_values_.size())
                     ? master_slot_values_[sel] : 0;
    sc.spo_us           = wxAtof(tx_spo_->GetValue());
    sc.station_delay_us = wxAtof(tx_delay_->GetValue());
}

void ParamEditor::RebuildMasterSlotChoices() {
    bool was_updating = updating_;
    updating_ = true;
    tx_mslot_choice_->Clear();
    master_slot_values_.clear();
    tx_mslot_choice_->Append("None (is master)");
    master_slot_values_.push_back(0);

    // All slots from all sites except the current site (and the current slot itself)
    for (int si = 0; si < (int)site_list_.size(); ++si) {
        if (si == current_site_id_) continue;
        const auto& s = site_list_[si];
        for (const auto& sc : s.slots) {
            wxString label = wxString::Format("Slot %d - %s",
                                              sc.slot, wxString::FromUTF8(s.name).c_str());
            tx_mslot_choice_->Append(label);
            master_slot_values_.push_back(sc.slot);
        }
    }
    // Also include OTHER slots within the same site (so colocated slots can
    // reference each other as master/slave)
    if (current_site_id_ >= 0 && current_site_id_ < (int)site_list_.size()) {
        const auto& cur = site_list_[current_site_id_];
        for (int j = 0; j < (int)cur.slots.size(); ++j) {
            if (j == current_slot_idx_) continue;
            const auto& sc = cur.slots[j];
            wxString label = wxString::Format("Slot %d - %s (same site)",
                                              sc.slot, wxString::FromUTF8(cur.name).c_str());
            tx_mslot_choice_->Append(label);
            master_slot_values_.push_back(sc.slot);
        }
    }
    updating_ = was_updating;
}

void ParamEditor::UpdateMasterSlotState() {
    tx_mslot_choice_->Enable(!tx_master_->GetValue());
}

void ParamEditor::UpdateSpoCalcState() {
    bool is_master = tx_master_->GetValue();
    int sel = tx_mslot_choice_->GetSelection();
    bool has_master = !is_master && sel > 0 && sel < (int)master_slot_values_.size();
    btn_spo_calc_->Enable(has_master);
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void ParamEditor::OnSiteField(wxCommandEvent& /*evt*/) {
    if (updating_ || current_site_id_ < 0 || !on_site_changed) return;
    current_site_.name    = tx_name_->GetValue().ToStdString();
    current_site_.lat     = wxAtof(tx_lat_->GetValue());
    current_site_.lon     = wxAtof(tx_lon_->GetValue());
    current_site_.power_w = wxAtof(tx_power_->GetValue());
    current_site_.height_m = wxAtof(tx_height_->GetValue());
    on_site_changed(current_site_id_, current_site_);
}

void ParamEditor::OnSlotSelected(wxCommandEvent& /*evt*/) {
    if (updating_) return;
    int sel = tx_slot_list_->GetSelection();
    if (sel == wxNOT_FOUND || sel < 0 ||
        sel >= (int)current_site_.slots.size()) return;
    // Save edits to the previously selected slot first
    SaveCurrentSlotFields();
    LoadSlotFields(sel);
}

void ParamEditor::OnSlotField(wxCommandEvent& /*evt*/) {
    if (updating_ || current_site_id_ < 0 || current_slot_idx_ < 0) return;
    SaveCurrentSlotFields();
    // Rebuild list box label for the current slot (slot number or master may have changed)
    UpdateSlotListBox();
    updating_ = true;
    tx_slot_list_->SetSelection(current_slot_idx_);
    updating_ = false;
    if (on_site_changed)
        on_site_changed(current_site_id_, current_site_);
}

void ParamEditor::OnAddSlot(wxCommandEvent& /*evt*/) {
    if (current_site_id_ < 0) return;
    // Save current slot edits first
    SaveCurrentSlotFields();
    // Pick a slot number not yet used in the site
    int next_slot = 1;
    for (const auto& sc : current_site_.slots)
        if (sc.slot >= next_slot) next_slot = sc.slot + 1;
    SlotConfig sc;
    sc.slot = next_slot;
    current_site_.slots.push_back(sc);
    current_slot_idx_ = (int)current_site_.slots.size() - 1;
    RebuildMasterSlotChoices();
    UpdateSlotListBox();
    updating_ = true;
    tx_slot_list_->SetSelection(current_slot_idx_);
    updating_ = false;
    LoadSlotFields(current_slot_idx_);
    btn_remove_slot_->Enable(true);
    if (on_site_changed)
        on_site_changed(current_site_id_, current_site_);
}

void ParamEditor::OnRemoveSlot(wxCommandEvent& /*evt*/) {
    if (current_site_id_ < 0 || current_slot_idx_ < 0 ||
        current_site_.slots.empty()) return;
    if (current_site_.slots.size() == 1) {
        // Don't allow removing the last slot (site must have at least one)
        wxMessageBox("A site must have at least one slot.\n"
                     "Delete the site instead if it is no longer needed.",
                     "Remove Slot", wxOK | wxICON_INFORMATION);
        return;
    }
    current_site_.slots.erase(current_site_.slots.begin() + current_slot_idx_);
    current_slot_idx_ = std::min(current_slot_idx_, (int)current_site_.slots.size() - 1);
    RebuildMasterSlotChoices();
    UpdateSlotListBox();
    if (current_slot_idx_ >= 0) {
        updating_ = true;
        tx_slot_list_->SetSelection(current_slot_idx_);
        updating_ = false;
        LoadSlotFields(current_slot_idx_);
    }
    btn_remove_slot_->Enable(!current_site_.slots.empty());
    if (on_site_changed)
        on_site_changed(current_site_id_, current_site_);
}

void ParamEditor::OnCalcSPO(wxCommandEvent& /*evt*/) {
    // Find the master transmitter in site_list_ by the chosen slot number.
    int sel = tx_mslot_choice_->GetSelection();
    if (sel <= 0 || sel >= (int)master_slot_values_.size()) return;
    int master_slot = master_slot_values_[sel];

    // Find master site/position
    double master_lat = 0.0, master_lon = 0.0;
    bool found = false;
    for (const auto& s : site_list_) {
        for (const auto& sc : s.slots) {
            if (sc.slot == master_slot) {
                master_lat = s.lat; master_lon = s.lon;
                found = true; break;
            }
        }
        if (found) break;
    }
    if (!found) return;

    double slave_lat = wxAtof(tx_lat_->GetValue());
    double slave_lon = wxAtof(tx_lon_->GetValue());

    const GeographicLib::Geodesic& geod = GeographicLib::Geodesic::WGS84();
    double dist_m = 0.0;
    geod.Inverse(slave_lat, slave_lon, master_lat, master_lon, dist_m);
    dist_m = std::abs(dist_m);

    constexpr double c = 299'792'458.0;
    double station_delay_s = wxAtof(tx_delay_->GetValue()) * 1e-6;
    double total_delay_s   = dist_m / c + station_delay_s;

    double phase_lanes = std::fmod(total_delay_s * f1_hz_, 1.0);
    if (phase_lanes > 0.5) phase_lanes -= 1.0;
    double spo_us = -phase_lanes / f1_hz_ * 1e6;

    updating_ = true;
    tx_spo_->ChangeValue(wxString::Format("%.3f", spo_us));
    updating_ = false;
    wxCommandEvent e; OnSlotField(e);
}

// ---------------------------------------------------------------------------
// Receiver events
// ---------------------------------------------------------------------------

void ParamEditor::OnRxField(wxCommandEvent& /*evt*/) {
    if (updating_ || !on_receiver_changed) return;
    wxCommandEvent e; OnRxMode(e);
}

void ParamEditor::OnRxMode(wxCommandEvent& /*evt*/) {
    UpdateRxFieldStates();
    if (updating_ || !on_receiver_changed) return;
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

void ParamEditor::UpdateRxFieldStates() {
    bool advanced = (rx_mode_->GetSelection() == 1);
    rx_vnoise_->Enable(advanced);
}

} // namespace bp
