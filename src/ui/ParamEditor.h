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
#include "../model/Transmitter.h"
#include "../model/ReceiverModel.h"

namespace bp {

class ParamEditor : public wxPanel {
public:
    explicit ParamEditor(wxWindow* parent);

    void LoadTransmitter(int id, const Transmitter& tx);
    void LoadReceiver(const ReceiverModel& rx);
    void ClearSelection();

    // Supply the full transmitter list so the master-slot dropdown can be
    // populated with names and slot numbers.  Call whenever the list changes.
    void SetTransmitterList(const std::vector<Transmitter>& txs);

    // Keep the configured F1 frequency up to date so the SPO estimate button
    // uses the correct carrier.  Defaults to Datatrak standard 146.4375 kHz.
    void SetFrequency(double f1_hz) { f1_hz_ = f1_hz; }

    std::function<void(int id, const Transmitter&)> on_transmitter_changed;
    std::function<void(const ReceiverModel&)>        on_receiver_changed;
    std::function<void(int id, bool locked)>         on_tx_lock_changed;
    std::function<void(bool locked)>                 on_rx_lock_changed;
    std::function<void(int id)>                      on_transmitter_deleted;

private:
    void BuildTransmitterPage(wxWindow* page);
    void BuildReceiverPage(wxWindow* page);
    void OnTxField(wxCommandEvent& evt);
    void OnRxField(wxCommandEvent& evt);
    void OnRxMode(wxCommandEvent& evt);
    void UpdateRxFieldStates();
    void RebuildMasterSlotChoices(int current_id);
    void UpdateMasterSlotState();
    void UpdateSpoCalcState();
    void OnCalcSPO(wxCommandEvent& evt);

    wxNotebook* notebook_ = nullptr;

    // Transmitter page
    wxTextCtrl* tx_name_         = nullptr;
    wxTextCtrl* tx_lat_          = nullptr;
    wxTextCtrl* tx_lon_          = nullptr;
    wxTextCtrl* tx_power_        = nullptr;
    wxTextCtrl* tx_height_       = nullptr;
    wxSpinCtrl* tx_slot_         = nullptr;
    wxCheckBox* tx_master_       = nullptr;
    wxChoice*   tx_mslot_choice_ = nullptr;  // replaces spinctrl
    wxTextCtrl* tx_spo_          = nullptr;
    wxButton*   btn_spo_calc_    = nullptr;
    wxTextCtrl* tx_delay_        = nullptr;
    wxCheckBox* tx_locked_       = nullptr;
    wxButton*   tx_delete_       = nullptr;

    // Master-slot dropdown state
    std::vector<int>         master_slot_values_;  // slot number per choice index
    std::vector<Transmitter> tx_list_;             // full list for rebuilding choices

    // Receiver page
    wxChoice*   rx_mode_    = nullptr;
    wxTextCtrl* rx_noise_   = nullptr;
    wxTextCtrl* rx_vnoise_  = nullptr;
    wxTextCtrl* rx_range_   = nullptr;
    wxSpinCtrl* rx_minstns_ = nullptr;
    wxCheckBox* rx_locked_  = nullptr;

    int    current_tx_id_  = -1;
    bool   updating_       = false;
    double f1_hz_          = 146437.5;  // kept in sync via SetFrequency(); used by OnCalcSPO
    ReceiverModel current_rx_;   // preserves fields not exposed in the form (vp_ms, ellipsoid)
};

} // namespace bp
