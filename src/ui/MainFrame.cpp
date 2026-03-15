#include "MainFrame.h"
#include "ExportManager.h"
#include "UiConstants.h"
#include "toolbar_icons.h"
#include "version_generated.h"
#include "../coords/NationalGrid.h"
#include "../coords/Osgb.h"
#include "../model/toml_io.h"
#include "../engine/asf.h"
#include "../almanac/AlmanacExport.h"
#include "../almanac/MonitorCalib.h"
#include "PoRefDialog.h"
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/choicdlg.h>
#include <wx/aboutdlg.h>
#include <wx/toolbar.h>
#include <wx/menu.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/artprov.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/file.h>
#include <cstdlib>
#include <stdexcept>

namespace bp {

enum {
    ID_FILE_NEW      = wxID_NEW,
    ID_FILE_OPEN     = wxID_OPEN,
    ID_FILE_SAVE     = wxID_SAVE,
    ID_FILE_SAVEAS   = wxID_SAVEAS,
    ID_VIEW_NETCFG   = wxID_HIGHEST + 100,
    ID_VIEW_LAYERS,
    ID_VIEW_PARAMS,
    ID_TOOL_PLACE_TX,
    ID_TOOL_PLACE_RX,
    ID_TOOL_COMPUTE,
    ID_EDIT_DELETE_TX,
    ID_EXPORT_ALMANAC_V7,
    ID_EXPORT_ALMANAC_V16,
    ID_EXPORT_LAYERS_CSV,
    ID_EXPORT_LAYERS_PNG,
    ID_EXPORT_LAYERS_GEOTIFF,
    ID_EXPORT_LAYERS_HTML,
    ID_IMPORT_MONITOR_LOG,
    ID_COMPUTE_PO,
    SB_WGS84   = 0,
    SB_OSGB    = 1,
    SB_ML      = 2,
    SB_STATUS  = 3,
};

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "BANDPASS II",
              wxDefaultPosition, wxSize(1200, 800))
{
    aui_.SetManagedWindow(this);

    // Initialize tile cache (store in user data dir)
    try {
        wxString data_dir = wxStandardPaths::Get().GetUserDataDir();
        wxFileName db_path(data_dir, "tiles.db");
        tile_cache_ = std::make_unique<TileCache>(
            std::filesystem::path(db_path.GetFullPath().ToStdString()));
    } catch (std::exception& e) {
        wxLogWarning("Tile cache init failed: %s", e.what());
    }

    uint16_t port = tile_cache_ ? tile_cache_->GetPort() : 0;

    // Map panel (centre)
    map_panel_ = new MapPanel(this, port);
    map_panel_->on_map_click         = [this](double lat, double lon){ OnMapClick(lat, lon); };
    map_panel_->on_transmitter_moved = [this](int id, double lat, double lon){
        OnTransmitterMoved(id, lat, lon);
    };
    map_panel_->on_cursor_moved          = [this](double lat, double lon){ OnCursorMoved(lat, lon); };
    map_panel_->on_receiver_moved        = [this](double lat, double lon){ OnReceiverMoved(lat, lon); };
    map_panel_->on_transmitter_selected  = [this](int id){ OnTransmitterSelected(id); };
    map_panel_->on_receiver_placed       = [this](double lat, double lon){ OnReceiverPlaced(lat, lon); };
    map_panel_->on_grid_bounds_changed   = [this](double lat_min, double lat_max,
                                                   double lon_min, double lon_max){
        OnGridBoundsChanged(lat_min, lat_max, lon_min, lon_max);
    };

    // Dockable panels
    net_config_    = new NetworkConfigPanel(this);
    param_editor_  = new ParamEditor(this);
    layer_panel_   = new LayerPanel(this);
    receiver_panel_ = new ReceiverPanel(this);
    receiver_panel_->on_export_simulator = [this](){ OnExportSimulator(); };
    results_panel_ = new ResultsPanel(this);

    net_config_->SetScenario(&scenario_);
    net_config_->on_changed = [this](const Scenario&){
        param_editor_->SetFrequency(scenario_.frequencies.f1_hz);
        SyncGridBounds();
        MarkDirty();
        TriggerRecompute();
    };
    results_panel_->SetScenario(&scenario_);

    param_editor_->on_site_changed = [this](int id, const TransmitterSite& site) {
        if (id >= 0 && id < (int)scenario_.transmitter_sites.size()) {
            scenario_.transmitter_sites[id] = site;
            // Refresh master-slot dropdown in case name or slot number changed
            param_editor_->SetSiteList(scenario_.transmitter_sites);
            // Update map marker icon in case slot count changed
            map_panel_->AddTransmitterMarker(id, site.lat, site.lon, site.name,
                                             site.locked, site.slot_count());
            MarkDirty();
            TriggerRecompute();
        }
    };
    param_editor_->on_receiver_changed = [this](const ReceiverModel& rx) {
        scenario_.receiver = rx;
        MarkDirty();
        TriggerRecompute();
    };
    param_editor_->on_site_lock_changed = [this](int id, bool locked) {
        if (id >= 0 && id < (int)scenario_.transmitter_sites.size()) {
            scenario_.transmitter_sites[id].locked = locked;
            map_panel_->LockTransmitter(id, locked);
            MarkDirty();
        }
    };
    param_editor_->on_rx_lock_changed = [this](bool locked) {
        rx_locked_ = locked;
        map_panel_->LockReceiver(locked);
    };
    param_editor_->on_site_deleted = [this](int id) {
        DeleteSite(id);
    };

    layer_panel_->on_select = [this](const std::string& layer) {
        // Clear whichever layer is currently displayed (including _log variants)
        if (!current_map_layer_.empty()) {
            map_panel_->ClearLayer(current_map_layer_);
            current_map_layer_.clear();
        }
        map_panel_->ClearLegend();
        if (!layer.empty()) PushLayerToMap(layer);
    };
    layer_panel_->on_opacity_changed = [this](float opacity) {
        map_panel_->SetLayerOpacity(opacity);
    };

    // AUI layout
    aui_.AddPane(map_panel_, wxAuiPaneInfo().CenterPane().Name("map"));
    aui_.AddPane(net_config_, wxAuiPaneInfo()
        .Right().Layer(1).Position(0).Name("netcfg")
        .Caption("Network Configuration").CloseButton(true).PinButton(true)
        .BestSize(260, 220).MinSize(200, 180));
    aui_.AddPane(param_editor_, wxAuiPaneInfo()
        .Right().Layer(1).Position(1).Name("params")
        .Caption("Parameters").CloseButton(true).PinButton(true)
        .BestSize(260, 300).MinSize(200, 200));
    aui_.AddPane(layer_panel_, wxAuiPaneInfo()
        .Right().Layer(1).Position(2).Name("layers")
        .Caption("Layers").CloseButton(true).PinButton(true)
        .BestSize(220, 250).MinSize(180, 200));
    aui_.AddPane(receiver_panel_, wxAuiPaneInfo()
        .Bottom().Layer(0).Position(0).Name("receiver")
        .Caption("Receiver Phase Table").CloseButton(true)
        .BestSize(600, 160).MinSize(400, 120));
    aui_.AddPane(results_panel_, wxAuiPaneInfo()
        .Bottom().Layer(0).Position(1).Name("results")
        .Caption("Field Strength Plots").CloseButton(true)
        .BestSize(400, 160).MinSize(300, 120));
    aui_.Update();

    BuildMenus();
    BuildToolbar();
    BuildStatusBar();

    // Compute manager
    compute_mgr_ = std::make_unique<ComputeManager>(this);
    Bind(EVT_COMPUTE_RESULT,   &MainFrame::OnComputeResult,   this);
    Bind(EVT_COMPUTE_PROGRESS, &MainFrame::OnComputeProgress, this);

    // Populate parameter editor with defaults
    param_editor_->SetFrequency(scenario_.frequencies.f1_hz);
    param_editor_->SetSiteList(scenario_.transmitter_sites);
    param_editor_->LoadReceiver(scenario_.receiver);

    // Initial recompute
    scenario_.frequencies.recompute();
    UpdateStatusBarMl();
    UpdateTitle();
    SyncGridBounds();
    TriggerRecompute();

    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
}

