#pragma once
#include <functional>
#include <vector>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/statbox.h>
#include "../model/Transmitter.h"
#include "../model/ReceiverModel.h"

namespace bp {

class ParamEditor : public wxPanel {
public:
    explicit ParamEditor(wxWindow* parent);

    // Load a transmitter site into the editor and show the Transmitter tab.
    void LoadSite(int id, const TransmitterSite& site);
    void LoadReceiver(const ReceiverModel& rx);
    void ClearSelection();

    // Supply the full site list so the master-slot dropdown can be populated.
    // Call whenever the list changes.
    void SetSiteList(const std::vector<TransmitterSite>& sites);

    // Keep the configured F1 frequency up to date so the SPO estimate button
    // uses the correct carrier.  Defaults to Datatrak standard 146.4375 kHz.
    void SetFrequency(double f1_hz) { f1_hz_ = f1_hz; }

    // Callbacks wired by MainFrame
    std::function<void(int site_id, const TransmitterSite&)> on_site_changed;
    std::function<void(int site_id, bool locked)>            on_site_lock_changed;
    std::function<void(int site_id)>                         on_site_deleted;
    std::function<void(const ReceiverModel&)>                on_receiver_changed;
    std::function<void(bool locked)>                         on_rx_lock_changed;

private:
    void BuildTransmitterPage(wxWindow* page);
    void BuildReceiverPage(wxWindow* page);

    // Site-level field change
    void OnSiteField(wxCommandEvent& evt);
    // Slot list selection change
    void OnSlotSelected(wxCommandEvent& evt);
    // Per-slot field change
    void OnSlotField(wxCommandEvent& evt);
    void OnAddSlot(wxCommandEvent& evt);
    void OnRemoveSlot(wxCommandEvent& evt);
    void OnCalcSPO(wxCommandEvent& evt);

    void OnRxField(wxCommandEvent& evt);
    void OnRxMode(wxCommandEvent& evt);
    void UpdateRxFieldStates();
    void RebuildMasterSlotChoices();
    void UpdateMasterSlotState();
    void UpdateSpoCalcState();
    void UpdateSlotListBox();
    void UpdateSlotNumWarning();  // red background on tx_slot_num_ if duplicate
    void LoadSlotFields(int slot_idx);
    void SaveCurrentSlotFields();

    wxNotebook* notebook_ = nullptr;

    // ── Transmitter tab ──────────────────────────────────────────────────────
    // Site properties
    wxTextCtrl* tx_name_   = nullptr;
    wxTextCtrl* tx_lat_    = nullptr;
    wxTextCtrl* tx_lon_    = nullptr;
    wxTextCtrl* tx_power_  = nullptr;
    wxTextCtrl* tx_height_ = nullptr;
    wxCheckBox* tx_locked_ = nullptr;

    // Slot list
    wxListBox*  tx_slot_list_ = nullptr;
    wxButton*   btn_add_slot_    = nullptr;
    wxButton*   btn_remove_slot_ = nullptr;

    // Per-slot fields
    wxSpinCtrl* tx_slot_num_     = nullptr;
    wxCheckBox* tx_master_       = nullptr;
    wxChoice*   tx_mslot_choice_ = nullptr;
    wxTextCtrl* tx_spo_          = nullptr;
    wxButton*   btn_spo_calc_    = nullptr;
    wxTextCtrl* tx_delay_        = nullptr;

    // Delete site button
    wxButton*   tx_delete_ = nullptr;

    // Master-slot dropdown state
    std::vector<int>             master_slot_values_;
    std::vector<TransmitterSite> site_list_;   // full list for rebuilding choices

    // Current editing state
    int              current_site_id_  = -1;
    int              current_slot_idx_ = -1;   // index into site.slots
    TransmitterSite  current_site_;            // working copy
    bool             updating_          = false;
    double           f1_hz_             = 146437.5;

    // ── Receiver tab ────────────────────────────────────────────────────────
    wxChoice*   rx_mode_    = nullptr;
    wxTextCtrl* rx_noise_   = nullptr;
    wxTextCtrl* rx_vnoise_  = nullptr;
    wxTextCtrl* rx_range_   = nullptr;
    wxSpinCtrl* rx_minstns_ = nullptr;
    wxCheckBox* rx_locked_  = nullptr;
    ReceiverModel current_rx_;
};

} // namespace bp
