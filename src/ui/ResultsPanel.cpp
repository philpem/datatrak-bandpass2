#include "ResultsPanel.h"
#include "../engine/groundwave.h"
#include "../engine/noise.h"
#include "../model/Scenario.h"
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/settings.h>
#include <cmath>
#include <algorithm>
#include <string>

namespace bp {

// Eight distinguishable colours for up to 8 transmitter curves.
static const wxColour TX_COLOURS[] = {
    wxColour(210, 50,  50),   // red
    wxColour(50,  100, 210),  // blue
    wxColour(40,  160, 50),   // green
    wxColour(210, 130, 30),   // orange
    wxColour(140, 50,  190),  // purple
    wxColour(30,  170, 190),  // cyan
    wxColour(190, 50,  140),  // magenta
    wxColour(130, 90,  40),   // brown
};
static const int N_TX_COLOURS = (int)(sizeof(TX_COLOURS) / sizeof(TX_COLOURS[0]));

ResultsPanel::ResultsPanel(wxWindow* parent)
    : wxPanel(parent)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &ResultsPanel::OnPaint, this);
    Bind(wxEVT_SIZE,  &ResultsPanel::OnSize,  this);
}

void ResultsPanel::SetScenario(const Scenario* s) {
    scenario_ = s;
    Refresh();
}

void ResultsPanel::OnSize(wxSizeEvent& evt) {
    Refresh();
    evt.Skip();
}

void ResultsPanel::OnPaint(wxPaintEvent& /*evt*/) {
    wxAutoBufferedPaintDC dc(this);
    DrawPlot(dc);
}