MainFrame::~MainFrame() {
    if (compute_mgr_) compute_mgr_->Shutdown();
    aui_.UnInit();
}

void MainFrame::BuildMenus() {
    auto* mb = new wxMenuBar;

    auto* file = new wxMenu;
    file->Append(ID_FILE_NEW,    "&New\tCtrl+N");
    file->Append(ID_FILE_OPEN,   "&Open...\tCtrl+O");
    file->Append(ID_FILE_SAVE,   "&Save\tCtrl+S");
    file->Append(ID_FILE_SAVEAS, "Save &As...");
    file->AppendSeparator();
    auto* exportMenu = new wxMenu;
    exportMenu->Append(ID_EXPORT_ALMANAC_V7,  "Almanac Commands (V7)");
    exportMenu->Append(ID_EXPORT_ALMANAC_V16, "Almanac Commands (V16)");
    exportMenu->AppendSeparator();
    exportMenu->Append(ID_EXPORT_LAYERS_CSV,    "Active Layer as CSV...");
    exportMenu->Append(ID_EXPORT_LAYERS_PNG,    "Active Layer as PNG...");
    exportMenu->Append(ID_EXPORT_LAYERS_GEOTIFF, "Active Layer as GeoTIFF...");
    exportMenu->Append(ID_EXPORT_LAYERS_HTML,   "HTML Report...");
    exportMenu->AppendSeparator();
    exportMenu->Append(ID_COMPUTE_PO, "Compute Pattern Offsets...");
    file->AppendSubMenu(exportMenu, "E&xport");
    auto* importMenu = new wxMenu;
    importMenu->Append(ID_IMPORT_MONITOR_LOG, "Monitor Station Log...");
    file->AppendSubMenu(importMenu, "&Import");
    file->AppendSeparator();
    file->Append(wxID_EXIT,      "E&xit");
    mb->Append(file, "&File");

    auto* edit = new wxMenu;
    edit->Append(ID_EDIT_DELETE_TX, "&Delete Transmitter\tDelete",
                 "Delete the currently selected transmitter");
    mb->Append(edit, "&Edit");

    auto* view = new wxMenu;
    view->Append(ID_VIEW_NETCFG, "&Network Configuration\tCtrl+Shift+N");
    view->Append(ID_VIEW_LAYERS, "&Layer Panel\tCtrl+Shift+L");
    view->Append(ID_VIEW_PARAMS, "&Parameter Editor\tCtrl+Shift+P");
    mb->Append(view, "&View");

    auto* help = new wxMenu;
    help->Append(wxID_ABOUT, "&About BANDPASS II...");
    mb->Append(help, "&Help");

    SetMenuBar(mb);

    Bind(wxEVT_MENU, &MainFrame::OnFileNew,         this, ID_FILE_NEW);
    Bind(wxEVT_MENU, &MainFrame::OnFileOpen,        this, ID_FILE_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnFileSave,        this, ID_FILE_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnFileSaveAs,      this, ID_FILE_SAVEAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ OnExportAlmanac(almanac::FirmwareFormat::V7);  }, ID_EXPORT_ALMANAC_V7);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ OnExportAlmanac(almanac::FirmwareFormat::V16); }, ID_EXPORT_ALMANAC_V16);
    Bind(wxEVT_MENU, &MainFrame::OnEditDeleteTx,      this, ID_EDIT_DELETE_TX);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ OnExportLayers("csv");    }, ID_EXPORT_LAYERS_CSV);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ OnExportLayers("png");    }, ID_EXPORT_LAYERS_PNG);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ OnExportLayers("geotiff"); }, ID_EXPORT_LAYERS_GEOTIFF);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ OnExportLayers("html");   }, ID_EXPORT_LAYERS_HTML);
    Bind(wxEVT_MENU, &MainFrame::OnImportMonitorLog,     this, ID_IMPORT_MONITOR_LOG);
    Bind(wxEVT_MENU, &MainFrame::OnComputePatternOffsets, this, ID_COMPUTE_PO);
    Bind(wxEVT_MENU, &MainFrame::OnViewNetworkConfig, this, ID_VIEW_NETCFG);
    Bind(wxEVT_MENU, &MainFrame::OnViewLayerPanel,    this, ID_VIEW_LAYERS);
    Bind(wxEVT_MENU, &MainFrame::OnViewParamEditor,   this, ID_VIEW_PARAMS);
    Bind(wxEVT_MENU, &MainFrame::OnHelpAbout,         this, wxID_ABOUT);
}

