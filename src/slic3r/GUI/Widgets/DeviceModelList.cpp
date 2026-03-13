

#include "DeviceModelList.hpp"
#include <wx/sizer.h>
#include <wx/dc.h>
#include <vector>
//#include "../GUI_App.hpp"
#include "../I18N.hpp"
//cj_2
namespace Slic3r {


namespace GUI {

DeviceModelItem::DeviceModelItem(wxWindow* parent,
	const wxString& name,
	const wxBitmap& image,
	double weight,
	const wxString& estimatedTime)
	: wxPanel(parent, wxID_ANY),
	m_name(name),
	m_weight(weight),
	m_estimatedTime(estimatedTime)
{
	

	
	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL); {
		m_checkbox = new wxCheckBox(this, wxID_ANY, "");
		m_checkbox->SetMinSize(wxSize(20, 20));
		m_checkbox->SetMaxSize(wxSize(20, 20));

		m_image = new wxStaticBitmap(this, wxID_ANY, image);
		m_image->SetMinSize(wxSize(20, 20));
		m_image->SetMaxSize(wxSize(20, 20));

		m_namePanel = new wxPanel(this);
		wxBoxSizer* nameSizer = new wxBoxSizer(wxVERTICAL);

		wxArrayString nameLines = wxSplit(name, ',', '\0');
		for (const auto& line : nameLines) {
			wxString linName = line;
			m_nameText = new wxStaticText(m_namePanel, wxID_ANY, linName.Trim());
			nameSizer->Add(m_nameText, 0, wxLEFT | wxRIGHT | wxTOP, 2);
		}
		m_namePanel->SetMinSize(wxSize(320, 20));
		m_namePanel->SetMaxSize(wxSize(320, 20));
		m_namePanel->SetSizer(nameSizer);

		wxString weightStr = wxString::Format("%.2f g", weight);
		m_weightText = new wxStaticText(this, wxID_ANY, weightStr);
		m_weightText->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTSTYLE_NORMAL));
		m_weightText->SetMinSize(wxSize(120, 20));
		m_weightText->SetMaxSize(wxSize(120, 20));

		m_timeText = new wxStaticText(this, wxID_ANY, estimatedTime);
		m_timeText->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTSTYLE_NORMAL));
		
	}
	mainSizer->Add(m_checkbox, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
	mainSizer->Add(m_image, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);
	mainSizer->Add(m_namePanel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 30);
	mainSizer->Add(m_weightText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);
	mainSizer->Add(m_timeText, 0, wxALIGN_CENTER_VERTICAL  | wxRIGHT, 0);
	

	SetSizer(mainSizer);

	
	Bind(wxEVT_PAINT, [this](wxPaintEvent& event) {
		wxPaintDC dc(this);
		wxRect rect = GetClientRect();
		dc.SetPen(wxPen(wxColour(196, 196, 196), 1));
		dc.DrawLine(rect.GetLeft(), rect.GetBottom(),
			rect.GetRight(), rect.GetBottom());
		});
}


DeviceModelListCtrl::DeviceModelListCtrl(wxWindow* parent, wxWindowID id)
	: wxScrolledWindow(parent, id)
{

	SetMinSize(wxSize(602, 915));
	SetMaxSize(wxSize(602, 915));
	m_mainSizer = new wxBoxSizer(wxVERTICAL);

	
	m_headerPanel = CreateHeaderPanel();
	m_mainSizer->Add(m_headerPanel, 0, wxEXPAND);

	SetSizer(m_mainSizer);
	SetScrollRate(10, 10);



}

