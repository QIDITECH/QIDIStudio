

#include "DeviceModelList.hpp"
#include "FileListDownloadTintFrame.hpp"
#include <wx/sizer.h>
#include <wx/dc.h>
//cj_3
#include <wx/dcclient.h>
//cj_3
#include <wx/control.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>
//cj_3
#include <wx/filename.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "../I18N.hpp"
#include "../DownloadManager.hpp"
#include "../GUI_App.hpp"
#include "StateColor.hpp"
#include "Label.hpp"
//cj_2
namespace Slic3r {


namespace GUI {

//cj_4
static wxString model_file_list_display_label(const wxString& storage_path)
{
	if (storage_path.empty())
		return wxString();
	wxString s(storage_path);
	s.Replace("\\", "/");
	const int pos = s.Find('/', true);
	if (pos == wxNOT_FOUND || pos + 1 >= static_cast<int>(s.length()))
		return s;
	return s.Mid(pos + 1);
}

//cj_4
static bool model_file_list_path_is_usb(const wxString& storage_path)
{
	if (storage_path.empty())
		return false;
	wxString s(storage_path);
	s.Replace("\\", "/");
	return s.StartsWith("USB/");
}

//cj_3
// MSVC: helpers in an anonymous namespace may not resolve from out-of-line member definitions in the same TU (C3861); use file-scope static in the GUI namespace instead.
static wxColour file_list_row_separator_colour()
{
	return wxGetApp().dark_mode() ? wxColour(76, 76, 85) : wxColour(238, 238, 238);
}

static wxColour file_list_panel_background()
{
	return wxGetApp().dark_mode() ? wxColour(45, 45, 49) : wxColour(255, 255, 255);
}

// Light theme matches legacy file_list_primary; dark theme matches GUI_App m_color_label_sys (#B2B3B5).
static wxColour file_list_primary_text_colour()
{
	return wxGetApp().dark_mode() ? wxColour(0xB2, 0xB3, 0xB5) : wxColour(140, 140, 140);
}

//cj_3 Idle face for Local/USB toggle; same as ModelFileListView (separator tone; no #EEE under dark refresh).
static wxColour mount_filter_button_idle_background()
{
	return file_list_row_separator_colour();
}

//cj_4 Same idle/hover/press as StatusBasePanel::create_tab_page tab buttons (#EEE / #999).
static void apply_model_file_mount_button_chrome(Button* button)
{
	if (button == nullptr)
		return;
	//cj_3
	const wxColour idle_bg = mount_filter_button_idle_background();
	StateColor add_btn_bg(
		std::pair<wxColour, int>(wxColour(0, 66, 255), StateColor::Pressed),
		std::pair<wxColour, int>(wxColour(116, 168, 255), StateColor::Hovered),
		std::pair<wxColour, int>(idle_bg, StateColor::Normal));
	//cj_3
	StateColor btn_text_color(std::pair<wxColour, int>(file_list_primary_text_colour(), StateColor::Normal));
	button->SetBackgroundColor(add_btn_bg);
	button->SetBorderWidth(0);
	button->SetFont(Label::Body_15);
	button->SetTextColor(btn_text_color);
	//cj_3 MSW StaticBox pre-clear must not use fixed #EEE in dark mode.
	button->SetBackgroundColour(file_list_panel_background());
}

//cj_4 Unselected / selected faces match tabSiwtch.
static void set_mount_filter_button_unselected(Button* b)
{
	if (b == nullptr)
		return;
	//cj_3
	b->SetBackgroundColorNormal(mount_filter_button_idle_background());
	b->SetTextColorNormal(file_list_primary_text_colour());
}

static void set_mount_filter_button_selected(Button* b)
{
	if (b == nullptr)
		return;
	b->SetBackgroundColorNormal(wxColour(68, 121, 251));
	b->SetTextColorNormal(wxColour(255, 255, 255));
}

//cj_3 Same face as printing-progress filename (HarmonyOS / Label body); row text at 10pt.
static wxFont file_list_row_content_font()
{
#ifdef __WXOSX_MAC__
	return ::Label::Body_10;
#else
	return wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC"));
#endif
}

//cj_3 List column headers: same face as rows, previous size (11pt / Body_11).
static wxFont file_list_header_label_font()
{
#ifdef __WXOSX_MAC__
	return ::Label::Body_11;
#else
	return wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC"));
#endif
}

//cj_3
//cj_3
// Blue when a local file already exists under Preferences download_path (readable in light / dark theme).
static wxColour file_list_local_exists_text_colour()
{
	return wxGetApp().dark_mode() ? wxColour(150, 195, 255) : wxColour(95, 145, 235);
}

//cj_3 Relative path under Preferences download_path (may contain subdirs, e.g. "dir/model.gcode").
static bool local_file_exists_in_download_dir(const wxString& file_name)
{
	if (file_name.empty())
		return false;
	wxString rel(file_name);
	rel.Replace("\\", "/");
	while (!rel.empty() && rel[0] == '/')
		rel = rel.Mid(1);
	if (rel.empty())
		return false;
	if (rel.Find(wxString("/../")) != wxNOT_FOUND || rel.StartsWith(wxString("../")) || rel.EndsWith(wxString("/..")) ||
	    rel == wxString(".."))
		return false;
#ifdef __WXMSW__
	if (rel.length() >= 2 && wxIsalpha(rel[0]) && rel[1] == ':')
		return false;
#endif
	const std::string dp = wxGetApp().app_config->get("download_path");
	if (dp.empty())
		return false;
	wxFileName root(wxString::FromUTF8(dp), wxEmptyString);
	root.MakeAbsolute();
	root.Normalize();
	wxString full = root.GetPathWithSep() + rel;
	wxFileName fn(full);
	fn.Normalize();
	if (!fn.FileExists())
		return false;
	wxString rfull = root.GetFullPath();
	wxString ffull = fn.GetFullPath();
#ifdef __WXMSW__
	rfull.MakeLower();
	ffull.MakeLower();
#endif
	const wxUniChar sep = wxFileName::GetPathSeparator();
	if (!rfull.empty() && rfull.Last() != sep)
		rfull += sep;
	return ffull.StartsWith(rfull);
}

static constexpr int kFileListRowHeightDip = 35;
static constexpr int kFileListRowSeparatorDip = 2;
//cj_3 Checkbox and thumbnail logical sizes (DIP); passed to FromDIP in row/header builders.
static constexpr int kFileListCheckboxDip = kFileListBitmapCheckboxScalablePxCnt;
static constexpr int kFileListThumbDip    = 18;
//cj_3 Model list header/data column widths (DIP); keep header row bundle in sync with DeviceModelItem.
static constexpr int kModelListWeightColDip = 93;
static constexpr int kModelListTimeColDip   = 78;
//cj_3 Max width (DIP) for the name column in data rows only.
static constexpr int kModelListNameColMaxDip = 350;
//cj_3 Header-only: tail pad after Local/USB (inside name flex item) so weight column x matches rows.
static constexpr int kModelListNameHeaderToggleBandMaxDip = 260;
static constexpr int kModelListNameHeaderToggleToWeightPadDip =
	kModelListNameColMaxDip - kModelListNameHeaderToggleBandMaxDip;
//cj_3 Row gutters (DIP): checkbox–thumb, thumb–name, name–weight, weight–time, time–row edge.
static constexpr int kModelListGapCbToThumbDip    = 10;
static constexpr int kModelListGapThumbToNameDip  = 10;
static constexpr int kModelListGapNameToWeightDip = 0;
static constexpr int kModelListGapWeightToTimeDip  = 3;
static constexpr int kModelListGapTimeToEdgeDip    = 2;

static void apply_surface_to_panels(wxWindow* root, const wxColour& bg)
{
	if (dynamic_cast<wxPanel*>(root))
		root->SetBackgroundColour(bg);
	for (wxWindow* ch : root->GetChildren())
		apply_surface_to_panels(ch, bg);
}

//cj_3 Tint RGB; alpha in FileListDownloadTintPanel via wxGCDC (same approach as StatusPanel test overlay).
static wxColour file_list_download_tint_colour()
{
	return wxColour(95, 145, 235);
}

static constexpr int k_download_tint_alpha = 120;

//cj_3
// Fixed-width horizontal gap as a real panel (not wxSizer spacer / Add(width,...)).
static wxPanel* make_file_list_row_gap_panel(wxWindow* parent, int width_px, int row_h_px, const wxColour& bg)
{
	wxPanel* p = new wxPanel(parent);
	p->SetBackgroundColour(bg);
	p->SetMinSize(wxSize(width_px, row_h_px));
	p->SetMaxSize(wxSize(width_px, row_h_px));
	return p;
}

static void apply_fg_to_static_texts(wxWindow* root, const wxColour& fg)
{
	if (auto* st = dynamic_cast<wxStaticText*>(root))
		st->SetForegroundColour(fg);
	for (wxWindow* ch : root->GetChildren())
		apply_fg_to_static_texts(ch, fg);
}

DeviceModelItem::DeviceModelItem(wxWindow* parent,
	const wxString& storagePath,
	const wxBitmap& image,
	double weight,
	const wxString& estimatedTime)
	: wxPanel(parent, wxID_ANY),
	m_storage_path(storagePath),
	m_display_name(model_file_list_display_label(storagePath)),
	m_is_usb(model_file_list_path_is_usb(storagePath)),
	m_weight(weight),
	m_estimatedTime(estimatedTime)
{
	const int row_lead = parent->FromDIP(10);
	const int thumb_sz = parent->FromDIP(kFileListThumbDip);
	const int row_h    = parent->FromDIP(kFileListRowHeightDip);
	const int sep_h    = parent->FromDIP(kFileListRowSeparatorDip);
	const int g_cb_thumb   = parent->FromDIP(kModelListGapCbToThumbDip);
	const int g_thumb_name = parent->FromDIP(kModelListGapThumbToNameDip);
	const int g_name_wt    = parent->FromDIP(kModelListGapNameToWeightDip);
	const int g_wt_mid     = parent->FromDIP(kModelListGapWeightToTimeDip);
	const int g_time_end   = parent->FromDIP(kModelListGapTimeToEdgeDip);
	const int col_weight_w    = parent->FromDIP(kModelListWeightColDip);
	const int col_time_w      = parent->FromDIP(kModelListTimeColDip);
	const int w_time_bundle_w = col_weight_w + g_wt_mid + col_time_w + g_time_end;

	//cj_4 WxDeclareFirst: declarations → root sizer + SetSizer(this) → mainSizer → { build } → Add.
	wxBoxSizer* outer     = nullptr;
	wxBoxSizer* mainSizer = nullptr;
	wxBoxSizer* cell_col  = nullptr;
	wxBoxSizer* img_col   = nullptr;
	wxBoxSizer* img_row   = nullptr;
	wxBoxSizer* nameSizer = nullptr;
	wxBoxSizer* wt_h      = nullptr;
	wxBoxSizer* wt_v      = nullptr;

	SetBackgroundColour(file_list_panel_background());
	outer = new wxBoxSizer(wxVERTICAL);
	SetSizer(outer);

	mainSizer = new wxBoxSizer(wxHORIZONTAL);

	{
		//cj_3 Stack cancel (file_cancel.svg) above checkbox; same cell width as checkbox column.
		m_checkbox_cell = new wxPanel(this);
		m_checkbox_cell->SetBackgroundColour(file_list_panel_background());
		cell_col = new wxBoxSizer(wxVERTICAL);
		m_checkbox_cell->SetSizer(cell_col);
		{
			//cj_3 Bitmap toggle (15px tier); not Field::CheckBox.
			m_checkbox = new FileListBitmapCheckBox(m_checkbox_cell, wxID_ANY);
			const int cb_cell_w = m_checkbox->GetSize().GetWidth();
			m_checkbox_cell->SetMinSize(wxSize(cb_cell_w, 2 * cb_cell_w + parent->FromDIP(2)));
			m_cancel_download_btn = new Button(m_checkbox_cell, wxEmptyString, "file_cancel", 0, cb_cell_w, wxID_ANY);
			m_cancel_download_btn->SetPaddingSize(wxSize(0, 0));
			m_cancel_download_btn->SetMinSize(wxSize(cb_cell_w, cb_cell_w));
			m_cancel_download_btn->SetMaxSize(wxSize(cb_cell_w, cb_cell_w));
			m_cancel_download_btn->SetBorderWidth(0);
			m_cancel_download_btn->SetCornerRadius(0);
			m_cancel_download_btn->SetBackgroundColor(StateColor(file_list_panel_background()));
			m_cancel_download_btn->SetToolTip(_L("Cancel download"));
#ifdef __WXMSW__
			m_cancel_download_btn->SetCursor(wxCURSOR_HAND);
#endif
			m_cancel_download_btn->Bind(wxEVT_BUTTON, &DeviceModelItem::onCancelDownload, this);
			m_cancel_download_btn->SetCanFocus(false);
			m_cancel_download_btn->Hide();
		}
		cell_col->AddStretchSpacer(1);
		cell_col->Add(m_cancel_download_btn, 0, wxALIGN_CENTER_HORIZONTAL);
		cell_col->Add(m_checkbox, 0, wxALIGN_CENTER_HORIZONTAL);
		cell_col->AddStretchSpacer(1);

		m_image_panel = new wxPanel(this);
		m_image_panel->SetBackgroundColour(file_list_panel_background());
		img_col = new wxBoxSizer(wxVERTICAL);
		img_row = new wxBoxSizer(wxHORIZONTAL);
		m_image_panel->SetSizer(img_col);
		{
			m_image = new wxStaticBitmap(m_image_panel, wxID_ANY, image);
			m_image->SetMinSize(wxSize(thumb_sz, thumb_sz));
			m_image->SetMaxSize(wxSize(thumb_sz, thumb_sz));
		}
		img_row->Add(m_image, 0, wxALIGN_CENTER_VERTICAL);
		img_col->AddStretchSpacer(1);
		img_col->Add(img_row, 0, wxALIGN_CENTER_HORIZONTAL);
		img_col->AddStretchSpacer(1);
		{
			const wxSize img_col_sz(thumb_sz, row_h);
			m_image_panel->SetMinSize(img_col_sz);
			m_image_panel->SetMaxSize(img_col_sz);
		}

		const wxColour row_bg = file_list_panel_background();
		m_gap_cb_thumb_panel   = make_file_list_row_gap_panel(this, g_cb_thumb, row_h, row_bg);
		m_gap_thumb_name_panel = make_file_list_row_gap_panel(this, g_thumb_name, row_h, row_bg);

		m_namePanel = new wxPanel(this);
		m_namePanel->SetBackgroundColour(file_list_panel_background());
		nameSizer = new wxBoxSizer(wxVERTICAL);
		m_namePanel->SetSizer(nameSizer);
		{
			m_nameText = new wxStaticText(m_namePanel, wxID_ANY, m_display_name);
			m_nameText->SetFont(file_list_row_content_font());
		}
		nameSizer->AddStretchSpacer(1);
		nameSizer->Add(m_nameText, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT, 0);
		nameSizer->AddStretchSpacer(1);
		m_namePanel->SetMaxSize(wxSize(parent->FromDIP(kModelListNameColMaxDip), -1));
		m_namePanel->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
			updateNameLabelWidth();
			e.Skip();
		});