void MainFrame::BuildToolbar() {
    auto* tb = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT);
    tb->AddTool(ID_TOOL_PLACE_TX, "Place TX",
                wxBitmapBundle::FromSVG(tx_mast_svg, wxSize(16, 20)),
                "Click map to place a transmitter", wxITEM_CHECK);
    tb->AddTool(ID_TOOL_PLACE_RX, "Place RX",
                wxBitmapBundle::FromSVG(rx_car_svg, wxSize(28, 17)),
                "Click map to place the receiver", wxITEM_CHECK);
    tb->AddSeparator();
    tb->AddTool(ID_TOOL_COMPUTE, "Auto-compute",
                wxArtProvider::GetBitmap(wxART_EXECUTABLE_FILE, wxART_TOOLBAR),
                "Enable/disable automatic recomputation", wxITEM_CHECK);
    tb->ToggleTool(ID_TOOL_COMPUTE, true);
    tb->Realize();
    Bind(wxEVT_TOOL, &MainFrame::OnToolPlaceTx,  this, ID_TOOL_PLACE_TX);
    Bind(wxEVT_TOOL, &MainFrame::OnToolPlaceRx,  this, ID_TOOL_PLACE_RX);
    Bind(wxEVT_TOOL, &MainFrame::OnToolCompute,  this, ID_TOOL_COMPUTE);
}

void MainFrame::BuildStatusBar() {
    CreateStatusBar(4);
    int widths[] = {220, 180, 260, -1};
    SetStatusWidths(4, widths);
    SetStatusText("Ready", SB_STATUS);
    UpdateStatusBarMl();
}

void MainFrame::UpdateStatusBarMl() {
    double ml_f1 = scenario_.frequencies.lane_width_f1_m / 1000.0;
    double ml_f2 = scenario_.frequencies.lane_width_f2_m / 1000.0;
    SetStatusText(wxString::Format("1 ml(F1)=%.3fm  1 ml(F2)=%.3fm", ml_f1, ml_f2), SB_ML);
}

void MainFrame::UpdateTitle() {
    wxString title = "BANDPASS II";
    if (!current_file_.empty()) {
        wxFileName fn(current_file_);
        title += " - " + fn.GetFullName();
    }
    if (dirty_) title += " *";
    SetTitle(title);
}

void MainFrame::MarkDirty() {
    dirty_ = true;
    UpdateTitle();
}

void MainFrame::TriggerRecompute() {
    results_panel_->Refresh();   // field-strength plot always reflects current scenario
    if (!compute_mgr_ || !compute_enabled_) return;
    compute_mgr_->PostRequest(std::make_shared<const Scenario>(scenario_));
    SetStatusText("Computing...", SB_STATUS);
}

void MainFrame::OnComputeResult(wxCommandEvent& evt) {
    auto* result = static_cast<ComputeResult*>(evt.GetClientData());
    if (!result) return;
    std::unique_ptr<ComputeResult> owned(result);
    if (!owned->error.empty()) {
        SetStatusText("Error: " + owned->error, SB_STATUS);
        return;
    }
    if (owned->data) {
        ApplyComputeResult(*owned);
    }
    SetStatusText("Ready", SB_STATUS);
}

