#ifndef slic3r_GUI_DeviceModelList_hpp_
#define slic3r_GUI_DeviceModelList_hpp_

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/statbmp.h>
#include <wx/checkbox.h>
#include <wx/panel.h>

// cj_2
namespace Slic3r {


namespace GUI {

class DeviceModelItem : public wxPanel
{
public:
	DeviceModelItem(wxWindow* parent,
		const wxString& name,
		const wxBitmap& image,
		double weight,
		const wxString& estimatedTime);

	bool IsSelected() const { return m_checkbox->GetValue(); }
	void SetSelected(bool selected) { m_checkbox->SetValue(selected); }

	wxString GetName() const { return m_name; }
	double GetWeight() const { return m_weight; }
	wxString GetEstimatedTime() const { return m_estimatedTime; }
	wxCheckBox* getCheckBox() { return m_checkbox; }
private:
	wxCheckBox* m_checkbox;
	wxStaticBitmap* m_image;
	wxString m_name;
	double m_weight;
	wxString m_estimatedTime;

	wxPanel* m_namePanel;
	wxStaticText* m_nameText;
	wxStaticText* m_weightText;
	wxStaticText* m_timeText;
};

//cj_2
class DeviceModelListCtrl : public wxScrolledWindow
{
public:
	DeviceModelListCtrl(wxWindow* parent, wxWindowID id = wxID_ANY);

        void AddItem(const wxString& name,
                const wxBitmap& image,
                double weight,
                const wxString& estimatedTime);
        void RemoveSelectedItems();
        void ClearAll();
        std::vector<DeviceModelItem*> GetSelectedItems();private:
	wxPanel* CreateHeaderPanel();

	void onCheckChange(wxCommandEvent& e);
private:
	wxBoxSizer* m_mainSizer;
	std::vector<DeviceModelItem*> m_items;

	
	wxPanel* m_headerPanel;

};
	}
}
#endif