		const wxString weightStr = wxString::Format("%.2fg", weight);
		const wxFont   row_font  = file_list_row_content_font();
		wxString       time_compact(estimatedTime);
		time_compact.Replace(" ", wxEmptyString, true);
		time_compact.Replace("\t", wxEmptyString, true);

		m_weight_time_panel = new wxPanel(this);
		m_weight_time_panel->SetBackgroundColour(file_list_panel_background());
		wt_h = new wxBoxSizer(wxHORIZONTAL);
		wt_v = new wxBoxSizer(wxVERTICAL);
		m_weight_time_panel->SetSizer(wt_v);
		{
			m_weightText = new wxStaticText(m_weight_time_panel, wxID_ANY, weightStr);
			m_weightText->SetFont(row_font);
			m_weightText->SetMinSize(wxSize(col_weight_w, -1));
			m_weightText->SetMaxSize(wxSize(col_weight_w, -1));
			m_timeText = new wxStaticText(m_weight_time_panel, wxID_ANY, time_compact);
			m_timeText->SetFont(row_font);
			m_timeText->SetMinSize(wxSize(col_time_w, -1));
			m_timeText->SetMaxSize(wxSize(col_time_w, -1));
		}
		wt_h->Add(m_weightText, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxRIGHT, g_wt_mid);
		wt_h->Add(m_timeText, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxRIGHT, g_time_end);
		wt_v->AddStretchSpacer(1);
		wt_v->Add(wt_h, 0, wxALIGN_CENTER_VERTICAL);
		wt_v->AddStretchSpacer(1);
		m_weight_time_panel->SetMinSize(wxSize(w_time_bundle_w, row_h));
		m_weight_time_panel->SetMaxSize(wxSize(w_time_bundle_w, row_h));

