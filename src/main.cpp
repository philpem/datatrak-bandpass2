#include <wx/app.h>
#include <wx/log.h>
#include <filesystem>
#include "ui/MainFrame.h"
#include "coords/Osgb.h"
#include "model/DataPaths.h"

namespace bp {

// Try to load the OSTN15 binary grid from standard data directories.
static void try_load_ostn15() {
    std::string path = resolve_data_path("OSTN15.dat");

    // resolve_data_path returns the original string if not found;
    // check whether it actually resolved to an existing file.
    if (!std::filesystem::exists(path))
        return;  // no OSTN15 available — Helmert fallback

    if (!osgb::load_ostn15(path))
        wxLogWarning("OSTN15 file found at %s but failed to load",
                     path.c_str());
}

class BandpassApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) return false;
        try_load_ostn15();
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

} // namespace bp

wxIMPLEMENT_APP(bp::BandpassApp);
