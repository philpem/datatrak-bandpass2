#pragma once
#include <functional>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/notebook.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include "../model/Transmitter.h"
#include "../model/ReceiverModel.h"

namespace bp {

class ParamEditor : public wxPanel {
public:
    explicit ParamEditor(wxWindow* parent);

    void LoadTransmitter(int id, const Transmitter& tx);
    void LoadReceiver(const ReceiverModel& rx);
    void ClearSelection();

    std::function<void(int id, const Transmitter&)> on_transmitter_changed;
    std::function<void(const ReceiverModel&)>        on_receiver_changed;

private:
    void BuildTransmitterPage(wxWindow* page);
    void BuildReceiverPage(wxWindow* page);
    void OnTxField(wxCommandEvent& evt);
    void OnRxField(wxCommandEvent& evt);
    void OnRxMode(wxCommandEvent& evt);

    wxNotebook* notebook_ = nullptr;

    // Transmitter page
    wxTextCtrl* tx_name_    = nullptr;
    wxTextCtrl* tx_lat_     = nullptr;
    wxTextCtrl* tx_lon_     = nullptr;
    wxTextCtrl* tx_power_   = nullptr;
    wxTextCtrl* tx_height_  = nullptr;
    wxSpinCtrl* tx_slot_    = nullptr;
    wxCheckBox* tx_master_  = nullptr;
    wxSpinCtrl* tx_mslot_   = nullptr;
    wxTextCtrl* tx_spo_     = nullptr;
    wxTextCtrl* tx_delay_   = nullptr;

    // Receiver page
    wxChoice*   rx_mode_    = nullptr;
    wxTextCtrl* rx_noise_   = nullptr;
    wxTextCtrl* rx_vnoise_  = nullptr;
    wxTextCtrl* rx_range_   = nullptr;
    wxSpinCtrl* rx_minstns_ = nullptr;

    int  current_tx_id_  = -1;
    bool updating_       = false;
};

} // namespace bp
