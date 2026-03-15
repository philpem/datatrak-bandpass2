#include "NetworkConfigPanel.h"
#include "../coords/Osgb.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <cmath>

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

NetworkConfigPanel::NetworkConfigPanel(wxWindow* parent)
    : wxScrolledWindow(parent)
{
    debounce_.Bind(wxEVT_TIMER, &NetworkConfigPanel::OnDebounceTimer, this);

    auto* outer = new wxBoxSizer(wxVERTICAL);

    // ── Scenario ────────────────────────────────────────────────────────────
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

    // ── Frequencies ─────────────────────────────────────────────────────────
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

    // ── Grid ────────────────────────────────────────────────────────────────
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
        lat_max_field_->SetValue("61.0");
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

    // ── Terrain ─────────────────────────────────────────────────────────────
    {
        auto* box   = new wxStaticBox(this, wxID_ANY, "Terrain");
        auto* bsiz  = new wxStaticBoxSizer(box, wxVERTICAL);
        auto* gs    = new wxFlexGridSizer(2, 4, 6);
        gs->AddGrowableCol(1);

        gs->Add(new wxStaticText(this, wxID_ANY, "Source"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        wxArrayString terr_opts;
        terr_opts.Add("Flat"); terr_opts.Add("SRTM"); terr_opts.Add("File");
        terrain_src_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, terr_opts);
        terrain_src_->SetSelection(0);
        terrain_src_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnTerrainSrcChanged, this);
        gs->Add(terrain_src_, 1, wxEXPAND | wxBOTTOM, 2);

        // File path row
        gs->Add(new wxStaticText(this, wxID_ANY, "File"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            terrain_file_ = new wxTextCtrl(this, wxID_ANY);
            terrain_file_->Bind(wxEVT_TEXT, &NetworkConfigPanel::OnFilePath, this);
            terrain_browse_ = new wxButton(this, wxID_ANY, "Browse...",
                                            wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            terrain_browse_->Bind(wxEVT_BUTTON, &NetworkConfigPanel::OnTerrainBrowse, this);
            row->Add(terrain_file_,   1, wxEXPAND | wxRIGHT, 4);
            row->Add(terrain_browse_, 0, wxALIGN_CENTER_VERTICAL);
            gs->Add(row, 1, wxEXPAND | wxBOTTOM, 2);
        }

        // Status
        gs->Add(new wxStaticText(this, wxID_ANY, ""), 0);
        terrain_status_label_ = new wxStaticText(this, wxID_ANY, "");
        gs->Add(terrain_status_label_, 0, wxBOTTOM, 2);

        bsiz->Add(gs, 0, wxEXPAND | wxALL, 4);
        outer->Add(bsiz, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }

    // ── Conductivity ────────────────────────────────────────────────────────
    {
        auto* box   = new wxStaticBox(this, wxID_ANY, "Conductivity");
        auto* bsiz  = new wxStaticBoxSizer(box, wxVERTICAL);
        auto* gs    = new wxFlexGridSizer(2, 4, 6);
        gs->AddGrowableCol(1);

        gs->Add(new wxStaticText(this, wxID_ANY, "Source"),
                0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        wxArrayString cond_opts;
        cond_opts.Add("Built-in"); cond_opts.Add("ITU P.832");
        cond_opts.Add("BGS"); cond_opts.Add("File");
        cond_src_ = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, cond_opts);
        cond_src_->SetSelection(0);
        cond_src_->Bind(wxEVT_CHOICE, &NetworkConfigPanel::OnCondSrcChanged, this);
        gs->Add(cond_src_, 1, wxEXPAND | wxBOTTOM, 2);

        // File path row
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

    SetSizer(outer);
    SetScrollRate(0, 10);
    FitInside();

    UpdateTerrainFileState();
    UpdateCondFileState();
    UpdateTerrainLabel();
    UpdateCondLabel();
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

    lat_min_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lat_min));
    lat_max_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lat_max));
    lon_min_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lon_min));
    lon_max_field_->ChangeValue(wxString::Format("%.4f", scenario_->grid.lon_max));
    res_field_->ChangeValue(wxString::Format("%.1f", scenario_->grid.resolution_km));

    // Terrain
    int terr_sel = 0;
    if (scenario_->terrain_source == Scenario::TerrainSource::SRTM)  terr_sel = 1;
    else if (scenario_->terrain_source == Scenario::TerrainSource::File) terr_sel = 2;
    terrain_src_->SetSelection(terr_sel);
    terrain_file_->ChangeValue(scenario_->terrain_file);
    UpdateTerrainFileState();
    UpdateTerrainLabel();

    // Conductivity
    int cond_sel = 0;
    if (scenario_->conductivity_source == Scenario::ConductivitySource::ItuP832) cond_sel = 1;
    else if (scenario_->conductivity_source == Scenario::ConductivitySource::BGS) cond_sel = 2;
    else if (scenario_->conductivity_source == Scenario::ConductivitySource::File) cond_sel = 3;
    cond_src_->SetSelection(cond_sel);
    cond_file_->ChangeValue(scenario_->conductivity_file);
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
    if (terr_sel == 0)      scenario_->terrain_source = Scenario::TerrainSource::Flat;
    else if (terr_sel == 1) scenario_->terrain_source = Scenario::TerrainSource::SRTM;
    else {
        scenario_->terrain_source = Scenario::TerrainSource::File;
        scenario_->terrain_file = terrain_file_->GetValue().ToStdString();
    }

    // Conductivity
    int cond_sel = cond_src_->GetSelection();
    if (cond_sel == 0)      scenario_->conductivity_source = Scenario::ConductivitySource::BuiltIn;
    else if (cond_sel == 1) scenario_->conductivity_source = Scenario::ConductivitySource::ItuP832;
    else if (cond_sel == 2) scenario_->conductivity_source = Scenario::ConductivitySource::BGS;
    else {
        scenario_->conductivity_source = Scenario::ConductivitySource::File;
        scenario_->conductivity_file = cond_file_->GetValue().ToStdString();
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
    if (!debounce_.IsRunning()) return;
    debounce_.Stop();
    SaveToScenario();
}