		m_row_sep = new wxPanel(this);
		m_row_sep->SetBackgroundColour(file_list_row_separator_colour());
		m_row_sep->SetMinSize(wxSize(-1, sep_h));
		m_row_sep->SetMaxSize(wxSize(-1, sep_h));
	}

	mainSizer->Add(m_checkbox_cell, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_lead);
	mainSizer->Add(m_gap_cb_thumb_panel, 0, wxALIGN_CENTER_VERTICAL);
	mainSizer->Add(m_image_panel, 0, wxALIGN_CENTER_VERTICAL);
	mainSizer->Add(m_gap_thumb_name_panel, 0, wxALIGN_CENTER_VERTICAL);
	mainSizer->Add(m_namePanel, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxRIGHT, g_name_wt);
	mainSizer->Add(m_weight_time_panel, 0, wxALIGN_CENTER_VERTICAL);

	outer->Add(mainSizer, 1, wxEXPAND);
	outer->Add(m_row_sep, 0, wxEXPAND);

	m_download_tint_panel = new FileListDownloadTintPanel(this, file_list_download_tint_colour(), k_download_tint_alpha);
	m_download_tint_panel->Hide();
	m_download_tint_panel->Lower();
	Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
		updateDownloadTintGeometry();
		e.Skip();
	});

	SetMinSize(wxSize(-1, row_h + sep_h));
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	applyListLabelColour(file_list_primary_text_colour());

	//cj_3
	Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
	Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
		wxPaintDC dc(this);
		const wxRect rect = GetClientRect();
		dc.SetBrush(wxBrush(GetBackgroundColour()));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(rect);
	});

	//cj_3
	CallAfter([this]() { updateNameLabelWidth(); });
}

