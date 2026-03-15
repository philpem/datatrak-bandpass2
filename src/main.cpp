#include <wx/app.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include "ui/MainFrame.h"
#include "coords/Osgb.h"

namespace bp {

// Try to load the OSTN15 binary grid from standard locations.
// Search order:
//   1. Executable directory
//   2. data/ subdirectory next to executable
//   3. <user data dir>/OSTN15.dat
//   4. Walk up from executable directory looking for data/OSTN15.dat
//      (handles build trees like build/src/bandpass2 → ../../data/)
static void try_load_ostn15() {
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.SetFullName("OSTN15.dat");

    wxFileName exe_data(wxStandardPaths::Get().GetExecutablePath());
    exe_data.AppendDir("data");
    exe_data.SetFullName("OSTN15.dat");

    wxFileName user_data(wxStandardPaths::Get().GetUserDataDir(), "OSTN15.dat");

    std::vector<wxFileName> candidates = {exe, exe_data, user_data};

    // Walk up from executable directory (up to 4 levels) to find data/OSTN15.dat
    // in the source tree.  Handles build/bandpass2, build/src/bandpass2, etc.
    wxFileName walk(wxStandardPaths::Get().GetExecutablePath());
    for (int i = 0; i < 4 && walk.GetDirCount() > 0; ++i) {
        walk.RemoveLastDir();
        wxFileName dev(walk);
        dev.AppendDir("data");
        dev.SetFullName("OSTN15.dat");
        candidates.push_back(dev);
    }

    for (const wxFileName& candidate : candidates) {
        if (candidate.FileExists()) {
            if (osgb::load_ostn15(candidate.GetFullPath().ToStdString()))
                return;
            wxLogWarning("OSTN15 file found at %s but failed to load",
                         candidate.GetFullPath().c_str());
        }
    }
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
