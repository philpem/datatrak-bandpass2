#include <wx/app.h>
#include "ui/MainFrame.h"

namespace bp {

class BandpassApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) return false;
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

} // namespace bp

wxIMPLEMENT_APP(bp::BandpassApp);