DeviceModelItem::~DeviceModelItem()
{
	if (m_download_tint_panel) {
		m_download_tint_panel->Destroy();
		m_download_tint_panel = nullptr;
	}
}

//cj_3
void DeviceModelItem::updateNameLabelWidth()
{
	if (!m_nameText || !m_namePanel)
		return;
	wxClientDC dc(m_nameText);
	dc.SetFont(m_nameText->GetFont());
	const int hpad = m_namePanel->FromDIP(4);
	int w = m_namePanel->GetClientSize().GetWidth() - 2 * hpad;
	if (w < 1)
		w = 1;
	m_nameText->SetLabel(wxControl::Ellipsize(m_display_name, dc, wxELLIPSIZE_END, w));
}

//cj_3
void DeviceModelItem::setLocalCopyExists(bool exists)
{
	m_local_copy_exists = exists;
}

//cj_3
void DeviceModelItem::applyListLabelColour(const wxColour& fg)
{
	const wxColour use_fg = m_local_copy_exists ? file_list_local_exists_text_colour() : fg;
	if (m_nameText)
		m_nameText->SetForegroundColour(use_fg);
	if (m_weightText)
		m_weightText->SetForegroundColour(use_fg);
	if (m_timeText)
		m_timeText->SetForegroundColour(use_fg);
	if (m_cancel_download_btn)
		m_cancel_download_btn->SetBackgroundColor(StateColor(GetBackgroundColour()));
}

void DeviceModelItem::sync_row_surface_colours(const wxColour& bg, const wxColour& fg)
{
	SetBackgroundColour(bg);
	if (m_checkbox_cell)
		m_checkbox_cell->SetBackgroundColour(bg);
	if (m_image_panel)
		m_image_panel->SetBackgroundColour(bg);
	if (m_gap_cb_thumb_panel)
		m_gap_cb_thumb_panel->SetBackgroundColour(bg);
	if (m_gap_thumb_name_panel)
		m_gap_thumb_name_panel->SetBackgroundColour(bg);
	if (m_namePanel)
		m_namePanel->SetBackgroundColour(bg);
	if (m_weight_time_panel)
		m_weight_time_panel->SetBackgroundColour(bg);
	if (m_cancel_download_btn)
		m_cancel_download_btn->SetBackgroundColor(StateColor(bg));
	if (m_row_sep)
		m_row_sep->SetBackgroundColour(file_list_row_separator_colour());
	applyListLabelColour(fg);
}

void DeviceModelItem::updateDownloadTintGeometry()
{
	if (!m_download_tint_panel) {
		return;
	}
	if (m_downloadFraction < 0.f) {
		if (m_download_tint_panel->IsShown())
			m_download_tint_panel->Hide();
		//cj_3 Tint is created last; keep it under siblings when idle so it cannot steal hits on MSW.
		m_download_tint_panel->Lower();
		return;
	}
	if (!IsShownOnScreen()) {
		if (m_download_tint_panel->IsShown())
			m_download_tint_panel->Hide();
		m_download_tint_panel->Lower();
		return;
	}
	const wxSize sz = GetClientSize();
	const int line_reserve = FromDIP(kFileListRowSeparatorDip);
	//cj_3 Tint span = thumbnail column left through weight+time panel right (not full row client width).
	int exclude_left = 0;
	if (m_image_panel) {
		exclude_left = std::clamp(m_image_panel->GetRect().GetLeft(), 0, sz.GetWidth());
	} else if (m_checkbox_cell) {
		exclude_left = std::clamp(
			m_checkbox_cell->GetRect().GetRight() + 1 + FromDIP(kModelListGapCbToThumbDip), 0, sz.GetWidth());
	}
	int content_right = sz.GetWidth();
	if (m_weight_time_panel) {
		content_right = std::clamp(m_weight_time_panel->GetRect().GetRight() + 1, exclude_left, sz.GetWidth());
	}
	const int content_w = std::max(0, content_right - exclude_left);
	const int h = std::max(0, sz.GetHeight() - line_reserve);
	const float ff = std::clamp(m_downloadFraction, 0.f, 1.f);
	int w = static_cast<int>(std::floor(static_cast<double>(content_w) * static_cast<double>(ff)));
	w = std::clamp(w, 0, content_w);
	if (w <= 0 || h <= 0 || content_w <= 0) {
		m_download_tint_panel->Hide();
		m_download_tint_panel->Lower();
		return;
	}
	m_download_tint_panel->Move(exclude_left, 0);
	m_download_tint_panel->SetSize(wxSize(w, h));
	if (!m_download_tint_panel->IsShown()) {
		m_download_tint_panel->Show();
	}
	m_download_tint_panel->Raise();
}

void DeviceModelItem::setFileDownloadTaskId(const std::string& task_id)
{
	m_file_download_task_id = task_id;
}

void DeviceModelItem::beginFileDownload(const std::string& task_id)
{
	m_file_download_task_id = task_id;
	m_downloadFraction = 0.f;
	updateDownloadChrome();
	updateDownloadTintGeometry();
	//cj_3
	if (auto* list = dynamic_cast<DeviceModelListCtrl*>(GetParent()))
		list->AfterRowDownloadUiChanged();
}