void MainFrame::OnComputeProgress(wxCommandEvent& evt) {
    SetStatusText(wxString::Format("Computing %s... %d%%",
                                   evt.GetString().c_str(), evt.GetInt()), SB_STATUS);
}

void MainFrame::ApplyComputeResult(const ComputeResult& result) {
    if (!result.data) return;
    last_grid_data_ = result.data;

    std::string selected = layer_panel_->GetSelectedLayer();
    if (selected.empty()) return;   // "None" selected — nothing to push
    // Clear the current overlay before replacing it with updated data
    if (!current_map_layer_.empty()) {
        map_panel_->ClearLayer(current_map_layer_);
        current_map_layer_.clear();
    }
    PushLayerToMap(selected);
}

// Strip a "_log" suffix to get the underlying GridArray key.
// Layer keys ending in "_log" share data with the bare key but force log scale.
static std::string BaseLayerName(const std::string& name) {
    constexpr std::string_view LOG_SUFFIX = "_log";
    if (name.size() > LOG_SUFFIX.size() &&
        name.compare(name.size() - LOG_SUFFIX.size(), LOG_SUFFIX.size(), LOG_SUFFIX) == 0)
        return name.substr(0, name.size() - LOG_SUFFIX.size());
    return name;
}

// Use log-scale colour ramp when the key ends in "_log".
// NaN no-data cells are always transparent regardless of scale mode.
static bool UseLogScale(const std::string& name) {
    return BaseLayerName(name) != name;  // true iff name ends in "_log"
}

static std::string LayerUnits(const std::string& name, bool log_scale) {
    const std::string base = BaseLayerName(name);
    std::string units;
    if (base == "groundwave" || base == "skywave" || base == "atm_noise")
        units = bp::ui::DBUVM;
    else if (base == "snr" || base == "gdr" || base == "sgr")
        units = "dB";
    else if (base == "repeatable" || base == "absolute_accuracy" ||
             base == "absolute_accuracy_corrected")
        units = "m";
    else if (base == "asf")
        units = "ml";
    else if (base == "asf_gradient")
        units = "ml/km";
    else if (base == "whdop" || base == "confidence")
        units = "dimensionless";

    if (log_scale && !units.empty())
        units += ", log scale";
    else if (log_scale)
        units = "log scale";
    return units;
}

void MainFrame::PushLayerToMap(const std::string& name) {
    if (!last_grid_data_) return;
    // Take a local copy of the shared_ptr so the GridData stays alive even
    // if a new compute result replaces last_grid_data_ during event processing.
    auto pinned = last_grid_data_;
    // "_log" variants share data with the bare-name layer.
    const std::string base = BaseLayerName(name);
    auto it = pinned->layers.find(base);
    if (it == pinned->layers.end()) return;
    const auto& arr = it->second;
    if (arr.values.empty()) return;

    const bool log_scale = UseLogScale(name);
    const bp::ScaleMode scale = log_scale ? bp::ScaleMode::Log : bp::ScaleMode::Linear;

    SetStatusText("Updating map...", SB_STATUS);
    double vmin, vmax;
    if (arr.width > 0 && arr.height > 0) {
        auto img = arr.to_image_data(scale);
        vmin = img.display_vmin;
        vmax = img.display_vmax;
        map_panel_->UpdateLayerImage(name, img);
    } else {
        auto [rv, rm] = arr.display_range(scale);
        vmin = rv;  vmax = rm;
        map_panel_->UpdateLayer(name, arr.to_geojson(scale));
    }
    map_panel_->UpdateLegend(name, vmin, vmax, LayerUnits(name, log_scale));
    current_map_layer_ = name;
    SetStatusText("Ready", SB_STATUS);
}

void MainFrame::OnMapClick(double lat, double lon) {
    if (!placement_mode_) return;

    TransmitterSite site;
    site.name = "TX-" + std::to_string(next_tx_id_);
    site.lat  = lat;
    site.lon  = lon;
    SlotConfig sc;
    sc.slot      = next_tx_id_;
    sc.is_master = (next_tx_id_ == 1);
    site.slots.push_back(sc);

    int id = (int)scenario_.transmitter_sites.size();
    scenario_.transmitter_sites.push_back(site);

    map_panel_->AddTransmitterMarker(id, lat, lon, site.name, site.locked,
                                     site.slot_count());
    selected_site_id_ = id;
    param_editor_->SetSiteList(scenario_.transmitter_sites);
    param_editor_->LoadSite(id, site);
    ++next_tx_id_;
    MarkDirty();
    TriggerRecompute();
}

void MainFrame::OnTransmitterMoved(int id, double lat, double lon) {
    if (id < 0 || id >= (int)scenario_.transmitter_sites.size()) return;
    scenario_.transmitter_sites[id].lat = lat;
    scenario_.transmitter_sites[id].lon = lon;
    MarkDirty();
    TriggerRecompute();
}

void MainFrame::OnCursorMoved(double lat, double lon) {
    SetStatusText(wxString::Format("%.5f, %.5f", lat, lon), SB_WGS84);
    try {
        LatLon osgb36 = osgb::wgs84_to_osgb36({lat, lon});
        EastNorth en  = national_grid::latlon_to_en(osgb36);
        std::string ref = national_grid::en_to_gridref(en, 8);
        SetStatusText(ref, SB_OSGB);
    } catch (...) {
        SetStatusText("", SB_OSGB);
    }
}

