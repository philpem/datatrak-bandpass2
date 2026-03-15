#include <wx/app.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <filesystem>
#include "ui/MainFrame.h"
#include "coords/Osgb.h"
#include "model/DataPaths.h"

namespace bp {

// Build the data search directory list from wxWidgets platform paths.
// Called once at startup; after this, DataPaths functions are pure C++.
static void setup_data_dirs() {
    std::vector<std::string> dirs;

    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());

    // 1. Executable directory
    dirs.push_back(exe.GetPath().ToStdString());

    // 2. data/ subdirectory next to executable
    {
        wxFileName exe_data(exe);
        exe_data.AppendDir("data");
        dirs.push_back(exe_data.GetPath().ToStdString());
    }

    // 3. Platform user-data directory
    dirs.push_back(
        wxStandardPaths::Get().GetUserDataDir().ToStdString());

    // 4. Walk up from executable (up to 4 levels) looking for data/
    {
        wxFileName walk(wxStandardPaths::Get().GetExecutablePath());
        for (int i = 0; i < 4 && walk.GetDirCount() > 0; ++i) {
            walk.RemoveLastDir();
            wxFileName dev(walk);
            dev.AppendDir("data");
            dirs.push_back(dev.GetPath().ToStdString());
        }
    }

    init_data_search_dirs(std::move(dirs));
}

// Try to load the OSTN15 binary grid from standard data directories.
static void try_load_ostn15() {
    std::string path = resolve_data_path("OSTN15.dat");

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
        setup_data_dirs();
        try_load_ostn15();
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

} // namespace bp

wxIMPLEMENT_APP(bp::BandpassApp);