void DeviceModelItem::updateDownloadChrome()
{
	const bool dl = isFileDownloading();
	if (m_checkbox) {
		m_checkbox->Show(!dl);
		if (dl)
			m_checkbox->SetValue(false);
	}
	if (m_cancel_download_btn) {
		m_cancel_download_btn->Show(dl);
		//cj_3
		// Avoid Raise() here: progress callbacks fire often; repeated Raise+Layout starves the UI and steals interaction.
	}
	if (m_checkbox_cell)
		m_checkbox_cell->Layout();
	const int row_h = FromDIP(kFileListRowHeightDip);
	const int sep_h = FromDIP(kFileListRowSeparatorDip);
	SetMinSize(wxSize(-1, row_h + sep_h));
	Layout();
	if (wxWindow* p = GetParent()) {
		p->Layout();
		if (auto* sw = dynamic_cast<wxScrolledWindow*>(p))
			sw->FitInside();
	}
}

void DeviceModelItem::onCancelDownload(wxCommandEvent& e)
{
	(void)e;
	if (!m_file_download_task_id.empty())
		DownloadManager::getInstance().cancelFileDownload(m_file_download_task_id);
	setDownloadProgressFraction(-1.f);
}

bool DeviceModelItem::IsSelected() const
{
	if (isFileDownloading())
		return false;
	return m_checkbox && m_checkbox->GetValue();
}

void DeviceModelItem::SetSelected(bool selected)
{
	if (isFileDownloading()) {
		if (m_checkbox)
			m_checkbox->SetValue(false);
		return;
	}
	if (m_checkbox)
		m_checkbox->SetValue(selected);
}

void DeviceModelItem::refresh_local_copy_label_from_disk()
{
	m_local_copy_exists = local_file_exists_in_download_dir(m_storage_path);
	applyListLabelColour(file_list_primary_text_colour());
	Refresh();
	if (wxWindow* p = GetParent())
		p->Refresh();
}

void DeviceModelItem::setDownloadProgressFraction(float fraction01)
{
	const bool was_downloading = (m_downloadFraction >= 0.f);
	m_downloadFraction = fraction01;
	if (fraction01 < 0.f)
		m_file_download_task_id.clear();
	const bool now_downloading = (m_downloadFraction >= 0.f);
	//cj_3
	// Full layout only when entering/leaving download; progress ticks only move the tint overlay.
	if (was_downloading != now_downloading)
		updateDownloadChrome();
	updateDownloadTintGeometry();
	//cj_3
	if (was_downloading && fraction01 < 0.f) {
		refresh_local_copy_label_from_disk();
		if (auto* list = dynamic_cast<DeviceModelListCtrl*>(GetParent()))
			list->AfterRowDownloadUiChanged();
		//cj_3
		// Idle pass: OS may not report the new file until after the download worker returns.
		wxGetApp().CallAfter([this]() { refresh_local_copy_label_from_disk(); });
	}
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
	//cj_3
	EnableScrolling(false, true);

	Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& e) {
		sync_file_list_surface_colours();
		e.Skip();
	});
	Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
		e.Skip();
		for (DeviceModelItem* item : m_items)
			item->updateDownloadTintGeometry();
	});
	Bind(wxEVT_SCROLLWIN_TOP, &DeviceModelListCtrl::on_download_overlay_scroll, this);
	Bind(wxEVT_SCROLLWIN_BOTTOM, &DeviceModelListCtrl::on_download_overlay_scroll, this);
	Bind(wxEVT_SCROLLWIN_LINEUP, &DeviceModelListCtrl::on_download_overlay_scroll, this);
	Bind(wxEVT_SCROLLWIN_LINEDOWN, &DeviceModelListCtrl::on_download_overlay_scroll, this);
	Bind(wxEVT_SCROLLWIN_PAGEUP, &DeviceModelListCtrl::on_download_overlay_scroll, this);
	Bind(wxEVT_SCROLLWIN_PAGEDOWN, &DeviceModelListCtrl::on_download_overlay_scroll, this);
	Bind(wxEVT_SCROLLWIN_THUMBTRACK, &DeviceModelListCtrl::on_download_overlay_scroll, this);
	Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &DeviceModelListCtrl::on_download_overlay_scroll, this);
	if (wxWindow* top = wxGetTopLevelParent(this)) {
		top->Bind(wxEVT_MOVE, &DeviceModelListCtrl::on_download_overlay_top_move, this);
		top->Bind(wxEVT_SIZE, &DeviceModelListCtrl::on_download_overlay_top_size, this);
	}
	sync_file_list_surface_colours();
}

DeviceModelListCtrl::~DeviceModelListCtrl()
{
	if (wxWindow* top = wxGetTopLevelParent(this)) {
		top->Unbind(wxEVT_MOVE, &DeviceModelListCtrl::on_download_overlay_top_move, this);
		top->Unbind(wxEVT_SIZE, &DeviceModelListCtrl::on_download_overlay_top_size, this);
	}
}

void DeviceModelListCtrl::on_download_overlay_scroll(wxScrollWinEvent& e)
{
	for (DeviceModelItem* item : m_items)
		item->updateDownloadTintGeometry();
	e.Skip();
}

void DeviceModelListCtrl::on_download_overlay_top_move(wxMoveEvent& e)
{
	for (DeviceModelItem* item : m_items)
		item->updateDownloadTintGeometry();
	e.Skip();
}

void DeviceModelListCtrl::on_download_overlay_top_size(wxSizeEvent& e)
{
	for (DeviceModelItem* item : m_items)
		item->updateDownloadTintGeometry();
	e.Skip();
}

void DeviceModelListCtrl::sync_file_list_surface_colours()
{
	const wxColour bg = file_list_panel_background();
	const wxColour fg = file_list_primary_text_colour();
	SetBackgroundColour(bg);
	if (m_headerPanel) {
		m_headerPanel->SetBackgroundColour(bg);
		apply_surface_to_panels(m_headerPanel, bg);
		apply_fg_to_static_texts(m_headerPanel, fg);
	}
	if (m_header_sep)
		m_header_sep->SetBackgroundColour(file_list_row_separator_colour());
	//cj_3
	for (DeviceModelItem* it : m_items) {
		it->setLocalCopyExists(local_file_exists_in_download_dir(it->GetName()));
		it->sync_row_surface_colours(bg, fg);
	}
	//cj_4 Re-apply chrome so Normal/hover StateColor tracks theme; then restore selection.
	if (m_btn_mount_local != nullptr && m_btn_mount_usb != nullptr) {
		apply_model_file_mount_button_chrome(m_btn_mount_local);
		apply_model_file_mount_button_chrome(m_btn_mount_usb);
	}
	updateMountFilterButtonStyles();
	Refresh();
}