void NetworkConfigPanel::OnFieldKillFocus(wxFocusEvent& evt) {
    evt.Skip();
    if (!debounce_.IsRunning()) return;
    debounce_.Stop();
    if (!scenario_ || !on_changed) return;
    if (!IsResValid()) return;  // don't trigger compute when over point limit
    SaveToScenario();
    on_changed(*scenario_);
}

void NetworkConfigPanel::OnDebounceTimer(wxTimerEvent& /*evt*/) {
    if (!scenario_ || !on_changed) return;
    if (!IsResValid()) return;  // guard: bounds changed after timer was started
    SaveToScenario();
    on_changed(*scenario_);
}

// ---------------------------------------------------------------------------
// Event handlers — terrain & conductivity
// ---------------------------------------------------------------------------

void NetworkConfigPanel::OnTerrainSrcChanged(wxCommandEvent& /*evt*/) {
    UpdateTerrainFileState();
    UpdateTerrainLabel();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnCondSrcChanged(wxCommandEvent& /*evt*/) {
    UpdateCondFileState();
    UpdateCondLabel();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::OnTerrainBrowse(wxCommandEvent& /*evt*/) {
    wxFileDialog dlg(this, "Select terrain data file", "", "",
                     "GeoTIFF (*.tif;*.tiff)|*.tif;*.tiff|"
                     "HGT (*.hgt)|*.hgt|"
                     "All files (*)|*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        terrain_file_->ChangeValue(dlg.GetPath());
        UpdateTerrainLabel();
        debounce_.StartOnce(500);
    }
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
    UpdateTerrainLabel();
    UpdateCondLabel();
    debounce_.StartOnce(500);
}

void NetworkConfigPanel::UpdateTerrainFileState() {
    bool file_mode = (terrain_src_->GetSelection() == 2);
    terrain_file_->Enable(file_mode);
    terrain_browse_->Enable(file_mode);
}

void NetworkConfigPanel::UpdateCondFileState() {
    bool file_mode = (cond_src_->GetSelection() == 3);
    cond_file_->Enable(file_mode);
    cond_browse_->Enable(file_mode);
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
        // Flat — always available, nothing to warn about
        msg = "";
    } else {
        wxString path = terrain_file_->GetValue();
        if (path.empty()) {
            msg = "No path set - using flat fallback";
            col = *wxRED;
        } else if (sel == 1) {
            // SRTM — expects a directory
            if (!wxFileName::DirExists(path)) {
                msg = "Directory not found - using flat fallback";
                col = *wxRED;
            } else {
                msg = "Directory OK";
            }
        } else {
            // File
            if (!wxFileName::FileExists(path)) {
                msg = "File not found - using flat fallback";
                col = *wxRED;
            } else {
                msg = "File OK";
            }
        }
    }
    terrain_status_label_->SetLabel(msg);
    terrain_status_label_->SetForegroundColour(col);
    terrain_status_label_->GetParent()->Layout();
}

void NetworkConfigPanel::UpdateCondLabel() {
    if (!cond_status_label_) return;
    int sel = cond_src_->GetSelection();
    wxString msg;
    wxColour col = *wxBLACK;
    if (sel == 0) {
        // Built-in — always available
        msg = "";
    } else {
        wxString path = cond_file_->GetValue();
        if (path.empty()) {
            msg = "No file set - using built-in fallback";
            col = *wxRED;
        } else if (!wxFileName::FileExists(path)) {
            msg = "File not found - using built-in fallback";
            col = *wxRED;
        } else {
            msg = "File OK";
        }
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
