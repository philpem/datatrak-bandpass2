#include "MapPanel.h"
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <nlohmann/json.hpp>
#include <sstream>

namespace bp {

MapPanel::MapPanel(wxWindow* parent, uint16_t tile_port)
    : wxPanel(parent)
    , tile_port_(tile_port)
{
    webview_ = wxWebView::New(this, wxID_ANY);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(webview_, 1, wxEXPAND);
    SetSizer(sizer);

    webview_->AddScriptMessageHandler("bandpass");
    webview_->Bind(wxEVT_WEBVIEW_LOADED,          &MapPanel::OnWebViewLoad,    this);
    webview_->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &MapPanel::OnScriptMessage, this);

    // Load the map HTML
    std::string html_path = GetMapHtmlPath();
    if (!html_path.empty()) {
        wxString url = wxString::Format("file://%s?port=%d", html_path, (int)tile_port_);
        webview_->LoadURL(url);
    }
}

std::string MapPanel::GetMapHtmlPath() const {
    // Look for map.html relative to the executable
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    wxFileName candidate;

    // Try: <exe_dir>/web/map.html
    candidate = wxFileName(exe.GetPath(), "map.html");
    candidate.PrependDir("web");
    if (candidate.FileExists()) return candidate.GetFullPath().ToStdString();

    // Try: <exe_dir>/../share/bandpass2/web/map.html (installed)
    candidate = wxFileName(exe.GetPath());
    candidate.AppendDir("..");
    candidate.AppendDir("share");
    candidate.AppendDir("bandpass2");
    candidate.AppendDir("web");
    candidate.SetFullName("map.html");
    candidate.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    if (candidate.FileExists()) return candidate.GetFullPath().ToStdString();

    // Try: <build_dir>/src/web/map.html (during development)
    candidate = wxFileName(exe.GetPath());
    candidate.AppendDir("..");
    candidate.AppendDir("src");
    candidate.AppendDir("web");
    candidate.SetFullName("map.html");
    candidate.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    if (candidate.FileExists()) return candidate.GetFullPath().ToStdString();

    return "";
}

void MapPanel::OnWebViewLoad(wxWebViewEvent& /*evt*/) {
    loaded_ = true;
    for (const auto& js : pending_scripts_) {
        webview_->RunScript(js);
    }
    pending_scripts_.clear();
}

void MapPanel::OnScriptMessage(wxWebViewEvent& evt) {
    std::string msg = evt.GetString().ToStdString();
    try {
        auto j = nlohmann::json::parse(msg);
        std::string type = j.value("type", "");

        if (type == "ready" && on_map_ready) {
            on_map_ready();
        } else if (type == "transmitter_placed" && on_map_click) {
            on_map_click(j["lat"].get<double>(), j["lon"].get<double>());
        } else if (type == "transmitter_moved" && on_transmitter_moved) {
            on_transmitter_moved(j["id"].get<int>(),
                                 j["lat"].get<double>(),
                                 j["lon"].get<double>());
        } else if (type == "transmitter_selected" && on_transmitter_selected) {
            on_transmitter_selected(j["id"].get<int>());
        } else if (type == "receiver_placed" && on_receiver_placed) {
            on_receiver_placed(j["lat"].get<double>(), j["lon"].get<double>());
        } else if (type == "receiver_moved" && on_receiver_moved) {
            on_receiver_moved(j["lat"].get<double>(), j["lon"].get<double>());
        } else if (type == "cursor_moved" && on_cursor_moved) {
            on_cursor_moved(j["lat"].get<double>(), j["lon"].get<double>());
        }
    } catch (...) {
        // Silently ignore malformed messages
    }
}

void MapPanel::RunScript(const std::string& js) {
    if (loaded_) {
        webview_->RunScript(js);
    } else {
        pending_scripts_.push_back(js);
    }
}

void MapPanel::AddTransmitterMarker(int id, double lat, double lon,
                                     const std::string& name, bool locked) {
    std::string safe_name = name;
    for (auto& c : safe_name) if (c == '\'' || c == '\\') c = '_';
    RunScript(wxString::Format("addTransmitter(%d, %f, %f, '%s', %s);",
                               id, lat, lon, safe_name,
                               locked ? "true" : "false").ToStdString());
}

void MapPanel::MoveTransmitterMarker(int id, double lat, double lon) {
    RunScript(wxString::Format("moveTransmitter(%d, %f, %f);", id, lat, lon).ToStdString());
}

void MapPanel::RemoveTransmitterMarker(int id) {
    RunScript(wxString::Format("removeTransmitter(%d);", id).ToStdString());
}

void MapPanel::LockTransmitter(int id, bool locked) {
    RunScript(wxString::Format("lockTransmitter(%d, %s);",
                               id, locked ? "true" : "false").ToStdString());
}

void MapPanel::SetReceiverMarker(double lat, double lon, bool locked) {
    RunScript(wxString::Format("setReceiver(%f, %f, %s);",
                               lat, lon, locked ? "true" : "false").ToStdString());
}

void MapPanel::RemoveReceiverMarker() {
    RunScript("removeReceiver();");
}

void MapPanel::LockReceiver(bool locked) {
    RunScript(wxString::Format("lockReceiver(%s);", locked ? "true" : "false").ToStdString());
}

void MapPanel::UpdateLayer(const std::string& layer_name, const std::string& geojson) {
    std::ostringstream js;
    js << "updateLayer('" << layer_name << "', " << geojson << ");";
    RunScript(js.str());
}

void MapPanel::ClearLayer(const std::string& layer_name) {
    RunScript(wxString::Format("clearLayer('%s');", layer_name).ToStdString());
}

void MapPanel::UpdateLegend(const std::string& name, double vmin, double vmax,
                             const std::string& units) {
    // Escape single-quotes just in case
    auto escape = [](std::string s) {
        for (auto& c : s) if (c == '\'' || c == '\\') c = '_';
        return s;
    };
    RunScript(wxString::Format("updateLegend('%s', %g, %g, '%s');",
                               escape(name).c_str(), vmin, vmax,
                               escape(units).c_str()).ToStdString());
}

void MapPanel::ClearLegend() {
    RunScript("clearLegend();");
}

void MapPanel::SetPlacementMode(bool enabled) {
    RunScript(wxString::Format("setPlacementMode(%s);", enabled ? "true" : "false").ToStdString());
}

void MapPanel::SetReceiverPlacementMode(bool enabled) {
    RunScript(wxString::Format("setReceiverPlacementMode(%s);",
                               enabled ? "true" : "false").ToStdString());
}

} // namespace bp