void MainFrame::OnToolPlaceTx(wxCommandEvent& evt) {
    placement_mode_ = evt.IsChecked();
    if (placement_mode_) {
        rx_placement_mode_ = false;
        GetToolBar()->ToggleTool(ID_TOOL_PLACE_RX, false);
    }
    map_panel_->SetPlacementMode(placement_mode_);
}

void MainFrame::OnToolPlaceRx(wxCommandEvent& evt) {
    rx_placement_mode_ = evt.IsChecked();
    if (rx_placement_mode_) {
        placement_mode_ = false;
        GetToolBar()->ToggleTool(ID_TOOL_PLACE_TX, false);
        map_panel_->SetPlacementMode(false);
    }
    map_panel_->SetReceiverPlacementMode(rx_placement_mode_);
}

void MainFrame::OnToolCompute(wxCommandEvent& evt) {
    compute_enabled_ = evt.IsChecked();
    if (compute_enabled_) {
        net_config_->FlushPending();
        TriggerRecompute();
    } else {
        // Clear all map layers when computation is disabled
        for (auto& name : {"groundwave","skywave","atm_noise","snr","sgr","gdr",
                            "whdop","repeatable","asf","asf_gradient",
                            "absolute_accuracy","absolute_accuracy_corrected",
                            "absolute_accuracy_delta","confidence"}) {
            map_panel_->ClearLayer(name);
        }
        SetStatusText("Computation disabled", SB_STATUS);
    }
}

void MainFrame::OnTransmitterSelected(int id) {
    if (id >= 0 && id < (int)scenario_.transmitter_sites.size()) {
        selected_site_id_ = id;
        param_editor_->LoadSite(id, scenario_.transmitter_sites[id]);
        map_panel_->SelectTransmitterMarker(id);
    }
}

