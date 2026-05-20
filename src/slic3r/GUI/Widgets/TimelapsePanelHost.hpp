#ifndef slic3r_GUI_TimelapsePanelHost_hpp_
#define slic3r_GUI_TimelapsePanelHost_hpp_

#include <wx/panel.h>

namespace Slic3r::GUI {

class TimelapseFileListCtrl;

namespace detail {
//cj_3
class TimelapseContextMenuPopup;
}

//cj_4
// Wraps TimelapseFileListCtrl; handles row activate (play), context menu rules, and posts commands to StatusPanel.
class TimelapsePanelHost : public wxPanel
{
    friend class detail::TimelapseContextMenuPopup;

public:
    TimelapsePanelHost(wxWindow* parent, wxWindow* event_target);
    ~TimelapsePanelHost() override = default;

    TimelapseFileListCtrl* GetFileList() const { return m_list; }
    bool                   HasActiveFileDownload() const;

private:
    void on_row_activate(wxCommandEvent& e);
    void on_row_context(wxCommandEvent& e);
    void show_context_menu(const wxString& file_name);

    wxWindow*              m_event_target{ nullptr };
    TimelapseFileListCtrl* m_list{ nullptr };
};

} // namespace Slic3r::GUI

#endif
