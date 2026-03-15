#include <wx/app.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include "ui/MainFrame.h"
#include "coords/Osgb.h"

namespace bp {

// Try to load the OSTN15 binary grid from standard locations.
// Search order:
//   1. Executable directory (most common deployment)
//   2. data/ subdirectory next to executable
//   3. <user data dir>/OSTN15.dat (user-installed)
//   4. data/ subdirectory in parent of executable dir (development tree)
static void try_load_ostn15() {
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.SetFullName("OSTN15.dat");

    wxFileName exe_data(wxStandardPaths::Get().GetExecutablePath());
    exe_data.AppendDir("data");
    exe_data.SetFullName("OSTN15.dat");

    wxFileName user_data(wxStandardPaths::Get().GetUserDataDir(), "OSTN15.dat");

    wxFileName dev_data(wxStandardPaths::Get().GetExecutablePath());
    dev_data.RemoveLastDir();
    dev_data.AppendDir("data");
    dev_data.SetFullName("OSTN15.dat");

    for (const wxFileName& candidate : {exe, exe_data, user_data, dev_data}) {
        if (candidate.FileExists()) {
            if (osgb::load_ostn15(candidate.GetFullPath().ToStdString())) {
                wxLogMessage("OSTN15 grid loaded from %s",
                             candidate.GetFullPath().c_str());
                return;
            }
            wxLogWarning("OSTN15 file found at %s but failed to load",
                         candidate.GetFullPath().c_str());
        }
    }
    wxLogMessage("OSTN15 grid not found; using Helmert (±5 m) datum transform");
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
