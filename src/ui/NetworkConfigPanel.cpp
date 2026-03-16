#include "NetworkConfigPanel.h"
#include "../coords/Osgb.h"
#include "../model/DataPaths.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/checkbox.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <cmath>
#include <filesystem>
#include <set>

namespace bp {

// Helper: create a labelled text field inside a flex grid sizer.
static wxTextCtrl* MakeField(wxWindow* parent, const char* label,
                              wxFlexGridSizer* gs) {
    gs->Add(new wxStaticText(parent, wxID_ANY, label),
            0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    auto* tc = new wxTextCtrl(parent, wxID_ANY);
    gs->Add(tc, 1, wxEXPAND | wxBOTTOM, 2);
    return tc;
}

// Scan data search dirs for terrain files (.tif/.tiff) and SRTM tile
// directories (directories containing .hgt files).
static std::vector<std::pair<std::string, std::string>>
discover_terrain_files() {
    // pair<display_name, absolute_path>
    std::vector<std::pair<std::string, std::string>> results;
    std::set<std::string> seen;  // deduplicate by canonical path

    for (const auto& dir : data_search_dirs()) {
        if (!std::filesystem::is_directory(dir)) continue;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                std::string canonical;
                try {
                    canonical = std::filesystem::canonical(entry.path()).string();
                } catch (...) {
                    canonical = entry.path().string();
                }
                if (seen.count(canonical)) continue;

                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    // case-insensitive extension check
                    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                    if (ext == ".tif" || ext == ".tiff") {
                        seen.insert(canonical);
                        results.push_back({entry.path().filename().string(),
                                           entry.path().string()});
                    }
                } else if (entry.is_directory()) {
                    // Check if directory contains any .hgt files
                    bool has_hgt = false;
                    try {
                        for (const auto& sub : std::filesystem::directory_iterator(entry.path())) {
                            if (sub.is_regular_file()) {
                                auto se = sub.path().extension().string();
                                for (auto& c : se) c = (char)std::tolower((unsigned char)c);
                                if (se == ".hgt") { has_hgt = true; break; }
                            }
                        }
                    } catch (...) {}
                    if (has_hgt) {
                        seen.insert(canonical);
                        results.push_back({entry.path().filename().string() + "/",
                                           entry.path().string()});
                    }
                }
            }
        } catch (...) {}
    }
    return results;
}

// Scan data search dirs for conductivity GeoTIFF files.
static std::vector<std::pair<std::string, std::string>>
discover_conductivity_files() {
    std::vector<std::pair<std::string, std::string>> results;
    std::set<std::string> seen;

    for (const auto& dir : data_search_dirs()) {
        if (!std::filesystem::is_directory(dir)) continue;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext != ".tif" && ext != ".tiff") continue;

                std::string canonical;
                try {
                    canonical = std::filesystem::canonical(entry.path()).string();
                } catch (...) {
                    canonical = entry.path().string();
                }
                if (seen.count(canonical)) continue;
                seen.insert(canonical);
                results.push_back({entry.path().filename().string(),
                                   entry.path().string()});
            }
        } catch (...) {}
    }
    return results;
}

