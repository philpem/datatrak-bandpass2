#include <wx/app.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include "ui/MainFrame.h"
#include "coords/Osgb.h"

namespace bp {

// Try to load the OSTN15 binary grid from standard locations.
// Search order:
//   1. Executable directory (most common deployment)
//   2. <user data dir>/OSTN15.dat (user-installed)
//   3. data/ subdirectory relative to executable (development tree)
static void try_load_ostn15() {
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.SetFullName("OSTN15.dat");

    wxFileName user_data(wxStandardPaths::Get().GetUserDataDir(), "OSTN15.dat");

    wxFileName dev_data(wxStandardPaths::Get().GetExecutablePath());
    dev_data.RemoveLastDir();
    dev_data.AppendDir("data");
    dev_data.SetFullName("OSTN15.dat");

    for (const wxFileName& candidate : {exe, user_data, dev_data}) {
        if (candidate.FileExists()) {
            osgb::load_ostn15(candidate.GetFullPath().ToStdString());
            return;
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
