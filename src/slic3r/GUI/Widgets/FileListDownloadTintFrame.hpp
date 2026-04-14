#pragma once

#include <wx/colour.h>
#include <wx/panel.h>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace Slic3r::GUI {

//cj_3 Same translucent technique as StatusPanel overlay: wxGCDC + alpha brush; not wxFrame/SetTransparent.
class FileListDownloadTintPanel : public wxPanel
{
public:
    FileListDownloadTintPanel(wxWindow *parent, const wxColour &rgb, int alpha);

private:
    void on_paint(wxPaintEvent &);

    wxColour m_fill;

#ifdef __WXMSW__
protected:
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
};

} // namespace Slic3r::GUI
