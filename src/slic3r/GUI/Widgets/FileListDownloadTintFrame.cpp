
#include "FileListDownloadTintFrame.hpp"
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

namespace Slic3r::GUI {

FileListDownloadTintPanel::FileListDownloadTintPanel(wxWindow *parent, const wxColour &rgb, int alpha)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
    m_fill = wxColour(rgb.Red(), rgb.Green(), rgb.Blue(), alpha);
    Bind(wxEVT_PAINT, &FileListDownloadTintPanel::on_paint, this);
}

void FileListDownloadTintPanel::on_paint(wxPaintEvent &)
{
    wxPaintDC dc(this);
    wxGCDC    gcdc(dc);
    gcdc.SetPen(*wxTRANSPARENT_PEN);
    gcdc.SetBrush(wxBrush(m_fill));
    gcdc.DrawRectangle(GetClientRect());
}

#ifdef __WXMSW__
WXLRESULT FileListDownloadTintPanel::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_NCHITTEST)
        return static_cast<WXLRESULT>(HTTRANSPARENT);
    return wxPanel::MSWWindowProc(nMsg, wParam, lParam);
}
#endif

} // namespace Slic3r::GUI