void MainFrame::OnEditDeleteTx(wxCommandEvent& /*evt*/) {
    if (selected_site_id_ < 0 || selected_site_id_ >= (int)scenario_.transmitter_sites.size()) {
        wxMessageBox("No transmitter selected.", "Delete Transmitter",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    DeleteSite(selected_site_id_);
}

void MainFrame::DeleteSite(int id) {
    if (id < 0 || id >= (int)scenario_.transmitter_sites.size()) return;

    std::string name = scenario_.transmitter_sites[id].name;
    int ret = wxMessageBox(
        wxString::Format("Delete transmitter site \"%s\"?", name.c_str()),
        "Delete Site", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    if (ret != wxYES) return;

    // Remove from scenario
    scenario_.transmitter_sites.erase(scenario_.transmitter_sites.begin() + id);

    // Remove all transmitter markers from the map and re-add with new indices
    // (IDs are vector indices, so they shift after erasure)
    map_panel_->RemoveTransmitterMarker(id);
    // Re-index: remove markers after the deleted one, then re-add them
    for (int i = id; i < (int)scenario_.transmitter_sites.size(); ++i) {
        map_panel_->RemoveTransmitterMarker(i + 1);  // old index
        const auto& site = scenario_.transmitter_sites[i];
        map_panel_->AddTransmitterMarker(i, site.lat, site.lon, site.name,
                                         site.locked, site.slot_count());
    }

    // Clear selection
    selected_site_id_ = -1;
    param_editor_->ClearSelection();
    param_editor_->SetSiteList(scenario_.transmitter_sites);
    map_panel_->SelectTransmitterMarker(-1);

    MarkDirty();
    TriggerRecompute();
}

static wxString FormatReceiverPosition(double lat, double lon) {
    wxString pos = wxString::Format("RX: %.5f, %.5f", lat, lon);
    try {
        LatLon osgb36 = osgb::wgs84_to_osgb36({lat, lon});
        EastNorth en  = national_grid::latlon_to_en(osgb36);
        pos += "  |  " + wxString::FromUTF8(national_grid::en_to_gridref(en, 8));
    } catch (...) {}
    return pos;
}

void MainFrame::OnReceiverPlaced(double lat, double lon) {
    rx_lat_    = lat;
    rx_lon_    = lon;
    rx_placed_ = true;
    // Deactivate receiver placement mode after first click
    rx_placement_mode_ = false;
    GetToolBar()->ToggleTool(ID_TOOL_PLACE_RX, false);
    map_panel_->SetReceiverPlacementMode(false);
    map_panel_->SetReceiverMarker(lat, lon, rx_locked_);
    receiver_panel_->SetPositionText(FormatReceiverPosition(lat, lon));
    auto results = computeAtPoint(lat, lon, scenario_);
    receiver_panel_->SetResults(results);
}

void MainFrame::ClearMapTransmitters(int count) {
    for (int i = count - 1; i >= 0; --i)
        map_panel_->RemoveTransmitterMarker(i);
}

void MainFrame::SyncGridBounds() {
    map_panel_->SetGridBounds(scenario_.grid.lat_min, scenario_.grid.lat_max,
                               scenario_.grid.lon_min, scenario_.grid.lon_max);
}

void MainFrame::OnGridBoundsChanged(double lat_min, double lat_max,
                                     double lon_min, double lon_max) {
    scenario_.grid.lat_min = lat_min;
    scenario_.grid.lat_max = lat_max;
    scenario_.grid.lon_min = lon_min;
    scenario_.grid.lon_max = lon_max;
    net_config_->SetBoundsFromMap(lat_min, lat_max, lon_min, lon_max);
    MarkDirty();
    TriggerRecompute();
}

void MainFrame::SyncMapTransmitters() {
    for (int i = 0; i < (int)scenario_.transmitter_sites.size(); ++i) {
        const auto& site = scenario_.transmitter_sites[i];
        map_panel_->AddTransmitterMarker(i, site.lat, site.lon, site.name,
                                         site.locked, site.slot_count());
    }
    // Set next_tx_id_ past the highest slot number in the loaded scenario
    int max_slot = 0;
    for (const auto& site : scenario_.transmitter_sites)
        for (const auto& sc : site.slots)
            if (sc.slot > max_slot) max_slot = sc.slot;
    next_tx_id_ = max_slot + 1;
}

void MainFrame::OnFileNew(wxCommandEvent& /*evt*/) {
    if (!ConfirmDiscardChanges()) return;
    int old_count = (int)scenario_.transmitter_sites.size();
    scenario_        = Scenario{};
    current_file_    = "";
    dirty_           = false;
    next_tx_id_      = 1;
    selected_site_id_ = -1;
    placement_mode_  = false;
    ClearMapTransmitters(old_count);
    net_config_->SetScenario(&scenario_);
    param_editor_->SetFrequency(scenario_.frequencies.f1_hz);
    param_editor_->SetSiteList(scenario_.transmitter_sites);
    param_editor_->LoadReceiver(scenario_.receiver);
    SyncGridBounds();
    UpdateStatusBarMl();
    UpdateTitle();
    TriggerRecompute();
}

void MainFrame::OnFileOpen(wxCommandEvent& /*evt*/) {
    if (!ConfirmDiscardChanges()) return;
    wxFileDialog dlg(this, "Open Scenario", "", "",
                     "BANDPASS Scenario (*.toml)|*.toml|All files (*.*)|*.*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    std::string path = dlg.GetPath().ToStdString();

    int old_count = (int)scenario_.transmitter_sites.size();

    try {
        scenario_     = toml_io::load(path);
        current_file_ = path;
        dirty_        = false;
    } catch (std::exception& e) {
        wxMessageBox(e.what(), "Error opening file", wxOK | wxICON_ERROR, this);
        return;
    }

    // Clear old transmitter markers, then add markers for loaded transmitters
    ClearMapTransmitters(old_count);
    selected_site_id_ = -1;
    placement_mode_   = false;
    SyncMapTransmitters();

    net_config_->SetScenario(&scenario_);
    param_editor_->SetFrequency(scenario_.frequencies.f1_hz);
    param_editor_->SetSiteList(scenario_.transmitter_sites);
    param_editor_->LoadReceiver(scenario_.receiver);
    param_editor_->ClearSelection();
    map_panel_->SelectTransmitterMarker(-1);
    SyncGridBounds();
    UpdateStatusBarMl();
    UpdateTitle();
    TriggerRecompute();
}

void MainFrame::OnFileSave(wxCommandEvent& evt) {
    if (current_file_.empty()) { OnFileSaveAs(evt); return; }
    try {
        toml_io::save(scenario_, current_file_);
        dirty_ = false;
        UpdateTitle();
    } catch (std::exception& e) {
        wxMessageBox(e.what(), "Error saving file", wxOK | wxICON_ERROR, this);
    }
}

void MainFrame::OnFileSaveAs(wxCommandEvent& /*evt*/) {
    wxFileDialog dlg(this, "Save Scenario", "", "",
                     "BANDPASS Scenario (*.toml)|*.toml|All files (*.*)|*.*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    current_file_ = dlg.GetPath().ToStdString();
    // On Linux/GTK the file dialog does not auto-append the filter extension.
    // If the TOML filter is selected and the path lacks a .toml extension, add it.
    if (dlg.GetFilterIndex() == 0) {
        std::filesystem::path p(current_file_);
        if (p.extension() != ".toml") {
            current_file_ += ".toml";
        }
    }
    try {
        toml_io::save(scenario_, current_file_);
        dirty_ = false;
        UpdateTitle();
    } catch (std::exception& e) {
        wxMessageBox(e.what(), "Error saving file", wxOK | wxICON_ERROR, this);
    }
}

void MainFrame::OnViewNetworkConfig(wxCommandEvent& /*evt*/) {
    auto& pane = aui_.GetPane("netcfg");
    pane.Show(!pane.IsShown());
    aui_.Update();
}

void MainFrame::OnViewLayerPanel(wxCommandEvent& /*evt*/) {
    auto& pane = aui_.GetPane("layers");
    pane.Show(!pane.IsShown());
    aui_.Update();
}

void MainFrame::OnViewParamEditor(wxCommandEvent& /*evt*/) {
    auto& pane = aui_.GetPane("params");
    pane.Show(!pane.IsShown());
    aui_.Update();
}

void MainFrame::OnHelpAbout(wxCommandEvent& /*evt*/) {
    wxAboutDialogInfo info;
    info.SetName("BANDPASS II");
    info.SetVersion(bp::VERSION);
    info.SetCopyright("(C) 2024-2025 The BANDPASS II Authors");
    info.SetDescription(
        "Coverage and positioning accuracy planner for\n"
        "Datatrak-type LF radio navigation networks.\n"
        "\n"
        "Physics model based on:\n"
        "  Williams (2004), \"Prediction of the Coverage and\n"
        "  Performance of the Datatrak Low-Frequency Tracking\n"
        "  System\", University of Wales Bangor PhD thesis.\n"
        "\n"
        "Propagation stages: groundwave (ITU P.368 + Monteath),\n"
        "skywave (ITU P.684), noise (ITU P.372), SNR/GDR, WHDOP,\n"
        "repeatable accuracy, ASF, and absolute accuracy.");
    info.SetLicence(
        "BANDPASS II is free software: you can redistribute it\n"
        "and/or modify it under the terms of the GNU General\n"
        "Public License as published by the Free Software\n"
        "Foundation, either version 3 of the License, or\n"
        "(at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will\n"
        "be useful, but WITHOUT ANY WARRANTY; without even the\n"
        "implied warranty of MERCHANTABILITY or FITNESS FOR A\n"
        "PARTICULAR PURPOSE. See the GNU General Public License\n"
        "for more details.");
    info.AddDeveloper("The BANDPASS II Authors");
    info.AddDocWriter("The BANDPASS II Authors");
    wxAboutBox(info, this);
}

void MainFrame::OnClose(wxCloseEvent& evt) {
    if (evt.CanVeto() && !ConfirmDiscardChanges()) {
        evt.Veto();
        return;
    }
    if (compute_mgr_) compute_mgr_->Shutdown();
    aui_.UnInit();
    Destroy();
}

bool MainFrame::ConfirmDiscardChanges() {
    if (!dirty_) return true;
    int ret = wxMessageBox("Discard unsaved changes?", "BANDPASS II",
                           wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
    return ret == wxYES;
}

void MainFrame::OnReceiverMoved(double lat, double lon) {
    rx_lat_    = lat;
    rx_lon_    = lon;
    rx_placed_ = true;
    receiver_panel_->SetPositionText(FormatReceiverPosition(lat, lon));
    auto results = computeAtPoint(lat, lon, scenario_);
    receiver_panel_->SetResults(results);
}

void MainFrame::OnExportSimulator() {
    if (!rx_placed_) {
        wxMessageBox("Place a receiver on the map first.",
                     "Export for Simulator", wxOK | wxICON_INFORMATION, this);
        return;
    }

    // Build export text matching datatrak_gen.h slotPhaseOffset[] format
    wxString text;
    text += "# BANDPASS II receiver phase export\n";
    text += wxString::Format("# Receiver: %.5f, %.5f\n", rx_lat_, rx_lon_);
    text += "# Format: slot  f1+  f1-  f2+  f2-  snr_db  gdr_db\n";
    text += "# Phases: integer millilanes 0-999 (slotPhaseOffset scale)\n";

    auto results = computeAtPoint(rx_lat_, rx_lon_, scenario_);
    for (const auto& r : results) {
        text += wxString::Format("%d  %d  %d  %d  %d  %.1f  %.1f\n",
            r.slot,
            (int)(r.f1plus_phase  * 1000) % 1000,
            (int)(r.f1minus_phase * 1000) % 1000,
            (int)(r.f2plus_phase  * 1000) % 1000,
            (int)(r.f2minus_phase * 1000) % 1000,
            r.snr_db, r.gdr_db);
    }

    // Copy to clipboard
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
    }

    // Save to file
    wxFileDialog dlg(this, "Save Simulator Export", "", "receiver_phase.txt",
                     "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_OK) {
        wxFile f(dlg.GetPath(), wxFile::write);
        if (f.IsOpened()) {
            f.Write(text);
            SetStatusText("Phase export saved.", SB_STATUS);
        }
    }
}

void MainFrame::OnExportAlmanac(almanac::FirmwareFormat fmt) {
    GridData gd;
    std::string text = almanac::generate_almanac(scenario_, gd, fmt);

    wxString defaultName = (fmt == almanac::FirmwareFormat::V7)
                            ? "almanac_v7.txt" : "almanac_v16.txt";
    wxFileDialog dlg(this, "Export Almanac Commands", "", defaultName,
                     "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;

    wxFile f(dlg.GetPath(), wxFile::write);
    if (f.IsOpened()) {
        f.Write(wxString(text));
        SetStatusText("Almanac exported.", SB_STATUS);
    } else {
        wxMessageBox("Could not write file.", "Export Error", wxICON_ERROR, this);
    }
}

void MainFrame::OnExportLayers(const std::string& format) {
    if (!last_grid_data_ || last_grid_data_->layers.empty()) {
        wxMessageBox("No computed layers available. Run a computation first.",
                     "Export Layers", wxICON_INFORMATION, this);
        return;
    }

    // Pick the active layer (first visible layer in the grid data).
    // If the layer panel has a selection, use that; otherwise use the first layer.
    std::string layer_name;
    if (layer_panel_) {
        layer_name = layer_panel_->GetSelectedLayer();
    }
    if (layer_name.empty() || last_grid_data_->layers.find(layer_name) == last_grid_data_->layers.end()) {
        layer_name = last_grid_data_->layers.begin()->first;
    }
    const GridArray& layer = last_grid_data_->layers.at(layer_name);

    // Build file dialog filters based on format
    wxString title, wildcard, defaultName;
    if (format == "csv") {
        title       = "Export Layer as CSV";
        wildcard    = "CSV files (*.csv)|*.csv|All files (*.*)|*.*";
        defaultName = wxString(layer_name) + ".csv";
    } else if (format == "png") {
        title       = "Export Layer as PNG";
        wildcard    = "PNG images (*.png)|*.png|All files (*.*)|*.*";
        defaultName = wxString(layer_name) + ".png";
    } else if (format == "html") {
        title       = "Export HTML Report";
        wildcard    = "HTML files (*.html)|*.html|All files (*.*)|*.*";
        defaultName = wxString(scenario_.name) + "_report.html";
    } else {
        title       = "Export Layer as GeoTIFF";
        wildcard    = "GeoTIFF files (*.tif)|*.tif|All files (*.*)|*.*";
        defaultName = wxString(layer_name) + ".tif";
    }

    wxFileDialog dlg(this, title, "", defaultName, wildcard,
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;

    std::string path = dlg.GetPath().ToStdString();
    std::string err;

    if (format == "csv") {
        err = ExportManager::export_csv(layer, path);
    } else if (format == "png") {
        err = ExportManager::export_png(layer, path);
    } else if (format == "html") {
        err = ExportManager::export_html(*last_grid_data_, scenario_, path);
    } else {
        err = ExportManager::export_geotiff(layer, path);
    }

    if (err.empty()) {
        SetStatusText(wxString::Format("Exported %s → %s", layer_name.c_str(), path.c_str()), SB_STATUS);
    } else {
        wxMessageBox(wxString::FromUTF8(err), "Export Error", wxICON_ERROR, this);
    }
}

void MainFrame::OnImportMonitorLog(wxCommandEvent& /*evt*/) {
    wxFileDialog dlg(this, "Import Monitor Station Log", "", "",
                     "Monitor logs (*.csv;*.txt)|*.csv;*.txt|All files (*.*)|*.*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;

    std::string path = dlg.GetPath().ToStdString();

    // Ask which monitor station to attach the log to (or create new)
    std::vector<wxString> choices;
    for (const auto& ms : scenario_.monitor_stations)
        choices.push_back(wxString::FromUTF8(ms.name));
    choices.push_back("< Create new monitor station >");

    wxSingleChoiceDialog choice_dlg(this, "Attach log to monitor station:",
                                    "Monitor Station", (int)choices.size(),
                                    choices.data());
    if (choice_dlg.ShowModal() != wxID_OK) return;
    int sel = choice_dlg.GetSelection();

    try {
        MonitorStation imported = almanac::import_monitor_log(path);
        if (sel < (int)scenario_.monitor_stations.size()) {
            // Merge into existing monitor station
            auto& ms = scenario_.monitor_stations[sel];
            for (auto& c : imported.corrections) {
                // Replace existing correction for same pattern, or append
                bool found = false;
                for (auto& existing : ms.corrections) {
                    if (existing.pattern == c.pattern) {
                        existing = c;
                        found = true;
                        break;
                    }
                }
                if (!found) ms.corrections.push_back(c);
            }
        } else {
            // Create new monitor station at (0, 0) — user can edit coordinates
            scenario_.monitor_stations.push_back(imported);
        }

        // Apply corrections to pattern_offsets and trigger recompute
        scenario_.pattern_offsets = almanac::apply_monitor_corrections(scenario_);
        MarkDirty();
        TriggerRecompute();

        // Run consistency check and show diagnostic
        if (scenario_.monitor_stations.size() >= 2) {
            auto report = almanac::check_consistency(scenario_);
            std::string msg = report.summary;
            for (const auto& item : report.items)
                msg += "\n\n" + item.detail;
            if (!report.inconsistencies.empty()) {
                msg += "\n\nInconsistencies:";
                for (const auto& iss : report.inconsistencies)
                    msg += "\n  Pattern " + iss.pattern + ": "
                         + iss.monitor1 + " vs " + iss.monitor2
                         + " (" + std::to_string(iss.max_delta_ml) + " ml)";
                wxMessageBox(wxString::FromUTF8(msg), "Monitor Consistency",
                             wxICON_WARNING, this);
            } else {
                SetStatusText(wxString::FromUTF8("Monitor import OK: " + report.summary),
                              SB_STATUS);
            }
        } else {
            SetStatusText(
                wxString::Format("Imported %d correction(s) from monitor log",
                                 (int)imported.corrections.size()),
                SB_STATUS);
        }
    } catch (const std::exception& e) {
        wxMessageBox(wxString::FromUTF8(e.what()), "Import Error",
                     wxICON_ERROR, this);
    }
}

void MainFrame::OnComputePatternOffsets(wxCommandEvent& /*evt*/) {
    if (scenario_.transmitter_sites.empty()) {
        wxMessageBox("Add transmitters to the scenario first.",
                     "Compute Pattern Offsets", wxICON_INFORMATION, this);
        return;
    }

    PoRefDialog dlg(this, scenario_);
    if (dlg.ShowModal() != wxID_OK) return;

    scenario_.pattern_offsets = dlg.result_offsets;
    MarkDirty();
    TriggerRecompute();

    SetStatusText(
        wxString::Format("Pattern offsets computed: %zu pair(s).",
                         scenario_.pattern_offsets.size()),
        SB_STATUS);
}

} // namespace bp