void ResultsPanel::DrawPlot(wxDC& dc) {
    wxSize sz = GetClientSize();
    int W = sz.GetWidth(), H = sz.GetHeight();

    dc.SetBackground(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)));
    dc.Clear();

    if (W < 160 || H < 80) return;

    if (!scenario_ || scenario_->transmitters.empty()) {
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        wxFont f(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
        dc.SetFont(f);
        dc.DrawText("Place transmitters to see field-strength vs range plot",
                    20, H / 2 - 8);
        return;
    }

    // ---- Layout ----
    const int ML = 52;   // left (Y axis labels)
    const int MR = 118;  // right (legend)
    const int MT = 24;   // top (title)
    const int MB = 40;   // bottom (X labels)
    int PW = W - ML - MR;
    int PH = H - MT - MB;
    if (PW < 40 || PH < 40) return;

    // Axis ranges
    double x_max = scenario_->receiver.max_range_km;
    if (x_max <= 0.0) x_max = 350.0;
    const double y_min = 0.0;
    const double y_max = 100.0;

    // Data → pixel helpers
    auto px = [&](double x) -> int {
        return ML + (int)((x / x_max) * PW);
    };
    auto py = [&](double y) -> int {
        double c = std::max(y_min, std::min(y_max, y));
        return MT + PH - (int)(((c - y_min) / (y_max - y_min)) * PH);
    };

    // ---- Plot background + grid ----
    dc.SetBrush(*wxWHITE_BRUSH);
    dc.SetPen(wxPen(wxColour(240, 240, 240)));
    dc.DrawRectangle(ML, MT, PW, PH);

    wxFont smallFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(smallFont);
    dc.SetTextForeground(wxColour(80, 80, 80));

    // Horizontal grid lines + Y labels every 10 dB
    for (int db = 0; db <= (int)y_max; db += 10) {
        int y = py(db);
        dc.SetPen(wxPen(wxColour(220, 220, 220)));
        dc.DrawLine(ML, y, ML + PW, y);
        wxString lbl = wxString::Format("%d", db);
        wxSize ts = dc.GetTextExtent(lbl);
        dc.SetPen(*wxBLACK_PEN);
        dc.DrawText(lbl, ML - ts.GetWidth() - 5, y - ts.GetHeight() / 2);
    }

    // Vertical grid lines + X labels
    int km_step = 50;
    if (x_max > 800.0)       km_step = 200;
    else if (x_max > 400.0)  km_step = 100;
    for (int km = 0; km <= (int)x_max; km += km_step) {
        int x = px((double)km);
        dc.SetPen(wxPen(wxColour(220, 220, 220)));
        dc.DrawLine(x, MT, x, MT + PH);
        wxString lbl = wxString::Format("%d", km);
        wxSize ts = dc.GetTextExtent(lbl);
        dc.SetPen(*wxBLACK_PEN);
        dc.DrawText(lbl, x - ts.GetWidth() / 2, MT + PH + 5);
    }

    // ---- Noise reference lines ----
    // Restrict drawing to the plot region
    dc.SetClippingRegion(ML, MT, PW, PH);

    double atm_n  = atm_noise_dbuvm(scenario_->frequencies.f1_hz);
    double nfloor = scenario_->receiver.noise_floor_dbuvpm;
    double vnoise = scenario_->receiver.vehicle_noise_dbuvpm;

    struct NoiseRef { double val; wxColour col; wxString label; };
    const NoiseRef noise_refs[] = {
        { atm_n,  wxColour( 80,  80, 200), "Atm noise"    },
        { nfloor, wxColour(180,  50,  50), "Noise floor"  },
        { vnoise, wxColour(200, 120,  40), "Vehicle noise"},
    };
    for (const auto& ref : noise_refs) {
        if (ref.val >= y_min && ref.val <= y_max) {
            dc.SetPen(wxPen(ref.col, 1, wxPENSTYLE_SHORT_DASH));
            dc.DrawLine(ML, py(ref.val), ML + PW, py(ref.val));
        }
    }

    // ---- Transmitter field-strength curves (F1) ----
    GroundConstants gc { 0.005, 15.0 };  // typical land
    const int N_SAMPLES = 300;
    double step_km = x_max / N_SAMPLES;

    struct LegendEntry { wxString label; wxColour col; bool dashed; };
    std::vector<LegendEntry> legend;

    for (int ti = 0; ti < (int)scenario_->transmitters.size(); ++ti) {
        const auto& tx = scenario_->transmitters[ti];
        if (tx.power_w <= 0.0) continue;

        wxColour col = TX_COLOURS[ti % N_TX_COLOURS];
        dc.SetPen(wxPen(col, 2));

        int prev_x = -1, prev_y = -1;
        for (int si = 1; si <= N_SAMPLES; ++si) {
            double d_km = si * step_km;
            double E = groundwave_field_dbuvm(
                scenario_->frequencies.f1_hz, d_km, gc, tx.power_w);
            int xi = px(d_km);
            int yi = py(E);
            if (prev_x >= 0)
                dc.DrawLine(prev_x, prev_y, xi, yi);
            prev_x = xi;
            prev_y = yi;
        }

        wxString name = tx.name.empty()
            ? wxString::Format("TX-%d (slot %d)", ti + 1, tx.slot)
            : wxString::FromUTF8(tx.name);
        legend.push_back({ name, col, false });
    }

    dc.DestroyClippingRegion();

    // ---- Legend (right margin) ----
    for (const auto& ref : noise_refs)
        legend.push_back({ ref.label, ref.col, true });

    int lx = ML + PW + 6;
    int ly = MT + 6;
    wxFont lgFont(7, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(lgFont);
    for (const auto& e : legend) {
        if (ly + 14 > MT + PH + MB - 4) break;
        wxPenStyle sty = e.dashed ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID;
        dc.SetPen(wxPen(e.col, 2, sty));
        dc.DrawLine(lx, ly + 6, lx + 18, ly + 6);
        dc.SetTextForeground(wxColour(30, 30, 30));
        dc.DrawText(e.label, lx + 22, ly);
        ly += 14;
    }

    // ---- Axis labels ----
    wxFont labelFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(labelFont);
    dc.SetTextForeground(wxColour(40, 40, 40));
    dc.DrawText("Range (km)", ML + PW / 2 - 28, MT + PH + 24);
    dc.DrawRotatedText("Field strength (dBuV/m)", 12, MT + PH / 2 + 70, 90.0);

    // ---- Title ----
    wxFont titleFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    dc.SetFont(titleFont);
    dc.SetTextForeground(wxColour(20, 20, 20));
    dc.DrawText("Groundwave field strength vs range  (F1, land \xcf\x83=0.005 S/m)", ML, 5);

    // ---- Border on top of everything ----
    dc.SetPen(*wxBLACK_PEN);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(ML, MT, PW, PH);
}

} // namespace bp