NetworkConfigPanel::NetworkConfigPanel(wxWindow* parent)
    : wxScrolledWindow(parent)
{
    debounce_.Bind(wxEVT_TIMER, &NetworkConfigPanel::OnDebounceTimer, this);

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // -- Scenario ---------------------------------------------------------
    {
        auto* box   = new wxStaticBox(this, wxID_ANY, "Scenario");
        auto* bsiz  = new wxStaticBoxSizer(box, wxVERTICAL);
        auto* gs    = new wxFlexGridSizer(2, 4, 6);
        gs->AddGrowableCol(1);

        name_field_ = MakeField(this, "Name", gs);
        name_field_->Bind(wxEVT_TEXT, &NetworkConfigPanel::OnOtherChanged, this);

        bsiz->Add(gs, 0, wxEXPAND | wxALL, 4);
        outer->Add(bsiz, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }

    // -- Frequencies ------------------------------------------------------
    {
        auto* box   = new wxStaticBox(this, wxID_ANY, "Frequencies");
        auto* bsiz  = new wxStaticBoxSizer(box, wxVERTICAL);
        auto* gs    = new wxFlexGridSizer(2, 4, 6);
        gs->AddGrowableCol(1);

        f1_field_ = MakeField(this, "F1 (kHz)", gs);
        f1_field_->SetValue("146.4375");
        f1_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnFreqChanged,    this);
        f1_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);

        f2_field_ = MakeField(this, "F2 (kHz)", gs);
        f2_field_->SetValue("131.2500");
        f2_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnFreqChanged,    this);
        f2_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);

        // Millilane display (spanning both columns)
        gs->Add(new wxStaticText(this, wxID_ANY, ""), 0);
        ml_label_ = new wxStaticText(this, wxID_ANY,
                        "1 ml(f1) = 2.047 m   1 ml(f2) = 2.285 m");
        gs->Add(ml_label_, 0, wxBOTTOM, 2);

        bsiz->Add(gs, 0, wxEXPAND | wxALL, 4);
        outer->Add(bsiz, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }

    // -- Propagation ------------------------------------------------------
    {
        auto* box   = new wxStaticBox(this, wxID_ANY, "Propagation");
        auto* bsiz  = new wxStaticBoxSizer(box, wxVERTICAL);
        auto* gs    = new wxFlexGridSizer(2, 4, 6);
        gs->AddGrowableCol(1);

        gs->Add(new wxStaticText(this, wxID_ANY, "Model"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        wxArrayString models;
        models.Add("Homogeneous (fast)"); models.Add("Millington mixed-path"); models.Add("GRWAVE (accurate)");
        prop_model_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, models);
        prop_model_->SetSelection(1);  // Millington default
        prop_model_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnOtherChanged, this);
        gs->Add(prop_model_, 1, wxEXPAND | wxBOTTOM, 2);

        gs->Add(new wxStaticText(this, wxID_ANY, ""), 0);
        airy_cache_cb_ = new wxCheckBox(this, wxID_ANY,
                                         "Pre-compute Airy distance cache (faster, more memory)");
        airy_cache_cb_->SetValue(true);
        airy_cache_cb_->Bind(wxEVT_CHECKBOX, &NetworkConfigPanel::OnOtherChanged, this);
        gs->Add(airy_cache_cb_, 1, wxEXPAND | wxBOTTOM, 2);

        bsiz->Add(gs, 0, wxEXPAND | wxALL, 4);
        outer->Add(bsiz, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }

    // -- Grid -------------------------------------------------------------
    {
        auto* box   = new wxStaticBox(this, wxID_ANY, "Grid");
        auto* bsiz  = new wxStaticBoxSizer(box, wxVERTICAL);
        auto* gs    = new wxFlexGridSizer(2, 4, 6);
        gs->AddGrowableCol(1);

        lat_min_field_ = MakeField(this, "Lat min", gs);
        lat_min_field_->SetValue("49.5");
        lat_min_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnBoundsChanged,  this);
        lat_min_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);

        lat_max_field_ = MakeField(this, "Lat max", gs);
        lat_max_field_->SetValue("59.0");
        lat_max_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnBoundsChanged,  this);
        lat_max_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);

        lon_min_field_ = MakeField(this, "Lon min", gs);
        lon_min_field_->SetValue("-7.0");
        lon_min_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnBoundsChanged,  this);
        lon_min_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);

        lon_max_field_ = MakeField(this, "Lon max", gs);
        lon_max_field_->SetValue("2.5");
        lon_max_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnBoundsChanged,  this);
        lon_max_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);

        // Resolution
        gs->Add(new wxStaticText(this, wxID_ANY, "Grid res (km)"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        res_field_ = new wxTextCtrl(this, wxID_ANY, "10.0");
        res_field_->Bind(wxEVT_TEXT,       &NetworkConfigPanel::OnResChanged,     this);
        res_field_->Bind(wxEVT_KILL_FOCUS, &NetworkConfigPanel::OnFieldKillFocus, this);
        gs->Add(res_field_, 1, wxEXPAND | wxBOTTOM, 2);

        // Point count display
        gs->Add(new wxStaticText(this, wxID_ANY, ""), 0);
        res_count_label_ = new wxStaticText(this, wxID_ANY, "");
        gs->Add(res_count_label_, 0, wxBOTTOM, 2);

        // Datum / OSTN15 status
        gs->Add(new wxStaticText(this, wxID_ANY, "Datum"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        ostn15_label_ = new wxStaticText(this, wxID_ANY, "");
        gs->Add(ostn15_label_, 0, wxBOTTOM, 2);

        bsiz->Add(gs, 0, wxEXPAND | wxALL, 4);
        outer->Add(bsiz, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }

    // -- Terrain ----------------------------------------------------------
    {
        auto* box   = new wxStaticBox(this, wxID_ANY, "Terrain");
        auto* bsiz  = new wxStaticBoxSizer(box, wxVERTICAL);
        auto* gs    = new wxFlexGridSizer(2, 4, 6);
        gs->AddGrowableCol(1);

        gs->Add(new wxStaticText(this, wxID_ANY, "Source"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        terrain_src_ = new wxChoice(this, wxID_ANY);
        terrain_src_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnTerrainSrcChanged, this);
        gs->Add(terrain_src_, 1, wxEXPAND | wxBOTTOM, 2);

        // Status
        gs->Add(new wxStaticText(this, wxID_ANY, ""), 0);
        terrain_status_label_ = new wxStaticText(this, wxID_ANY, "");
        gs->Add(terrain_status_label_, 0, wxBOTTOM, 2);

        bsiz->Add(gs, 0, wxEXPAND | wxALL, 4);
        outer->Add(bsiz, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }
    PopulateTerrainChoices();

    // -- Conductivity -----------------------------------------------------
    {
        auto* box   = new wxStaticBox(this, wxID_ANY, "Conductivity");
        auto* bsiz  = new wxStaticBoxSizer(box, wxVERTICAL);
        auto* gs    = new wxFlexGridSizer(2, 4, 6);
        gs->AddGrowableCol(1);

        gs->Add(new wxStaticText(this, wxID_ANY, "Source"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        cond_src_ = new wxChoice(this, wxID_ANY);
        cond_src_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnCondSrcChanged, this);
        gs->Add(cond_src_, 1, wxEXPAND | wxBOTTOM, 2);

        // File path row (only shown for "Other...")
        gs->Add(new wxStaticText(this, wxID_ANY, "File"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            cond_file_ = new wxTextCtrl(this, wxID_ANY);
            cond_file_->Bind(wxEVT_TEXT, &NetworkConfigPanel::OnFilePath, this);
            cond_browse_ = new wxButton(this, wxID_ANY, "Browse...",
                                         wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            cond_browse_->Bind(wxEVT_BUTTON, &NetworkConfigPanel::OnCondBrowse, this);
            row->Add(cond_file_,   1, wxEXPAND | wxRIGHT, 4);
            row->Add(cond_browse_, 0, wxALIGN_CENTER_VERTICAL);
            gs->Add(row, 1, wxEXPAND | wxBOTTOM, 2);
        }

        // Status
        gs->Add(new wxStaticText(this, wxID_ANY, ""), 0);
        cond_status_label_ = new wxStaticText(this, wxID_ANY, "");
        gs->Add(cond_status_label_, 0, wxBOTTOM, 2);

        bsiz->Add(gs, 0, wxEXPAND | wxALL, 4);
        outer->Add(bsiz, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }
    PopulateCondChoices();

    SetSizer(outer);
    SetScrollRate(0, 10);
    FitInside();

    UpdateCondFileState();
    UpdateTerrainLabel();
    UpdateCondLabel();
}

// ---------------------------------------------------------------------------
// Terrain dropdown: discover files and populate
// ---------------------------------------------------------------------------

void NetworkConfigPanel::PopulateTerrainChoices() {
    terrain_paths_.clear();
    terrain_src_->Clear();

    // Index 0: Flat
    terrain_src_->Append("Flat");

    // Discovered terrain files and SRTM tile directories
    auto discovered = discover_terrain_files();
    for (const auto& [name, path] : discovered) {
        terrain_src_->Append(name);
        terrain_paths_.push_back(path);
    }

    // Last entry: Other...
    terrain_src_->Append("Other...");
    terrain_src_->SetSelection(0);
}

int NetworkConfigPanel::TerrainIndexForPath(const std::string& path) const {
    if (path.empty()) return 0;  // Flat

    // Try canonical match against discovered paths
    std::string canonical;
    try {
        if (std::filesystem::exists(path))
            canonical = std::filesystem::canonical(path).string();
        else
            canonical = path;
    } catch (...) {
        canonical = path;
    }

    for (size_t i = 0; i < terrain_paths_.size(); ++i) {
        std::string cand;
        try {
            if (std::filesystem::exists(terrain_paths_[i]))
                cand = std::filesystem::canonical(terrain_paths_[i]).string();
            else
                cand = terrain_paths_[i];
        } catch (...) {
            cand = terrain_paths_[i];
        }
        if (cand == canonical)
            return (int)(i + 1);  // +1 because index 0 is "Flat"
    }

    // Not in the discovered list — add it dynamically before "Other..."
    return -1;  // caller should handle
}

// ---------------------------------------------------------------------------
// Conductivity dropdown: Built-in + discovered files + Other...
// ---------------------------------------------------------------------------

void NetworkConfigPanel::PopulateCondChoices() {
    cond_src_->Clear();

    // Index 0: Built-in
    cond_src_->Append("Built-in");

    // Discovered conductivity files
    auto discovered = discover_conductivity_files();
    for (const auto& [name, path] : discovered) {
        // Use friendly names for well-known files
        std::string display = name;
        if (name == "conductivity_p832.tif") display = "ITU P.832 (conductivity_p832.tif)";
        else if (name == "conductivity_bgs.tif") display = "BGS (conductivity_bgs.tif)";
        cond_src_->Append(display);
    }

    // Last entry: Other...
    cond_src_->Append("Other...");
    cond_src_->SetSelection(0);

    // Store paths for lookup (not including Built-in or Other...)
    // Reuse terrain_paths_ pattern — but we need a separate member.
    // For now, use client data on the wxChoice items.
    // Actually, store them similarly to terrain.
    // We'll just re-discover in SaveToScenario. Keep it simple.
}

int NetworkConfigPanel::CondIndexForPath(const std::string& path) const {
    if (path.empty()) return 0;  // Built-in

    auto discovered = discover_conductivity_files();
    std::string canonical;
    try {
        if (std::filesystem::exists(path))
            canonical = std::filesystem::canonical(path).string();
        else
            canonical = path;
    } catch (...) {
        canonical = path;
    }

    for (size_t i = 0; i < discovered.size(); ++i) {
        std::string cand;
        try {
            if (std::filesystem::exists(discovered[i].second))
                cand = std::filesystem::canonical(discovered[i].second).string();
            else
                cand = discovered[i].second;
        } catch (...) {
            cand = discovered[i].second;
        }
        if (cand == canonical)
            return (int)(i + 1);  // +1 because index 0 is "Built-in"
    }
    return -1;
}

// ---------------------------------------------------------------------------
// SetScenario / SaveToScenario
// ---------------------------------------------------------------------------

void NetworkConfigPanel::SetScenario(Scenario* scenario) {
    scenario_ = scenario;
    if (!scenario_) return;

    name_field_->ChangeValue(scenario_->name);

    f1_field_->ChangeValue(wxString::Format("%.4f", scenario_->frequencies.f1_hz / 1000.0));
    f2_field_->ChangeValue(wxString::Format("%.4f", scenario_->frequencies.f2_hz / 1000.0));

    prop_model_->SetSelection(
        scenario_->propagation_model == Scenario::PropagationModel::GRWAVE ? 2 :
        scenario_->propagation_model == Scenario::PropagationModel::Millington ? 1 : 0);
    airy_cache_cb_->SetValue(scenario_->precompute_airy_cache);

    lat_min_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lat_min));
    lat_max_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lat_max));
    lon_min_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lon_min));
    lon_max_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lon_max));
    res_field_->ChangeValue(wxString::Format("%.1f", scenario_->grid.resolution_km));

    // Terrain
    if (scenario_->terrain_source == Scenario::TerrainSource::Flat ||
        scenario_->terrain_file.empty()) {
        terrain_src_->SetSelection(0);
    } else {
        int idx = TerrainIndexForPath(scenario_->terrain_file);
        if (idx >= 0) {
            terrain_src_->SetSelection(idx);
        } else {
            // Not in discovered list — insert before "Other..."
            int other_idx = (int)terrain_src_->GetCount() - 1;
            std::filesystem::path p(scenario_->terrain_file);
            std::string display = p.filename().string();
            if (std::filesystem::is_directory(scenario_->terrain_file))
                display += "/";
            terrain_src_->Insert(display, other_idx);
            terrain_paths_.push_back(scenario_->terrain_file);
            terrain_src_->SetSelection(other_idx);
        }
    }
    UpdateTerrainLabel();

    // Conductivity
    if (scenario_->conductivity_source == Scenario::ConductivitySource::BuiltIn ||
        scenario_->conductivity_file.empty()) {
        cond_src_->SetSelection(0);
        cond_file_->ChangeValue("");
    } else {
        int idx = CondIndexForPath(scenario_->conductivity_file);
        if (idx >= 0) {
            cond_src_->SetSelection(idx);
            cond_file_->ChangeValue("");
        } else {
            // Select "Other..." and put the path in the file field
            cond_src_->SetSelection((int)cond_src_->GetCount() - 1);
            cond_file_->ChangeValue(scenario_->conductivity_file);
        }
    }
    UpdateCondFileState();
    UpdateCondLabel();

    UpdateOstn15Label();
    UpdateMlDisplay();
    ValidateBoundsFields();
    ValidateResField();
    UpdateResCountDisplay();
}

void NetworkConfigPanel::SaveToScenario() {
    if (!scenario_) return;

    scenario_->name = name_field_->GetValue().ToStdString();

    double f1 = wxAtof(f1_field_->GetValue());
    double f2 = wxAtof(f2_field_->GetValue());
    if (f1 >= F_MIN_KHZ && f1 <= F_MAX_KHZ) scenario_->frequencies.f1_hz = f1 * 1000.0;
    if (f2 >= F_MIN_KHZ && f2 <= F_MAX_KHZ) scenario_->frequencies.f2_hz = f2 * 1000.0;
    scenario_->frequencies.recompute();

    int prop_sel = prop_model_->GetSelection();
    scenario_->propagation_model = (prop_sel == 2) ? Scenario::PropagationModel::GRWAVE :
                                    (prop_sel == 1) ? Scenario::PropagationModel::Millington
                                                    : Scenario::PropagationModel::Homogeneous;
    scenario_->precompute_airy_cache = airy_cache_cb_->GetValue();

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

    double res = wxAtof(res_field_->GetValue());
    if (res >= RES_MIN_KM && res <= RES_MAX_KM) scenario_->grid.resolution_km = res;

    // Terrain
    int terr_sel = terrain_src_->GetSelection();
    int terr_last = (int)terrain_src_->GetCount() - 1;  // "Other..." index
    if (terr_sel == 0) {
        scenario_->terrain_source = Scenario::TerrainSource::Flat;
        scenario_->terrain_file.clear();
    } else if (terr_sel < terr_last) {
        // A discovered or user-added file
        scenario_->terrain_source = Scenario::TerrainSource::File;
        int path_idx = terr_sel - 1;  // terrain_paths_ is 0-indexed, dropdown is 1-indexed
        if (path_idx >= 0 && path_idx < (int)terrain_paths_.size())
            scenario_->terrain_file = terrain_paths_[path_idx];
    }
    // terr_sel == terr_last means "Other..." is still selected but no file was
    // picked — leave terrain unchanged

    // Conductivity
    int cond_sel = cond_src_->GetSelection();
    int cond_last = (int)cond_src_->GetCount() - 1;  // "Other..." index
    if (cond_sel == 0) {
        scenario_->conductivity_source = Scenario::ConductivitySource::BuiltIn;
        scenario_->conductivity_file.clear();
    } else if (cond_sel < cond_last) {
        // A discovered file
        auto discovered = discover_conductivity_files();
        int path_idx = cond_sel - 1;
        if (path_idx >= 0 && path_idx < (int)discovered.size()) {
            scenario_->conductivity_source = Scenario::ConductivitySource::File;
            scenario_->conductivity_file = discovered[path_idx].second;
        }
    } else {
        // "Other..." — use the file field
        scenario_->conductivity_source = Scenario::ConductivitySource::File;
        scenario_->conductivity_file = make_relative_data_path(
            cond_file_->GetValue().ToStdString());
    }
}

// ---------------------------------------------------------------------------
// Bounds from map
// ---------------------------------------------------------------------------

void NetworkConfigPanel::SetBoundsFromMap(double lat_min, double lat_max,
                                           double lon_min, double lon_max) {
    lat_min_field_->ChangeValue(wxString::Format("%.4f", lat_min));
    lat_max_field_->ChangeValue(wxString::Format("%.4f", lat_max));
    lon_min_field_->ChangeValue(wxString::Format("%.4f", lon_min));
    lon_max_field_->ChangeValue(wxString::Format("%.4f", lon_max));
    ValidateBoundsFields();
    ValidateResField();
    UpdateResCountDisplay();
}

// ---------------------------------------------------------------------------
// Event handlers — frequencies, bounds, resolution
// ---------------------------------------------------------------------------

void NetworkConfigPanel::OnFreqChanged(wxCommandEvent& /*evt*/) {
    ValidateFreqFields();
    UpdateMlDisplay();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnBoundsChanged(wxCommandEvent& /*evt*/) {
    ValidateBoundsFields();
    ValidateResField();
    UpdateResCountDisplay();
    if (IsResValid()) debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnResChanged(wxCommandEvent& /*evt*/) {
    ValidateResField();
    UpdateResCountDisplay();
    if (IsResValid()) debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnOtherChanged(wxCommandEvent& /*evt*/) {
    UpdateOstn15Label();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::FlushPending() {
    if (debounce_.IsRunning()) debounce_.Stop();
    // Always save regardless of whether the debounce was running, so that
    // callers (e.g. OnToolCompute re-enabling auto-compute) always get the
    // latest UI state even if the debounce already fired or was suppressed
    // by an IsResValid() check.
    if (scenario_) SaveToScenario();
}

void NetworkConfigPanel::OnFieldKillFocus(wxFocusEvent& evt) {
    evt.Skip();
    if (!debounce_.IsRunning()) return;
    debounce_.Stop();
    if (!scenario_) return;
    // Always save so scenario_ reflects the latest UI state, even if the
    // grid is currently too large to compute.  This ensures a manual
    // re-enable of auto-compute (FlushPending) picks up all pending changes.
    SaveToScenario();
    if (!on_changed || !IsResValid()) return;
    on_changed(*scenario_);
}

void NetworkConfigPanel::OnDebounceTimer(wxTimerEvent& /*evt*/) {
    if (!scenario_) return;
    // Always save so scenario_ is kept in sync with the UI, even when the
    // grid resolution is temporarily invalid (too many points).  Changes
    // such as frequency or propagation-model edits must not be silently
    // discarded just because the grid is oversized.
    SaveToScenario();
    if (!on_changed || !IsResValid()) return;
    on_changed(*scenario_);
}

// ---------------------------------------------------------------------------
// Event handlers — terrain & conductivity
// ---------------------------------------------------------------------------

void NetworkConfigPanel::OnTerrainSrcChanged(wxCommandEvent& /*evt*/) {
    int sel = terrain_src_->GetSelection();
    int last = (int)terrain_src_->GetCount() - 1;

    if (sel == last) {
        // "Other..." selected — open file dialog
        wxFileDialog dlg(this, "Select terrain data file", "", "",
                         "GeoTIFF (*.tif;*.tiff)|*.tif;*.tiff|"
                         "HGT (*.hgt)|*.hgt|"
                         "All files (*)|*",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
            std::string chosen = dlg.GetPath().ToStdString();
            // Check if it matches an existing entry
            int idx = TerrainIndexForPath(chosen);
            if (idx >= 0) {
                terrain_src_->SetSelection(idx);
            } else {
                // Insert before "Other..."
                std::filesystem::path p(chosen);
                terrain_src_->Insert(p.filename().string(), last);
                terrain_paths_.push_back(chosen);
                terrain_src_->SetSelection(last);  // the newly inserted item
            }
        } else {
            // User cancelled — revert to previous selection (Flat)
            terrain_src_->SetSelection(0);
        }
    }

    UpdateTerrainLabel();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnCondSrcChanged(wxCommandEvent& /*evt*/) {
    int sel = cond_src_->GetSelection();
    int last = (int)cond_src_->GetCount() - 1;

    if (sel == last) {
        // "Other..." selected — open file dialog
        wxFileDialog dlg(this, "Select conductivity data file", "", "",
                         "GeoTIFF (*.tif;*.tiff)|*.tif;*.tiff|"
                         "All files (*)|*",
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_OK) {
            cond_file_->ChangeValue(dlg.GetPath());
        } else {
            // User cancelled — revert to Built-in
            cond_src_->SetSelection(0);
        }
    }

    UpdateCondFileState();
    UpdateCondLabel();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnCondBrowse(wxCommandEvent& /*evt*/) {
    wxFileDialog dlg(this, "Select conductivity data file", "", "",
                     "GeoTIFF (*.tif;*.tiff)|*.tif;*.tiff|"
                     "All files (*)|*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        cond_file_->ChangeValue(dlg.GetPath());
        UpdateCondLabel();
        debounce_.StartOnce(500);
    }
}

void NetworkConfigPanel::OnFilePath(wxCommandEvent& /*evt*/) {
    UpdateCondLabel();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::UpdateCondFileState() {
    int sel = cond_src_->GetSelection();
    int last = (int)cond_src_->GetCount() - 1;
    bool other_mode = (sel == last);
    cond_file_->Enable(other_mode);
    cond_browse_->Enable(other_mode);
}

// ---------------------------------------------------------------------------
// Validation helpers
// ---------------------------------------------------------------------------

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

bool NetworkConfigPanel::IsResValid() const {
    double res_km  = wxAtof(res_field_->GetValue());
    double lat_min = wxAtof(lat_min_field_->GetValue());
    double lat_max = wxAtof(lat_max_field_->GetValue());
    double lon_min = wxAtof(lon_min_field_->GetValue());
    double lon_max = wxAtof(lon_max_field_->GetValue());
    if (res_km < RES_MIN_KM || res_km > RES_MAX_KM) return false;
    if (lat_min >= lat_max || lon_min >= lon_max)    return false;
    double mid_lat = (lat_min + lat_max) / 2.0;
    constexpr double DEG_PER_KM_LAT = 1.0 / 110.574;
    double deg_per_km_lon = 1.0 / (111.320 * std::cos(mid_lat * M_PI / 180.0));
    int rows = std::max(1, (int)((lat_max - lat_min) / (res_km * DEG_PER_KM_LAT)) + 1);
    int cols = std::max(1, (int)((lon_max - lon_min) / (res_km * deg_per_km_lon)) + 1);
    return (rows * cols <= MAX_GRID_PTS);
}

void NetworkConfigPanel::ValidateResField() {
    bool ok = IsResValid();
    res_field_->SetBackgroundColour(ok ? wxNullColour : *wxRED);
    res_field_->Refresh();
}

void NetworkConfigPanel::UpdateResCountDisplay() {
    double res_km  = wxAtof(res_field_->GetValue());
    double lat_min = wxAtof(lat_min_field_->GetValue());
    double lat_max = wxAtof(lat_max_field_->GetValue());
    double lon_min = wxAtof(lon_min_field_->GetValue());
    double lon_max = wxAtof(lon_max_field_->GetValue());
    if (res_km < RES_MIN_KM || res_km > RES_MAX_KM || lat_min >= lat_max || lon_min >= lon_max) {
        res_count_label_->SetLabel("");
        return;
    }
    double mid_lat = (lat_min + lat_max) / 2.0;
    constexpr double DEG_PER_KM_LAT = 1.0 / 110.574;
    double deg_per_km_lon = 1.0 / (111.320 * std::cos(mid_lat * M_PI / 180.0));
    int rows = std::max(1, (int)((lat_max - lat_min) / (res_km * DEG_PER_KM_LAT)) + 1);
    int cols = std::max(1, (int)((lon_max - lon_min) / (res_km * deg_per_km_lon)) + 1);
    int total = rows * cols;
    wxString label;
    if (total > MAX_GRID_PTS)
        label = wxString::Format("~%dk points (too large)", total / 1000);
    else if (total >= 1000000)
        label = wxString::Format("~%.1fM points", total / 1000000.0);
    else if (total >= 1000)
        label = wxString::Format("~%dk points", total / 1000);
    else
        label = wxString::Format("~%d points", total);
    res_count_label_->SetLabel(label);
}

void NetworkConfigPanel::UpdateTerrainLabel() {
    if (!terrain_status_label_) return;
    int sel = terrain_src_->GetSelection();
    wxString msg;
    wxColour col = *wxBLACK;
    if (sel == 0) {
        // Flat
    } else {
        int path_idx = sel - 1;
        if (path_idx >= 0 && path_idx < (int)terrain_paths_.size()) {
            const auto& path = terrain_paths_[path_idx];
            if (std::filesystem::is_directory(path))
                msg = "SRTM tile directory";
            else
                msg = wxString(path.c_str());
        }
    }
    terrain_status_label_->SetLabel(msg);
    terrain_status_label_->SetForegroundColour(col);
    terrain_status_label_->GetParent()->Layout();
}

void NetworkConfigPanel::UpdateCondLabel() {
    if (!cond_status_label_) return;
    int sel = cond_src_->GetSelection();
    int last = (int)cond_src_->GetCount() - 1;
    wxString msg;
    wxColour col = *wxBLACK;

    if (sel == 0) {
        // Built-in
    } else if (sel == last) {
        // "Other..." — check the file field
        wxString path = cond_file_->GetValue();
        if (path.empty()) {
            msg = "No file set - using built-in fallback";
            col = *wxRED;
        } else if (!wxFileName::FileExists(path)) {
            msg = "File not found - using built-in fallback";
            col = *wxRED;
        } else {
            msg = wxString(path.c_str());
        }
    } else {
        // A discovered file — show its path
        auto discovered = discover_conductivity_files();
        int path_idx = sel - 1;
        if (path_idx >= 0 && path_idx < (int)discovered.size())
            msg = wxString(discovered[path_idx].second.c_str());
    }
    cond_status_label_->SetLabel(msg);
    cond_status_label_->SetForegroundColour(col);
    cond_status_label_->GetParent()->Layout();
}

void NetworkConfigPanel::UpdateOstn15Label() {
    if (!ostn15_label_) return;
    if (osgb::ostn15_loaded()) {
        ostn15_label_->SetLabel("OSTN15 (+/-0.1 m)");
        ostn15_label_->SetForegroundColour(*wxBLACK);
    } else {
        ostn15_label_->SetLabel("Helmert (+/-5 m) - run ostn15_download.py for better accuracy");
        ostn15_label_->SetForegroundColour(*wxRED);
    }
    ostn15_label_->GetParent()->Layout();
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