wxPanel* DeviceModelListCtrl::CreateHeaderPanel()
{
	//cj_4 WxDeclareFirst: declarations → headerPanel → headerOuter + SetSizer → headerSizer → { build } → Add.
	wxPanel*      headerPanel              = nullptr;
	wxBoxSizer*   headerOuter              = nullptr;
	wxBoxSizer*   headerSizer             = nullptr;
	wxPanel*      hdr_gap_cb               = nullptr;
	wxPanel*      hdr_gap_tn               = nullptr;
	wxPanel*      nameHeaderWrap           = nullptr;
	wxBoxSizer*   nameHeaderRow            = nullptr;
	wxStaticText* nameHeader               = nullptr;
	wxPanel*      mountToggleWrap          = nullptr;
	wxBoxSizer*   mountVBox                = nullptr;
	wxBoxSizer*   mountHBox                = nullptr;
	wxPanel*      hdr_toggle_to_weight_pad = nullptr;
	wxPanel*      wt_header_panel          = nullptr;
	wxStaticText* weightHeader             = nullptr;
	wxStaticText* timeHeader               = nullptr;
	wxBoxSizer*   hdr_wt_h                 = nullptr;
	wxBoxSizer*   hdr_wt_v                 = nullptr;

	headerPanel = new wxPanel(this);
	const wxColour header_bg = file_list_panel_background();
	headerPanel->SetBackgroundColour(header_bg);
	const int header_row_h = headerPanel->FromDIP(kFileListRowHeightDip);
	headerPanel->SetMinSize(wxSize(-1, header_row_h));
	const int row_lead     = headerPanel->FromDIP(10);
	const int thumb_hdr    = headerPanel->FromDIP(kFileListThumbDip);
	const int g_cb_thumb   = headerPanel->FromDIP(kModelListGapCbToThumbDip);
	const int g_thumb_name = headerPanel->FromDIP(kModelListGapThumbToNameDip);
	const int g_name_wt    = headerPanel->FromDIP(kModelListGapNameToWeightDip);
	const int g_wt_mid     = headerPanel->FromDIP(kModelListGapWeightToTimeDip);
	const int g_time_end   = headerPanel->FromDIP(kModelListGapTimeToEdgeDip);

	headerOuter = new wxBoxSizer(wxVERTICAL);
	headerPanel->SetSizer(headerOuter);

	headerSizer = new wxBoxSizer(wxHORIZONTAL);

	{
		m_header_select_all_cb = new FileListBitmapCheckBox(headerPanel, wxID_ANY);
		m_header_select_all_cb->SetToolTip(_L("Select all"));
		m_header_select_all_cb->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
			SelectAllRows(m_header_select_all_cb->GetValue());
			e.Skip();
		});

		hdr_gap_cb = new wxPanel(headerPanel);
		hdr_gap_cb->SetBackgroundColour(header_bg);
		hdr_gap_cb->SetMinSize(wxSize(g_cb_thumb, header_row_h));
		hdr_gap_cb->SetMaxSize(wxSize(g_cb_thumb, header_row_h));

		hdr_gap_tn = new wxPanel(headerPanel);
		hdr_gap_tn->SetBackgroundColour(header_bg);
		hdr_gap_tn->SetMinSize(wxSize(g_thumb_name, header_row_h));
		hdr_gap_tn->SetMaxSize(wxSize(g_thumb_name, header_row_h));

		nameHeaderWrap = new wxPanel(headerPanel);
		nameHeaderWrap->SetBackgroundColour(header_bg);
		nameHeaderRow = new wxBoxSizer(wxHORIZONTAL);
		nameHeaderWrap->SetSizer(nameHeaderRow);
		{
			nameHeader = new wxStaticText(nameHeaderWrap, wxID_ANY, _L("Name"));
			nameHeader->SetFont(file_list_header_label_font());
			mountToggleWrap = new wxPanel(nameHeaderWrap);
			mountToggleWrap->SetBackgroundColour(header_bg);
			mountVBox = new wxBoxSizer(wxVERTICAL);
			mountHBox = new wxBoxSizer(wxHORIZONTAL);
			mountToggleWrap->SetSizer(mountVBox);
			{
				m_btn_mount_local = new Button(mountToggleWrap, _L("Local"), wxEmptyString, 0, 0, wxID_ANY);
				m_btn_mount_usb = new Button(mountToggleWrap, _L("USB"), wxEmptyString, 0, 0, wxID_ANY);
				m_btn_mount_local->SetCanFocus(false);
				m_btn_mount_usb->SetCanFocus(false);
				m_btn_mount_local->SetMinSize(mountToggleWrap->FromDIP(wxSize(44, 22)));
				m_btn_mount_local->SetMaxSize(mountToggleWrap->FromDIP(wxSize(56, 24)));
				m_btn_mount_usb->SetMinSize(mountToggleWrap->FromDIP(wxSize(44, 22)));
				m_btn_mount_usb->SetMaxSize(mountToggleWrap->FromDIP(wxSize(56, 24)));
				apply_model_file_mount_button_chrome(m_btn_mount_local);
				apply_model_file_mount_button_chrome(m_btn_mount_usb);
				//cj_3
				{
					const double corner_r = static_cast<double>(m_btn_mount_local->FromDIP(4));
					m_btn_mount_local->SetCornerRadius(corner_r);
					m_btn_mount_usb->SetCornerRadius(corner_r);
				}
				m_btn_mount_local->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
					if (m_mount_filter == ModelListMountFilter::Local)
						return;
					m_mount_filter = ModelListMountFilter::Local;
					applyMountFilterVisibility();
					updateMountFilterButtonStyles();
				});
				m_btn_mount_usb->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
					if (m_mount_filter == ModelListMountFilter::Usb)
						return;
					m_mount_filter = ModelListMountFilter::Usb;
					applyMountFilterVisibility();
					updateMountFilterButtonStyles();
				});
			}
			mountHBox->Add(m_btn_mount_local, 0, wxALIGN_CENTER_VERTICAL);
			mountHBox->Add(m_btn_mount_usb, 0, wxALIGN_CENTER_VERTICAL);
			mountVBox->AddStretchSpacer(1);
			mountVBox->Add(mountHBox, 0, wxALIGN_CENTER_HORIZONTAL);
			mountVBox->AddStretchSpacer(1);
			mountToggleWrap->SetMinSize(wxSize(headerPanel->FromDIP(92), header_row_h));
			mountToggleWrap->SetMaxSize(wxSize(headerPanel->FromDIP(104), header_row_h));
			hdr_toggle_to_weight_pad = new wxPanel(nameHeaderWrap);
			hdr_toggle_to_weight_pad->SetBackgroundColour(header_bg);
			const int g_hdr_toggle_wt = headerPanel->FromDIP(kModelListNameHeaderToggleToWeightPadDip);
			hdr_toggle_to_weight_pad->SetMinSize(wxSize(g_hdr_toggle_wt, header_row_h));
			hdr_toggle_to_weight_pad->SetMaxSize(wxSize(g_hdr_toggle_wt, header_row_h));
		}
		nameHeaderRow->Add(nameHeader, 0, wxALIGN_CENTER_VERTICAL);
		nameHeaderRow->AddStretchSpacer(1);
		nameHeaderRow->AddSpacer(headerPanel->FromDIP(50));
		nameHeaderRow->Add(mountToggleWrap, 0, wxALIGN_CENTER_VERTICAL);
		nameHeaderRow->Add(hdr_toggle_to_weight_pad, 0, wxALIGN_CENTER_VERTICAL);
		nameHeaderWrap->SetMaxSize(wxSize(headerPanel->FromDIP(kModelListNameColMaxDip), header_row_h));

		const int col_weight_w    = headerPanel->FromDIP(kModelListWeightColDip);
		const int col_time_w      = headerPanel->FromDIP(kModelListTimeColDip);
		const int w_time_bundle_w = col_weight_w + g_wt_mid + col_time_w + g_time_end;

		wt_header_panel = new wxPanel(headerPanel);
		wt_header_panel->SetBackgroundColour(header_bg);
		hdr_wt_h = new wxBoxSizer(wxHORIZONTAL);
		hdr_wt_v = new wxBoxSizer(wxVERTICAL);
		wt_header_panel->SetSizer(hdr_wt_v);
		{
			weightHeader = new wxStaticText(wt_header_panel, wxID_ANY, _L("filament weight"));
			weightHeader->SetMinSize(wxSize(col_weight_w, -1));
			weightHeader->SetMaxSize(wxSize(col_weight_w, -1));
			weightHeader->SetFont(file_list_header_label_font());
			timeHeader = new wxStaticText(wt_header_panel, wxID_ANY, _L("pre time"));
			timeHeader->SetMinSize(wxSize(col_time_w, -1));
			timeHeader->SetMaxSize(wxSize(col_time_w, -1));
			timeHeader->SetFont(file_list_header_label_font());
		}
		hdr_wt_h->Add(weightHeader, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxRIGHT, g_wt_mid);
		hdr_wt_h->Add(timeHeader, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxRIGHT, g_time_end);
		hdr_wt_v->AddStretchSpacer(1);
		hdr_wt_v->Add(hdr_wt_h, 0, wxALIGN_CENTER_VERTICAL);
		hdr_wt_v->AddStretchSpacer(1);
		wt_header_panel->SetMinSize(wxSize(w_time_bundle_w, header_row_h));
		wt_header_panel->SetMaxSize(wxSize(w_time_bundle_w, header_row_h));

		const int sep_h = headerPanel->FromDIP(kFileListRowSeparatorDip);
		m_header_sep = new wxPanel(headerPanel);
		m_header_sep->SetBackgroundColour(file_list_row_separator_colour());
		m_header_sep->SetMinSize(wxSize(-1, sep_h));
		m_header_sep->SetMaxSize(wxSize(-1, sep_h));
	}

	headerSizer->Add(m_header_select_all_cb, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_lead);
	headerSizer->Add(hdr_gap_cb, 0, wxALIGN_CENTER_VERTICAL);
	headerSizer->AddSpacer(thumb_hdr);
	headerSizer->Add(hdr_gap_tn, 0, wxALIGN_CENTER_VERTICAL);
	headerSizer->Add(nameHeaderWrap, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxRIGHT, g_name_wt);
	if (g_name_wt > 0)
		headerSizer->AddSpacer(g_name_wt);
	headerSizer->Add(wt_header_panel, 0, wxALIGN_CENTER_VERTICAL);

	headerOuter->Add(headerSizer, 1, wxEXPAND);
	headerOuter->Add(m_header_sep, 0, wxEXPAND);

	updateMountFilterButtonStyles();
	return headerPanel;
}

