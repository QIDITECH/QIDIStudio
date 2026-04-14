#ifndef slic3r_GUI_DeviceModelList_hpp_
#define slic3r_GUI_DeviceModelList_hpp_

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/statbmp.h>
#include "Button.hpp"
#include "FileListBitmapCheckBox.hpp"
//cj_3 File list uses FileListBitmapCheckBox (15px tier), not ::CheckBox, so Field::CheckBox name clash does not apply here.
#include <wx/panel.h>
#include <string>

// cj_2
namespace Slic3r {


namespace GUI {

class FileListDownloadTintPanel;

class DeviceModelItem : public wxPanel
{
public:
	DeviceModelItem(wxWindow* parent,
		const wxString& name,
		const wxBitmap& image,
		double weight,
		const wxString& estimatedTime);

	bool IsSelected() const;
	void SetSelected(bool selected);

	wxString GetName() const { return m_name; }
	double GetWeight() const { return m_weight; }
	wxString GetEstimatedTime() const { return m_estimatedTime; }
	FileListBitmapCheckBox* getCheckBox() { return m_checkbox; }
	void SetImage(const wxBitmap& bitmap) { if (m_image) m_image->SetBitmap(bitmap); }
	void applyListLabelColour(const wxColour& fg);
	void sync_row_surface_colours(const wxColour& bg, const wxColour& fg);
	//cj_3
	void setLocalCopyExists(bool exists);
	void setDownloadProgressFraction(float fraction01);
	void setFileDownloadTaskId(const std::string& task_id);
	//cj_3
	// Begin download UI; keeps task_id and fraction in sync when scheduling races completion callbacks.
	void beginFileDownload(const std::string& task_id);
	void updateDownloadTintGeometry();
	//cj_3
	bool isFileDownloading() const { return m_downloadFraction >= 0.f; }
	~DeviceModelItem() override;

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
	wxPanel* m_image_panel{ nullptr };
	//cj_3
	wxPanel* m_gap_cb_thumb_panel{ nullptr };
	wxPanel* m_gap_thumb_name_panel{ nullptr };
	wxStaticBitmap* m_image;
	wxString m_name;
	double m_weight;
	wxString m_estimatedTime;

	wxPanel* m_namePanel;
	wxStaticText* m_nameText;
	//cj_3 Fewer row HWNDs for download tint (aligned bundle; same idea as TimelapseFileList).
	wxPanel* m_weight_time_panel{ nullptr };
	wxStaticText* m_weightText;
	wxStaticText* m_timeText;
	wxPanel* m_row_sep{ nullptr };

	//cj_3
	bool m_local_copy_exists{ false };

	FileListDownloadTintPanel *m_download_tint_panel{ nullptr };
	float m_downloadFraction{ -1.f };
};

//cj_2
class DeviceModelListCtrl : public wxScrolledWindow
{
public:
	DeviceModelListCtrl(wxWindow* parent, wxWindowID id = wxID_ANY);
	~DeviceModelListCtrl() override;

        void AddItem(const wxString& name,
                const wxBitmap& image,
                double weight,
                const wxString& estimatedTime);
        void RemoveSelectedItems();
        void ClearAll();
        std::vector<DeviceModelItem*> GetSelectedItems();
        bool UpdateItemThumbnail(const wxString& name, const wxBitmap& image);
        //cj_3
        void SelectAllRows(bool selected);
        //cj_3 Refresh header checkbox and toolbar after a row exits download mode.
        void AfterRowDownloadUiChanged();
        //cj_3
        bool HasActiveFileDownload() const;
	void sync_file_list_surface_colours();

private:
	wxPanel* CreateHeaderPanel();

	void onCheckChange(wxCommandEvent& e);
    //cj_3
    void postSelectionAggregateEvent();
    //cj_3
    void syncHeaderSelectAllState();
private:
	void on_download_overlay_scroll(wxScrollWinEvent& e);
	void on_download_overlay_top_move(wxMoveEvent& e);
	void on_download_overlay_top_size(wxSizeEvent& e);

	wxBoxSizer* m_mainSizer;
	std::vector<DeviceModelItem*> m_items;

	
	wxPanel* m_headerPanel;
	wxPanel* m_header_sep{ nullptr };
    //cj_3
    FileListBitmapCheckBox* m_header_select_all_cb{ nullptr };
    //cj_3 Suppress per-row handlers during SelectAllRows (programmatic SetValue may emit toggle events).
    bool m_bulk_updating_selection{ false };

};
	}
}
#endif