wxPanel* DeviceModelListCtrl::CreateHeaderPanel()
{
	wxPanel* headerPanel = new wxPanel(this);
	//headerPanel->SetBackgroundColour(*wxWHITE);

	wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

	
	wxStaticText* selectHeader = new wxStaticText(headerPanel, wxID_ANY, "");
	selectHeader->SetMaxSize(wxSize(10, 20));
	selectHeader->SetMinSize(wxSize(10, 20));
	selectHeader->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	headerSizer->Add(selectHeader, 0, wxALIGN_CENTER_VERTICAL | wxUP | wxDOWN, 10);

	
	wxStaticText* imageHeader = new wxStaticText(headerPanel, wxID_ANY, "");
	imageHeader->SetMaxSize(wxSize(10, 20));
	imageHeader->SetMinSize(wxSize(10, 20));
	imageHeader->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	headerSizer->Add(imageHeader, 0, wxALIGN_CENTER_VERTICAL | wxUP | wxDOWN, 5);

	
	wxStaticText* nameHeader = new wxStaticText(headerPanel, wxID_ANY, _L("Name"));
	nameHeader->SetMaxSize(wxSize(380, 20));
	nameHeader->SetMinSize(wxSize(380, 20));
	nameHeader->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	headerSizer->Add(nameHeader, 1, wxALIGN_CENTER_VERTICAL | wxUP | wxDOWN, 10);
	

	
	wxStaticText* weightHeader = new wxStaticText(headerPanel, wxID_ANY, _L("filament weight"));
	weightHeader->SetMaxSize(wxSize(140, 20));
	weightHeader->SetMinSize(wxSize(140, 20));

	weightHeader->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	headerSizer->Add(weightHeader, 0, wxALIGN_CENTER_VERTICAL | wxUP | wxDOWN,10);

	
	wxStaticText* timeHeader = new wxStaticText(headerPanel, wxID_ANY, _L("pre time"));
	timeHeader->SetMaxSize(wxSize(60, 20));
	timeHeader->SetMinSize(wxSize(60, 20));
	timeHeader->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	headerSizer->Add(timeHeader, 0, wxALIGN_CENTER_VERTICAL | wxUP | wxDOWN, 10);

	headerPanel->SetSizer(headerSizer);

	
	headerPanel->Bind(wxEVT_PAINT, [headerPanel](wxPaintEvent& event) {
		wxPaintDC dc(headerPanel);
		wxRect rect = headerPanel->GetClientRect();
		dc.SetPen(wxPen(wxColour(200, 200, 200), 2));
		dc.DrawLine(rect.GetLeft(), rect.GetBottom(),
			rect.GetRight(), rect.GetBottom());
		});

	return headerPanel;
}

void DeviceModelListCtrl::onCheckChange(wxCommandEvent& e)
{
	int selectNum = 0;
 	for (DeviceModelItem* item : m_items) {
		if (item->IsSelected()) {
			++selectNum;
		}
	}
	if (selectNum > 2) {
		selectNum = 2;
	}

	e.SetInt(selectNum);
	e.Skip();
}

void DeviceModelListCtrl::AddItem(const wxString& name,
	const wxBitmap& image,
	double weight,
	const wxString& estimatedTime)
{
	DeviceModelItem* item = new DeviceModelItem(this, name, image, weight, estimatedTime);
	m_items.push_back(item);
	m_mainSizer->Add(item, 0, wxEXPAND);
	
	item->Bind(wxEVT_CHECKBOX, &DeviceModelListCtrl::onCheckChange, this);
	item->SetMinSize(wxSize(-1,40));
	BOOST_LOG_TRIVIAL(fatal) << wxSystemSettings::GetAppearance().IsUsingDarkBackground();;
	Layout();

}

void DeviceModelListCtrl::RemoveSelectedItems()
{
	for (auto iter = m_items.begin(); iter != m_items.end(); ) {
		if ((*iter)->IsSelected()) {
			m_mainSizer->Detach(*iter);
			(*iter)->Destroy();
			iter = m_items.erase(iter);
		} else {
			++iter;
		}
	}
	m_mainSizer->Layout();
}

void DeviceModelListCtrl::ClearAll()
{
	for (auto item : m_items) {
		m_mainSizer->Detach(item);
		item->Destroy();
	}
	m_items.clear();
	m_mainSizer->Layout();
}

std::vector<DeviceModelItem*> DeviceModelListCtrl::GetSelectedItems()
{
	std::vector<DeviceModelItem*> selected;
	for (auto item : m_items) {
		if (item->IsSelected()) {
			selected.push_back(item);
		}
	}
	return selected;
}
}
}