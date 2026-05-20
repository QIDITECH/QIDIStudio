#ifndef slic3r_GUI_ModelFileListView_hpp_
#define slic3r_GUI_ModelFileListView_hpp_

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include "Button.hpp"
#include <string>
#include <vector>

namespace Slic3r::GUI {

//cj_4
enum class ModelFileListCommandType : int
{
    Print       = 0,
    Download    = 1,
    Delete      = 2,
    RevealLocal = 3,
};

wxDECLARE_EVENT(EVT_MODEL_FILE_LIST_COMMAND, wxCommandEvent);

//cj_4
enum class ModelListMountFilterV2
{
    Local,
    Usb,
};

class ModelFileListRow;

//cj_4
// Model file list without multi-select: context menu (print / download / delete / reveal local), hover row, Local|UFD filter.
class ModelFileListView : public wxScrolledWindow
{
public:
    //cj_4 event_target: window that receives EVT_MODEL_FILE_LIST_COMMAND (typically StatusBasePanel).
    ModelFileListView(wxWindow* parent, wxWindow* event_target, wxWindowID id = wxID_ANY);
    ~ModelFileListView() override;

    void ClearAll();
    void AddItem(const wxString& storage_path, const wxBitmap& image, double weight, const wxString& estimated_time);
    bool UpdateItemThumbnail(const wxString& storage_path, const wxBitmap& image);
    void RemoveItemByStoragePath(const wxString& storage_path);
    bool HasActiveFileDownload() const;
    void sync_file_list_surface_colours();
    void refresh_local_exist_flags_from_disk();
    void AfterRowDownloadUiChanged();

    void begin_file_download_for_path(const wxString& storage_path, const std::string& task_id);
    void set_download_progress_for_path(const wxString& storage_path, float fraction01);
    void end_file_download_for_path(const wxString& storage_path, bool failed);

    //cj_4 Invoked from row context menu (row is nested in .cpp).
    void post_list_command(ModelFileListCommandType cmd, const wxString& storage_path);
    void set_hovered_row(ModelFileListRow* row);
    //cj_4 Drop row highlight only when it is the current hover owner (see ModelFileListRow::set_hover).
    void clear_hovered_row_if(ModelFileListRow* row);

private:
    wxPanel* create_header_panel();
    //cj_4
    void refresh_mount_usb_toggle_visibility();
    void apply_mount_filter_visibility();
    void update_mount_filter_button_styles();
    void sync_new_item_mount_visibility(ModelFileListRow* row);
    bool row_matches_mount_filter(const ModelFileListRow* row) const;

    void on_download_overlay_scroll(wxScrollWinEvent& e);
    void on_download_overlay_top_move(wxMoveEvent& e);
    void on_download_overlay_top_size(wxSizeEvent& e);

    wxBoxSizer*     m_main_sizer{ nullptr };
    std::vector<ModelFileListRow*> m_rows;
    wxPanel*        m_header_panel{ nullptr };
    wxPanel*        m_header_sep{ nullptr };
    ModelListMountFilterV2 m_mount_filter{ ModelListMountFilterV2::Local };
    Button*         m_btn_mount_local{ nullptr };
    Button*         m_btn_mount_usb{ nullptr };
    //cj_4
    wxPanel*        m_mount_toggle_wrap{ nullptr };
    wxWindow*       m_event_target{ nullptr };
    ModelFileListRow* m_hovered_row{ nullptr };

    wxWindow*       m_top_level_bound{ nullptr };
};

} // namespace Slic3r::GUI

#endif
