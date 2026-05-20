#ifndef slic3r_GUI_TimelapseUiEvents_hpp_
#define slic3r_GUI_TimelapseUiEvents_hpp_

#include <wx/event.h>
#include <wx/string.h>

namespace Slic3r::GUI {

//cj_4
// Posted from StatusPanel (toolbar / context delete); PrinterWebView shows delete dialog (local vs printer).
wxDECLARE_EVENT(EVT_TIMELAPSE_DELETE_UI, wxCommandEvent);
//cj_4
// Posted to StatusPanel; PrinterWebView handles play/reveal/download-one.
wxDECLARE_EVENT(EVT_TIMELAPSE_PLAY_FILE, wxCommandEvent);
wxDECLARE_EVENT(EVT_TIMELAPSE_REVEAL_FILE, wxCommandEvent);
wxDECLARE_EVENT(EVT_TIMELAPSE_DOWNLOAD_ONE, wxCommandEvent);
//cj_4
// After list selection is prepared (e.g. single row for context delete).
wxDECLARE_EVENT(EVT_TIMELAPSE_REQUEST_DELETE, wxCommandEvent);

} // namespace Slic3r::GUI

#endif
