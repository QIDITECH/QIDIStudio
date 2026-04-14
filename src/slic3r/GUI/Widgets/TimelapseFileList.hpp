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

class FileListDownloadTintPanel;

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
    void sync_row_surface_colours(const wxColour& bg, const wxColour& fg);
    ~TimelapseFileItem() override;

private:
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
};

class TimelapseFileListCtrl : public wxScrolledWindow
{
public:
    explicit TimelapseFileListCtrl(wxWindow* parent, wxWindowID id = wxID_ANY);
    ~TimelapseFileListCtrl() override;

    void AddItem(const wxString& name, const wxBitmap& image, const wxString& size_text, const wxString& modified_text);
    void RemoveSelectedItems();
    void ClearAll();
    std::vector<TimelapseFileItem*> GetSelectedItems();
    bool UpdateItemThumbnail(const wxString& name, const wxBitmap& image);
    //cj_3
    void SelectAllRows(bool selected);
    //cj_3
    void AfterRowDownloadUiChanged();
    //cj_3
    bool HasActiveFileDownload() const;
    //cj_3 Same role as DeviceModelListCtrl::sync_file_list_surface_colours.
    void sync_file_list_surface_colours();

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
};

} // namespace Slic3r::GUI

#endif
