#pragma once
#include <memory>
#include <string>
#include <wx/frame.h>
#include <wx/aui/aui.h>
#include "../model/Scenario.h"
#include "../engine/compute_manager.h"
#include "../engine/grid.h"
#include "../almanac/AlmanacExport.h"
#include "MapPanel.h"
#include "NetworkConfigPanel.h"
#include "ParamEditor.h"
#include "LayerPanel.h"
#include "ReceiverPanel.h"
#include "ResultsPanel.h"
#include "TileCache.h"

namespace bp {

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame();

private:
    // Menu/toolbar handlers
    void OnFileNew(wxCommandEvent& evt);
    void OnFileOpen(wxCommandEvent& evt);
    void OnFileSave(wxCommandEvent& evt);
    void OnFileSaveAs(wxCommandEvent& evt);
    void OnViewNetworkConfig(wxCommandEvent& evt);
    void OnViewLayerPanel(wxCommandEvent& evt);
    void OnViewParamEditor(wxCommandEvent& evt);
    void OnToolPlaceTx(wxCommandEvent& evt);
    void OnToolPlaceRx(wxCommandEvent& evt);
    void OnToolCompute(wxCommandEvent& evt);
    void OnHelpAbout(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    // Compute events
    void OnComputeResult(wxCommandEvent& evt);
    void OnComputeProgress(wxCommandEvent& evt);

    void OnEditDeleteTx(wxCommandEvent& evt);

    // Map / scenario callbacks
    void OnMapClick(double lat, double lon);
    void OnTransmitterMoved(int id, double lat, double lon);
    void OnTransmitterSelected(int id);
    void DeleteSite(int id);
    void OnReceiverPlaced(double lat, double lon);
    void OnCursorMoved(double lat, double lon);
    void OnReceiverMoved(double lat, double lon);
    void OnExportSimulator();
    void OnExportAlmanac(almanac::FirmwareFormat fmt);
    void OnExportLayers(const std::string& format);
    void OnImportMonitorLog(wxCommandEvent& evt);
    void OnComputePatternOffsets(wxCommandEvent& evt);
    void ClearMapTransmitters(int count);
    void SyncMapTransmitters();
    void SyncGridBounds();
    void OnGridBoundsChanged(double lat_min, double lat_max, double lon_min, double lon_max);
    void TriggerRecompute();
    void MarkDirty();
    bool ConfirmDiscardChanges();

    void BuildMenus();
    void BuildToolbar();
    void BuildStatusBar();
    void UpdateStatusBarMl();
    void UpdateTitle();
    void ApplyComputeResult(const ComputeResult& result);
    void PushLayerToMap(const std::string& name);

    // UI components
    wxAuiManager       aui_;
    MapPanel*          map_panel_      = nullptr;
    NetworkConfigPanel* net_config_    = nullptr;
    ParamEditor*       param_editor_   = nullptr;
    LayerPanel*        layer_panel_    = nullptr;
    ReceiverPanel*     receiver_panel_ = nullptr;
    ResultsPanel*      results_panel_  = nullptr;

    // State
    Scenario           scenario_;
    std::string        current_file_;
    bool               dirty_           = false;
    bool               placement_mode_     = false;
    bool               rx_placement_mode_  = false;
    bool               rx_locked_          = false;
    bool               compute_enabled_    = true;
    int                next_tx_id_         = 1;
    int                selected_site_id_   = -1;
    std::shared_ptr<const GridData> last_grid_data_;
    std::string        current_map_layer_;  // exact key currently shown on map
    double             rx_lat_          = 0.0;
    double             rx_lon_          = 0.0;
    bool               rx_placed_       = false;

    // Backend
    std::unique_ptr<TileCache>      tile_cache_;
    std::unique_ptr<ComputeManager> compute_mgr_;
};

} // namespace bp