//cj_3
void DeviceModelListCtrl::syncHeaderSelectAllState()
{
	if (!m_header_select_all_cb || m_items.empty()) {
		if (m_header_select_all_cb)
			m_header_select_all_cb->SetValue(false);
		return;
	}
	bool any_visible = false;
	bool all = true;
	for (DeviceModelItem* item : m_items) {
		if (!item->IsShown())
			continue;
		any_visible = true;
		if (!item->IsSelected()) {
			all = false;
			break;
		}
	}
	m_header_select_all_cb->SetValue(all && any_visible);
}

//cj_3
void DeviceModelListCtrl::postSelectionAggregateEvent()
{
    int selectNum = 0;
    for (DeviceModelItem* item : m_items) {
        if (item->IsShown() && item->IsSelected()) {
            ++selectNum;
        }
    }
    if (selectNum > 2) {
        selectNum = 2;
    }
    wxCommandEvent e(wxEVT_CHECKBOX, GetId());
    e.SetEventObject(this);
    e.SetInt(selectNum);
    wxPostEvent(this, e);
}

void DeviceModelListCtrl::onCheckChange(wxCommandEvent& e)
{
	int selectNum = 0;
 	for (DeviceModelItem* item : m_items) {
		if (item->IsShown() && item->IsSelected()) {
			++selectNum;
		}
	}
	if (selectNum > 2) {
		selectNum = 2;
	}
	//cj_3 wxEVT_TOGGLEBUTTON does not propagate like wxEVT_CHECKBOX; StatusPanel listens for wxEVT_CHECKBOX on this list.
	if (!m_bulk_updating_selection) {
		syncHeaderSelectAllState();
		postSelectionAggregateEvent();
	}
	e.SetInt(selectNum);
	//cj_3 Let toggle's wxEVT_TOGGLEBUTTON handler run (update() bitmap); without Skip the row toggle looks dead.
	e.Skip();
}

