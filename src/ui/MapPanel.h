#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/webview.h>

namespace bp {

class MapPanel : public wxPanel {
public:
    explicit MapPanel(wxWindow* parent, uint16_t tile_port = 0);

    // C++ → JavaScript
    void AddTransmitterMarker(int id, double lat, double lon,
                               const std::string& name, bool locked = false);
    void MoveTransmitterMarker(int id, double lat, double lon);
    void RemoveTransmitterMarker(int id);
    void LockTransmitter(int id, bool locked);
    void SetReceiverMarker(double lat, double lon, bool locked = false);
    void RemoveReceiverMarker();
    void LockReceiver(bool locked);
    void UpdateLayer(const std::string& layer_name, const std::string& geojson);
    void ClearLayer(const std::string& layer_name);
    void SetPlacementMode(bool enabled);
    void SetReceiverPlacementMode(bool enabled);

    // JS → C++ callbacks (set by MainFrame)
    std::function<void(double lat, double lon)>           on_map_click;
    std::function<void(int id, double lat, double lon)>   on_transmitter_moved;
    std::function<void(int id)>                           on_transmitter_selected;
    std::function<void(double lat, double lon)>           on_receiver_placed;
    std::function<void(double lat, double lon)>           on_receiver_moved;
    std::function<void(double lat, double lon)>           on_cursor_moved;
    std::function<void()>                                  on_map_ready;

private:
    void OnWebViewLoad(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);
    void RunScript(const std::string& js);
    std::string GetMapHtmlPath() const;

    wxWebView*           webview_    = nullptr;
    bool                 loaded_     = false;
    std::vector<std::string> pending_scripts_;
    uint16_t             tile_port_  = 0;
};

} // namespace bp
