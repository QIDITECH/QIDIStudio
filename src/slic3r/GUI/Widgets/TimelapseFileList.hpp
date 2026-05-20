#ifndef slic3r_GUI_TimelapseFileList_hpp_
#define slic3r_GUI_TimelapseFileList_hpp_

#include <wx/wx.h>
#include <wx/panel.h>
#include "Button.hpp"
#include "FileListBitmapCheckBox.hpp"
//cj_3 File list uses FileListBitmapCheckBox (15px tier); avoids Field::CheckBox vs global CheckBox include order issues.
#include <wx/statbmp.h>
#include <string>
#include <vector>

namespace Slic3r::GUI {

//cj_4
wxDECLARE_EVENT(EVT_TIMELAPSE_ROW_ACTIVATE, wxCommandEvent);
wxDECLARE_EVENT(EVT_TIMELAPSE_ROW_CONTEXT, wxCommandEvent);

//cj_4
// True if file exists under Preferences download_path (same rules as DeviceModelList local check).
bool timelapse_local_file_ready(const wxString& base_name);

class FileListDownloadTintPanel;
class TimelapseFileListCtrl;

class TimelapseFileItem : public wxPanel
{
public:
    TimelapseFileItem(wxWindow* parent,
        const wxString& name,
        const wxBitmap& image,
        const wxString& size_text,
        const wxString& modified_text);

    bool IsSelected() const;
    void SetSelected(bool selected);

    wxString GetName() const { return m_name; }
    wxString GetSizeText() const { return m_size_text; }
    wxString GetModifiedText() const { return m_modified_text; }
    FileListBitmapCheckBox* getCheckBox() { return m_checkbox; }
    void SetImage(const wxBitmap& bitmap)
    {
        if (m_image)
            m_image->SetBitmap(bitmap);
    }
    void setDownloadProgressFraction(float fraction01);
    void setFileDownloadTaskId(const std::string& task_id);
    //cj_3
    void beginFileDownload(const std::string& task_id);
    void updateDownloadTintGeometry();
    //cj_3
    bool isFileDownloading() const { return m_download_fraction >= 0.f; }
    //cj_3
    void setLocalCopyExists(bool exists);
    //cj_3
    void apply_row_text_colours(const wxColour& base_fg);
    //cj_3 Match DeviceModelItem::sync_row_surface_colours (list row chrome + theme).
    void sync_row_surface_colours(const wxColour& bg, const wxColour& fg, bool hover = false);
    ~TimelapseFileItem() override;

private:
    //cj_4
    void on_row_left_down(wxMouseEvent& e);
    //cj_4
    void on_row_right_down(wxMouseEvent& e);
    //cj_4
    void wire_row_mouse_for_play_and_context();
    //cj_3
    void bind_row_hand_cursor_recursive(wxWindow* w);
    //cj_3
    void bind_row_hover_recursive(wxWindow* w);
    void on_mouse_enter(wxMouseEvent& e);
    void on_mouse_leave(wxMouseEvent& e);
    void on_mouse_motion(wxMouseEvent& e);
    void set_hover(bool h);
    //cj_4
    wxPoint pointer_pos_in_row(wxWindow* event_src, const wxPoint& src_client_pos) const;
    //cj_3
    void updateNameLabelWidth();
    void updateDownloadChrome();
    void onCancelDownload(wxCommandEvent& e);
    //cj_3
    void refresh_local_copy_label_from_disk();

    wxPanel* m_checkbox_cell{ nullptr };
    //cj_3
    Button* m_cancel_download_btn{ nullptr };
    FileListBitmapCheckBox* m_checkbox{ nullptr };
    std::string m_file_download_task_id;
    wxStaticBitmap* m_image{ nullptr };
    wxPanel* m_image_panel{ nullptr };
    //cj_3
    wxPanel* m_gap_cb_thumb_panel{ nullptr };
    wxPanel* m_gap_thumb_name_panel{ nullptr };
    wxString m_name;
    wxString m_size_text;
    wxString m_modified_text;

    //cj_3
    bool m_local_copy_exists{ false };

    wxPanel* m_name_panel{ nullptr };
    wxStaticText* m_name_text{ nullptr };
    wxPanel* m_mtime_size_panel{ nullptr };
    wxStaticText* m_mtime_label{ nullptr };
    wxStaticText* m_size_label{ nullptr };

    FileListDownloadTintPanel *m_download_tint_panel{ nullptr };
    float m_download_fraction{ -1.f };
    //cj_3 Same as DeviceModelItem::m_row_sep (separator line under row).
    wxPanel* m_row_sep{ nullptr };
    //cj_3
    TimelapseFileListCtrl* m_list_host{ nullptr };
    bool                     m_hover{ false };
};

class TimelapseFileListCtrl : public wxScrolledWindow
{
public:
    explicit TimelapseFileListCtrl(wxWindow* parent, wxWindowID id = wxID_ANY);
    ~TimelapseFileListCtrl() override;

    void AddItem(const wxString& name, const wxBitmap& image, const wxString& size_text, const wxString& modified_text);
    void RemoveSelectedItems();
    //cj_3
    void RemoveItems(const std::vector<TimelapseFileItem*>& rows);
    void ClearAll();
    std::vector<TimelapseFileItem*> GetSelectedItems();
    //cj_4
    TimelapseFileItem* FindItemByName(const wxString& name) const;
    //cj_4
    void SelectOnlyItem(TimelapseFileItem* item);
    bool UpdateItemThumbnail(const wxString& name, const wxBitmap& image);
    //cj_3
    void SelectAllRows(bool selected);
    //cj_3
    void AfterRowDownloadUiChanged();
    //cj_3
    void set_hovered_row(TimelapseFileItem* row);
    void clear_hovered_row_if(TimelapseFileItem* row);
    //cj_3
    bool HasActiveFileDownload() const;
    //cj_3 Same role as DeviceModelListCtrl::sync_file_list_surface_colours.
    void sync_file_list_surface_colours();
    //cj_3
    void refresh_local_exist_flags_from_disk();

private:
    void on_download_overlay_scroll(wxScrollWinEvent& e);
    void on_download_overlay_top_move(wxMoveEvent& e);
    void on_download_overlay_top_size(wxSizeEvent& e);

    wxPanel* CreateHeaderPanel();
    void onCheckChange(wxCommandEvent& e);
    //cj_3
    void postSelectionAggregateEvent();
    //cj_3
    void syncHeaderSelectAllState();

    wxBoxSizer* m_mainSizer{ nullptr };
    std::vector<TimelapseFileItem*> m_items;
    wxPanel* m_header_panel{ nullptr };
    //cj_3
    wxPanel* m_header_sep{ nullptr };
    //cj_3
    FileListBitmapCheckBox* m_header_select_all_cb{ nullptr };
    //cj_3 Suppress per-row handlers during SelectAllRows (programmatic SetValue may emit toggle events).
    bool m_bulk_updating_selection{ false };
    //cj_3
    TimelapseFileItem* m_hovered_row{ nullptr };
};

} // namespace Slic3r::GUI

#endif