//cj_3
void DeviceModelListCtrl::SelectAllRows(bool selected)
{
	m_bulk_updating_selection = true;
	for (DeviceModelItem* item : m_items) {
		if (item->IsShown())
			item->SetSelected(selected);
	}
	m_bulk_updating_selection = false;
	if (m_header_select_all_cb)
		m_header_select_all_cb->SetValue(selected && hasAnyVisibleItem());
	syncHeaderSelectAllState();
	postSelectionAggregateEvent();
}

//cj_3
void DeviceModelListCtrl::AfterRowDownloadUiChanged()
{
	syncHeaderSelectAllState();
	postSelectionAggregateEvent();
}

//cj_3
bool DeviceModelListCtrl::HasActiveFileDownload() const
{
	for (DeviceModelItem* it : m_items) {
		if (it && it->isFileDownloading())
			return true;
	}
	return false;
}

void DeviceModelListCtrl::AddItem(const wxString& name,
	const wxBitmap& image,
	double weight,
	const wxString& estimatedTime)
{
	DeviceModelItem* item = new DeviceModelItem(this, name, image, weight, estimatedTime);
	m_items.push_back(item);
	m_mainSizer->Add(item, 0, wxEXPAND);

	//cj_3 Bind on the toggle itself: wxEVT_TOGGLEBUTTON is not propagated to the row panel by default.
	if (FileListBitmapCheckBox* cb = item->getCheckBox())
		cb->Bind(wxEVT_TOGGLEBUTTON, &DeviceModelListCtrl::onCheckChange, this);
	item->SetMinSize(wxSize(-1, FromDIP(kFileListRowHeightDip) + FromDIP(kFileListRowSeparatorDip)));
	//cj_3
	item->setLocalCopyExists(local_file_exists_in_download_dir(item->GetName()));
	item->sync_row_surface_colours(file_list_panel_background(), file_list_primary_text_colour());
	syncNewItemMountVisibility(item);
	m_mainSizer->Layout();
	//cj_3
	FitInside();
}

void DeviceModelListCtrl::RemoveSelectedItems()
{
	for (auto iter = m_items.begin(); iter != m_items.end(); ) {
		if ((*iter)->IsShown() && (*iter)->IsSelected()) {
			m_mainSizer->Detach(*iter);
			(*iter)->Destroy();
			iter = m_items.erase(iter);
		} else {
			++iter;
		}
	}
	syncHeaderSelectAllState();
	postSelectionAggregateEvent();
	m_mainSizer->Layout();
	//cj_3
	FitInside();
}

void DeviceModelListCtrl::ClearAll()
{
	for (auto item : m_items) {
		m_mainSizer->Detach(item);
		item->Destroy();
	}
	m_items.clear();
	m_mount_filter = ModelListMountFilter::Local;
	updateMountFilterButtonStyles();
	//cj_3
	if (m_header_select_all_cb)
		m_header_select_all_cb->SetValue(false);
	postSelectionAggregateEvent();
	m_mainSizer->Layout();
	//cj_3
	FitInside();
}

std::vector<DeviceModelItem*> DeviceModelListCtrl::GetSelectedItems()
{
	std::vector<DeviceModelItem*> selected;
	for (auto item : m_items) {
		if (item->IsShown() && item->IsSelected()) {
			selected.push_back(item);
		}
	}
	return selected;
}

bool DeviceModelListCtrl::UpdateItemThumbnail(const wxString& name, const wxBitmap& image)
{
	for (auto* item : m_items) {
		if (item != nullptr && item->GetName() == name) {
			item->SetImage(image);
			item->Refresh();
			return true;
		}
	}
	return false;
}

//cj_4
bool DeviceModelListCtrl::rowMatchesMountFilter(const DeviceModelItem* item) const
{
	if (item == nullptr)
		return false;
	const bool usb = item->IsUsbStorage();
	return (m_mount_filter == ModelListMountFilter::Usb) ? usb : !usb;
}

//cj_4
bool DeviceModelListCtrl::hasAnyVisibleItem() const
{
	for (DeviceModelItem* item : m_items) {
		if (item != nullptr && item->IsShown())
			return true;
	}
	return false;
}

//cj_4
void DeviceModelListCtrl::syncNewItemMountVisibility(DeviceModelItem* item)
{
	if (item == nullptr)
		return;
	item->Show(rowMatchesMountFilter(item));
}

//cj_4
void DeviceModelListCtrl::applyMountFilterVisibility()
{
	for (DeviceModelItem* item : m_items) {
		const bool show = rowMatchesMountFilter(item);
		item->Show(show);
		if (!show)
			item->SetSelected(false);
	}
	m_mainSizer->Layout();
	FitInside();
	syncHeaderSelectAllState();
	postSelectionAggregateEvent();
}

//cj_4
void DeviceModelListCtrl::updateMountFilterButtonStyles()
{
	if (m_btn_mount_local == nullptr || m_btn_mount_usb == nullptr)
		return;
	const bool local = (m_mount_filter == ModelListMountFilter::Local);
	if (local) {
		set_mount_filter_button_selected(m_btn_mount_local);
		set_mount_filter_button_unselected(m_btn_mount_usb);
	} else {
		set_mount_filter_button_unselected(m_btn_mount_local);
		set_mount_filter_button_selected(m_btn_mount_usb);
	}
	m_btn_mount_local->Refresh();
	m_btn_mount_usb->Refresh();
}
}
}
