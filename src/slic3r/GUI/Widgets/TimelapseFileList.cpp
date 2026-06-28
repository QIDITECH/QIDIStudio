
#include "TimelapseFileList.hpp"
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
#include "../I18N.hpp"
#include "../DownloadManager.hpp"
#include "../GUI_App.hpp"
#include "StateColor.hpp"
#include "Label.hpp"
#include <functional>

namespace Slic3r::GUI {

//cj_4
wxDEFINE_EVENT(EVT_TIMELAPSE_ROW_ACTIVATE, wxCommandEvent);
wxDEFINE_EVENT(EVT_TIMELAPSE_ROW_CONTEXT, wxCommandEvent);

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

static wxColour file_list_row_separator_colour()
{
    return wxGetApp().dark_mode() ? wxColour(76, 76, 85) : wxColour(238, 238, 238);
}

//cj_3 Same as DeviceModelList::file_list_panel_background (list surface; light/dark).
static wxColour file_list_panel_background()
{
    return wxGetApp().dark_mode() ? wxColour(45, 45, 49) : wxColour(255, 255, 255);
}

//cj_3
static wxColour file_list_row_hover_background()
{
    return wxGetApp().dark_mode() ? wxColour(52, 52, 58) : wxColour(236, 242, 255);
}

// Same as DeviceModelList::file_list_primary_text_colour (header label text).
static wxColour timelapse_file_list_header_text_colour()
{
    return wxGetApp().dark_mode() ? wxColour(0xB2, 0xB3, 0xB5) : wxColour(140, 140, 140);
}

static constexpr int kFileListRowHeightDip = 35;
// Row and header bottom separator height in DIP (same role as former line_reserve).
static constexpr int kFileListRowSeparatorDip = 2;
//cj_3 Match DeviceModelList: checkbox and thumb logical DIP.
static constexpr int kFileListCheckboxDip = kFileListBitmapCheckboxScalablePxCnt;
static constexpr int kFileListThumbDip    = 18;
//cj_3 Same DIP as DeviceModelList kModelListGapCbToThumbDip (checkbox column to thumbnail).
static constexpr int kFileListGapCbToThumbDip = 10;
//cj_3 Same as DeviceModelList kModelListGapThumbToNameDip (thumbnail column to name).
static constexpr int kFileListGapThumbToNameDip = 10;
//cj_3 Same as DeviceModelList name column cap (FromDIP(350)).
static constexpr int kModelListNameColMaxDip = 350;

//cj_3 Same helpers as DeviceModelListCtrl::sync_file_list_surface_colours.
static void apply_surface_to_panels(wxWindow* root, const wxColour& bg)
{
    if (dynamic_cast<wxPanel*>(root))
        root->SetBackgroundColour(bg);
    for (wxWindow* ch : root->GetChildren())
        apply_surface_to_panels(ch, bg);
}

static void apply_fg_to_static_texts(wxWindow* root, const wxColour& fg)
{
    if (auto* st = dynamic_cast<wxStaticText*>(root))
        st->SetForegroundColour(fg);
    for (wxWindow* ch : root->GetChildren())
        apply_fg_to_static_texts(ch, fg);
}

//cj_3
// Row text colour (same as DeviceModelList primary); helpers for local file highlight in download_path.
static wxColour timelapse_file_row_text_colour()
{
	return wxGetApp().dark_mode() ? wxColour(0xB2, 0xB3, 0xB5) : wxColour(140, 140, 140);
}

//cj_3
// Same as DeviceModelList local-exists highlight (blue).
static wxColour timelapse_local_exists_text_colour()
{
	return wxGetApp().dark_mode() ? wxColour(150, 195, 255) : wxColour(95, 145, 235);
}

//cj_3 Same rules as DeviceModelList::local_file_exists_in_download_dir (subpaths under download_path).
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

//cj_4
bool timelapse_local_file_ready(const wxString& base_name)
{
	return local_file_exists_in_download_dir(base_name);
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

// Same as DeviceModelList::file_list_download_tint_colour (download progress overlay tint).
static wxColour file_list_download_tint_colour()
{
    return wxColour(95, 145, 235);
}

TimelapseFileItem::TimelapseFileItem(wxWindow* parent,
    const wxString& name,
    const wxBitmap& image,
    const wxString& size_text,
    const wxString& modified_text)
    : wxPanel(parent, wxID_ANY)
    , m_name(name)
    , m_size_text(size_text)
    , m_modified_text(modified_text)
    , m_list_host(dynamic_cast<TimelapseFileListCtrl*>(parent))
{
    const int row_h        = parent->FromDIP(kFileListRowHeightDip);
    const int row_pad      = parent->FromDIP(4);
    const int row_lead     = parent->FromDIP(10);
    const int g_cb_thumb   = parent->FromDIP(kFileListGapCbToThumbDip);
    const int g_thumb_name = parent->FromDIP(kFileListGapThumbToNameDip);
    const int thumb_sz     = parent->FromDIP(kFileListThumbDip);
    const int sep_h        = FromDIP(kFileListRowSeparatorDip);

    //cj_4 WxDeclareFirst: declarations → root sizer + SetSizer(this) → mainSizer → { build } → Add.
    wxBoxSizer* outer     = nullptr;
    wxBoxSizer* mainSizer = nullptr;
    wxBoxSizer* cell_col  = nullptr;
    wxBoxSizer* img_col   = nullptr;
    wxBoxSizer* img_row   = nullptr;
    wxBoxSizer* nameSizer = nullptr;
    wxBoxSizer* mts_v     = nullptr;
    wxBoxSizer* mts_h     = nullptr;

    SetBackgroundColour(file_list_panel_background());
    outer = new wxBoxSizer(wxVERTICAL);
    SetSizer(outer);

    mainSizer = new wxBoxSizer(wxHORIZONTAL);

    {
        m_checkbox_cell = new wxPanel(this);
        m_checkbox_cell->SetBackgroundColour(file_list_panel_background());
        cell_col = new wxBoxSizer(wxVERTICAL);
        m_checkbox_cell->SetSizer(cell_col);
        {
            m_checkbox = new FileListBitmapCheckBox(m_checkbox_cell, wxID_ANY);
            const int cb_cell_w = m_checkbox->GetSize().GetWidth();
            m_checkbox_cell->SetMinSize(wxSize(cb_cell_w, 2 * cb_cell_w + parent->FromDIP(2)));
            m_cancel_download_btn = new Button(m_checkbox_cell, wxEmptyString, "file_cancel", 0, cb_cell_w, wxID_ANY);
            m_cancel_download_btn->SetPaddingSize(wxSize(0, 0));
            m_cancel_download_btn->SetMinSize(wxSize(cb_cell_w, cb_cell_w));
            m_cancel_download_btn->SetMaxSize(wxSize(cb_cell_w, cb_cell_w));
            m_cancel_download_btn->SetBorderWidth(0);
            m_cancel_download_btn->SetCornerRadius(0);
            m_cancel_download_btn->SetToolTip(_L("Cancel download"));
            m_cancel_download_btn->Bind(wxEVT_BUTTON, &TimelapseFileItem::onCancelDownload, this);
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

        m_name_panel = new wxPanel(this);
        nameSizer = new wxBoxSizer(wxVERTICAL);
        m_name_panel->SetSizer(nameSizer);
        {
            m_name_text = new wxStaticText(m_name_panel, wxID_ANY, name);
            m_name_text->SetFont(file_list_row_content_font());
        }
        nameSizer->AddStretchSpacer(1);
        nameSizer->Add(m_name_text, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT, 0);
        nameSizer->AddStretchSpacer(1);
        m_name_panel->SetMaxSize(wxSize(parent->FromDIP(kModelListNameColMaxDip), -1));
        m_name_panel->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
            updateNameLabelWidth();
            e.Skip();
        });

        const wxFont row_font = file_list_row_content_font();
        m_mtime_size_panel = new wxPanel(this);
        mts_v = new wxBoxSizer(wxVERTICAL);
        mts_h = new wxBoxSizer(wxHORIZONTAL);
        m_mtime_size_panel->SetSizer(mts_v);
        {
            m_mtime_label = new wxStaticText(m_mtime_size_panel, wxID_ANY, modified_text);
            m_size_label = new wxStaticText(m_mtime_size_panel, wxID_ANY, size_text);
            m_mtime_label->SetFont(row_font);
            m_size_label->SetFont(row_font);
            m_mtime_label->SetMinSize(wxSize(parent->FromDIP(120), -1));
            m_mtime_label->SetMaxSize(wxSize(parent->FromDIP(120), -1));
            m_size_label->SetMinSize(wxSize(parent->FromDIP(72), -1));
            m_size_label->SetMaxSize(wxSize(parent->FromDIP(72), -1));
        }
        mts_h->Add(m_mtime_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_pad);
        mts_h->Add(m_size_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_pad);
        mts_v->AddStretchSpacer(1);
        mts_v->Add(mts_h, 0, wxALIGN_CENTER_VERTICAL);
        mts_v->AddStretchSpacer(1);
        const int meta_bundle_w = parent->FromDIP(120) + parent->FromDIP(72) + 3 * row_pad;
        m_mtime_size_panel->SetMinSize(wxSize(meta_bundle_w, row_h));
        m_mtime_size_panel->SetMaxSize(wxSize(meta_bundle_w, row_h));

        m_row_sep = new wxPanel(this);
        m_row_sep->SetMinSize(wxSize(-1, sep_h));
        m_row_sep->SetMaxSize(wxSize(-1, sep_h));
    }

    mainSizer->Add(m_checkbox_cell, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_lead);
    mainSizer->Add(m_gap_cb_thumb_panel, 0, wxALIGN_CENTER_VERTICAL);
    mainSizer->Add(m_image_panel, 0, wxALIGN_CENTER_VERTICAL);
    mainSizer->Add(m_gap_thumb_name_panel, 0, wxALIGN_CENTER_VERTICAL);
    mainSizer->Add(m_name_panel, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxRIGHT, 0);
    mainSizer->Add(m_mtime_size_panel, 0, wxALIGN_CENTER_VERTICAL);

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
    //cj_3 One-shot paint of row surfaces and labels (same pattern as DeviceModelItem ctor + applyListLabelColour).
    sync_row_surface_colours(file_list_panel_background(), timelapse_file_row_text_colour());

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxPaintDC dc(this);
        const wxRect rect = GetClientRect();
        dc.SetBrush(wxBrush(GetBackgroundColour()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(rect);
    });

    CallAfter([this]() { updateNameLabelWidth(); });
    //cj_4
    wire_row_mouse_for_play_and_context();
    //cj_3
    bind_row_hand_cursor_recursive(this);
    bind_row_hover_recursive(this);
}

wxPoint TimelapseFileItem::pointer_pos_in_row(wxWindow* event_src, const wxPoint& src_client_pos) const
{
    if (!event_src || event_src == this)
        return src_client_pos;
    return ScreenToClient(event_src->ClientToScreen(src_client_pos));
}

void TimelapseFileItem::on_row_left_down(wxMouseEvent& e)
{
    const wxPoint pos_in_row = pointer_pos_in_row(dynamic_cast<wxWindow*>(e.GetEventObject()), e.GetPosition());
    if (m_checkbox_cell && m_checkbox_cell->GetRect().Contains(pos_in_row)) {
        e.Skip();
        return;
    }
    if (isFileDownloading()) {
        e.Skip();
        return;
    }
    wxCommandEvent ev(EVT_TIMELAPSE_ROW_ACTIVATE, wxID_ANY);
    ev.SetString(m_name);
    wxPostEvent(GetParent(), ev);
}

void TimelapseFileItem::on_row_right_down(wxMouseEvent& e)
{
    (void)e;
    if (isFileDownloading())
        return;
    wxCommandEvent ev(EVT_TIMELAPSE_ROW_CONTEXT, wxID_ANY);
    ev.SetString(m_name);
    wxPostEvent(GetParent(), ev);
}

void TimelapseFileItem::wire_row_mouse_for_play_and_context()
{
    std::function<void(wxWindow*)> go = [&](wxWindow* w) {
        if (!w || w == m_checkbox_cell)
            return;
        w->Bind(wxEVT_LEFT_DOWN, &TimelapseFileItem::on_row_left_down, this);
        w->Bind(wxEVT_RIGHT_DOWN, &TimelapseFileItem::on_row_right_down, this);
        for (wxWindow* c : w->GetChildren()) {
            if (c == m_checkbox_cell)
                continue;
            go(c);
        }
    };
    go(this);
}

//cj_3
void TimelapseFileItem::bind_row_hand_cursor_recursive(wxWindow* w)
{
    if (!w)
        return;
    w->SetCursor(wxCURSOR_HAND);
    for (wxWindow* ch : w->GetChildren())
        bind_row_hand_cursor_recursive(ch);
}

//cj_3
void TimelapseFileItem::bind_row_hover_recursive(wxWindow* w)
{
    if (!w)
        return;
    w->Bind(wxEVT_ENTER_WINDOW, &TimelapseFileItem::on_mouse_enter, this);
    w->Bind(wxEVT_LEAVE_WINDOW, &TimelapseFileItem::on_mouse_leave, this);
    w->Bind(wxEVT_MOTION, &TimelapseFileItem::on_mouse_motion, this);
    for (wxWindow* ch : w->GetChildren())
        bind_row_hover_recursive(ch);
}

void TimelapseFileItem::on_mouse_enter(wxMouseEvent& e)
{
    if (!isFileDownloading())
        set_hover(true);
    e.Skip();
}

void TimelapseFileItem::on_mouse_leave(wxMouseEvent& e)
{
    const wxPoint mp = wxGetMousePosition();
    if (!GetScreenRect().Contains(mp))
        set_hover(false);
    e.Skip();
}

void TimelapseFileItem::on_mouse_motion(wxMouseEvent& e)
{
    (void)e;
    if (!isFileDownloading())
        set_hover(true);
}

void TimelapseFileItem::set_hover(bool h)
{
    if (m_hover == h)
        return;
    m_hover = h;
    if (!m_list_host)
        return;
    if (h)
        m_list_host->set_hovered_row(this);
    else
        m_list_host->clear_hovered_row_if(this);
}

TimelapseFileItem::~TimelapseFileItem()
{
    if (m_download_tint_panel) {
        m_download_tint_panel->Destroy();
        m_download_tint_panel = nullptr;
    }
}

//cj_3
void TimelapseFileItem::setLocalCopyExists(bool exists)
{
    m_local_copy_exists = exists;
}

//cj_3
void TimelapseFileItem::apply_row_text_colours(const wxColour& base_fg)
{
    const wxColour fg = m_local_copy_exists ? timelapse_local_exists_text_colour() : base_fg;
    if (m_name_text)
        m_name_text->SetForegroundColour(fg);
    if (m_mtime_label)
        m_mtime_label->SetForegroundColour(fg);
    if (m_size_label)
        m_size_label->SetForegroundColour(fg);
    if (m_cancel_download_btn)
        m_cancel_download_btn->SetBackgroundColor(StateColor(GetBackgroundColour()));
}

//cj_3 Align with DeviceModelItem::sync_row_surface_colours; hover matches ModelFileListRow.
void TimelapseFileItem::sync_row_surface_colours(const wxColour& bg_base, const wxColour& fg, bool hover)
{
    const wxColour bg = hover ? file_list_row_hover_background() : bg_base;
    SetBackgroundColour(bg);
    if (m_checkbox_cell)
        m_checkbox_cell->SetBackgroundColour(bg);
    if (m_image_panel)
        m_image_panel->SetBackgroundColour(bg);
    if (m_gap_cb_thumb_panel)
        m_gap_cb_thumb_panel->SetBackgroundColour(bg);
    if (m_gap_thumb_name_panel)
        m_gap_thumb_name_panel->SetBackgroundColour(bg);
    if (m_name_panel)
        m_name_panel->SetBackgroundColour(bg);
    if (m_mtime_size_panel)
        m_mtime_size_panel->SetBackgroundColour(bg);
    if (m_row_sep)
        m_row_sep->SetBackgroundColour(file_list_row_separator_colour());
    apply_row_text_colours(fg);
    //cj_3
    Refresh();
}

void TimelapseFileItem::updateNameLabelWidth()
{
    if (!m_name_text || !m_name_panel)
        return;
    wxClientDC dc(m_name_text);
    dc.SetFont(m_name_text->GetFont());
    const int hpad = m_name_panel->FromDIP(4);
    int w = m_name_panel->GetClientSize().GetWidth() - 2 * hpad;
    if (w < 1)
        w = 1;
    m_name_text->SetLabel(wxControl::Ellipsize(m_name, dc, wxELLIPSIZE_END, w));
}

void TimelapseFileItem::updateDownloadTintGeometry()
{
    if (!m_download_tint_panel) {
        return;
    }
    if (m_download_fraction < 0.f) {
        if (m_download_tint_panel->IsShown())
            m_download_tint_panel->Hide();
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
    //cj_3 Tint span = image column left through mtime+size panel right (not full row width).
    int exclude_left = 0;
    if (m_image_panel) {
        exclude_left = std::clamp(m_image_panel->GetRect().GetLeft(), 0, sz.GetWidth());
    } else if (m_checkbox_cell) {
        exclude_left = std::clamp(
            m_checkbox_cell->GetRect().GetRight() + 1 + FromDIP(kFileListGapCbToThumbDip), 0, sz.GetWidth());
    }
    int content_right = sz.GetWidth();
    if (m_mtime_size_panel) {
        content_right = std::clamp(m_mtime_size_panel->GetRect().GetRight() + 1, exclude_left, sz.GetWidth());
    }
    const int content_w = std::max(0, content_right - exclude_left);
    const int h = std::max(0, sz.GetHeight() - line_reserve);
    const float ff = std::clamp(m_download_fraction, 0.f, 1.f);
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

void TimelapseFileItem::setFileDownloadTaskId(const std::string& task_id)
{
    m_file_download_task_id = task_id;
}

void TimelapseFileItem::beginFileDownload(const std::string& task_id)
{
    set_hover(false);
    m_file_download_task_id = task_id;
    m_download_fraction = 0.f;
    updateDownloadChrome();
    updateDownloadTintGeometry();
    if (auto* list = dynamic_cast<TimelapseFileListCtrl*>(GetParent()))
        list->AfterRowDownloadUiChanged();
}

void TimelapseFileItem::updateDownloadChrome()
{
    const bool dl = isFileDownloading();
    if (m_checkbox) {
        m_checkbox->Show(!dl);
        if (dl)
            m_checkbox->SetValue(false);
    }
    if (m_cancel_download_btn) {
        m_cancel_download_btn->Show(dl);
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

void TimelapseFileItem::onCancelDownload(wxCommandEvent& e)
{
    (void)e;
    if (!m_file_download_task_id.empty())
        DownloadManager::getInstance().cancelFileDownload(m_file_download_task_id);
    setDownloadProgressFraction(-1.f);
}

bool TimelapseFileItem::IsSelected() const
{
    if (isFileDownloading())
        return false;
    return m_checkbox && m_checkbox->GetValue();
}

void TimelapseFileItem::SetSelected(bool selected)
{
    if (isFileDownloading()) {
        if (m_checkbox)
            m_checkbox->SetValue(false);
        return;
    }
    if (m_checkbox)
        m_checkbox->SetValue(selected);
}

void TimelapseFileItem::refresh_local_copy_label_from_disk()
{
    m_local_copy_exists = local_file_exists_in_download_dir(m_name);
    apply_row_text_colours(timelapse_file_row_text_colour());
    Refresh();
    if (wxWindow* p = GetParent())
        p->Refresh();
}

void TimelapseFileItem::setDownloadProgressFraction(float fraction01)
{
    const bool was_downloading = (m_download_fraction >= 0.f);
    m_download_fraction = fraction01;
    if (fraction01 < 0.f)
        m_file_download_task_id.clear();
    const bool now_downloading = (m_download_fraction >= 0.f);
    //cj_3
    if (was_downloading != now_downloading)
        updateDownloadChrome();
    updateDownloadTintGeometry();
    //cj_3
    if (was_downloading && fraction01 < 0.f) {
        refresh_local_copy_label_from_disk();
        if (auto* list = dynamic_cast<TimelapseFileListCtrl*>(GetParent()))
            list->AfterRowDownloadUiChanged();
        //cj_3
        wxGetApp().CallAfter([this]() { refresh_local_copy_label_from_disk(); });
    }
}

TimelapseFileListCtrl::TimelapseFileListCtrl(wxWindow* parent, wxWindowID id)
    : wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxVSCROLL)
{
    SetMinSize(wxSize(602, 915));
    SetMaxSize(wxSize(602, 915));
    m_mainSizer = new wxBoxSizer(wxVERTICAL);
    m_header_panel = CreateHeaderPanel();
    m_mainSizer->Add(m_header_panel, 0, wxEXPAND);
    SetSizer(m_mainSizer);
    //cj_4
    SetScrollRate(10,  90);
    //cj_3
    EnableScrolling(false, true);
    //cj_3 Hide bottom horizontal scrollbar when virtual width slightly exceeds client (wx still reserves it otherwise).
    ShowScrollbars(wxScrollbarVisibility::wxSHOW_SB_NEVER, wxScrollbarVisibility::wxSHOW_SB_DEFAULT);

    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& e) {
        sync_file_list_surface_colours();
        e.Skip();
    });
    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        e.Skip();
        for (TimelapseFileItem* item : m_items)
            item->updateDownloadTintGeometry();
    });
    //cj_4
    Bind(EVT_TIMELAPSE_ROW_ACTIVATE, [this](wxCommandEvent& ev) { wxPostEvent(GetParent(), ev); });
    Bind(EVT_TIMELAPSE_ROW_CONTEXT, [this](wxCommandEvent& ev) { wxPostEvent(GetParent(), ev); });
    Bind(wxEVT_SCROLLWIN_TOP, &TimelapseFileListCtrl::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_BOTTOM, &TimelapseFileListCtrl::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_LINEUP, &TimelapseFileListCtrl::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_LINEDOWN, &TimelapseFileListCtrl::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_PAGEUP, &TimelapseFileListCtrl::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_PAGEDOWN, &TimelapseFileListCtrl::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_THUMBTRACK, &TimelapseFileListCtrl::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &TimelapseFileListCtrl::on_download_overlay_scroll, this);
    if (wxWindow* top = wxGetTopLevelParent(this)) {
        top->Bind(wxEVT_MOVE, &TimelapseFileListCtrl::on_download_overlay_top_move, this);
        top->Bind(wxEVT_SIZE, &TimelapseFileListCtrl::on_download_overlay_top_size, this);
    }
    sync_file_list_surface_colours();
}

//cj_3 Mirror DeviceModelListCtrl::sync_file_list_surface_colours.
void TimelapseFileListCtrl::sync_file_list_surface_colours()
{
    const wxColour bg = file_list_panel_background();
    const wxColour fg = timelapse_file_row_text_colour();
    SetBackgroundColour(bg);
    if (m_header_panel) {
        m_header_panel->SetBackgroundColour(bg);
        apply_surface_to_panels(m_header_panel, bg);
        apply_fg_to_static_texts(m_header_panel, fg);
    }
    if (m_header_sep)
        m_header_sep->SetBackgroundColour(file_list_row_separator_colour());
    for (TimelapseFileItem* it : m_items) {
        it->setLocalCopyExists(local_file_exists_in_download_dir(it->GetName()));
        it->sync_row_surface_colours(bg, fg, it == m_hovered_row);
    }
    Refresh();
}

//cj_3
void TimelapseFileListCtrl::refresh_local_exist_flags_from_disk()
{
    const wxColour bg = file_list_panel_background();
    const wxColour fg = timelapse_file_row_text_colour();
    for (TimelapseFileItem* it : m_items) {
        it->setLocalCopyExists(local_file_exists_in_download_dir(it->GetName()));
        it->sync_row_surface_colours(bg, fg, it == m_hovered_row);
    }
    Refresh();
}

//cj_3
void TimelapseFileListCtrl::set_hovered_row(TimelapseFileItem* row)
{
    const wxColour bg = file_list_panel_background();
    const wxColour fg = timelapse_file_row_text_colour();
    if (m_hovered_row && m_hovered_row != row) {
        m_hovered_row->sync_row_surface_colours(bg, fg, false);
        m_hovered_row = nullptr;
    }
    m_hovered_row = row;
    if (m_hovered_row)
        m_hovered_row->sync_row_surface_colours(bg, fg, true);
}

//cj_3
void TimelapseFileListCtrl::clear_hovered_row_if(TimelapseFileItem* row)
{
    if (!row || m_hovered_row != row)
        return;
    const wxColour bg = file_list_panel_background();
    const wxColour fg = timelapse_file_row_text_colour();
    m_hovered_row->sync_row_surface_colours(bg, fg, false);
    m_hovered_row = nullptr;
}

TimelapseFileListCtrl::~TimelapseFileListCtrl()
{
    if (wxWindow* top = wxGetTopLevelParent(this)) {
        top->Unbind(wxEVT_MOVE, &TimelapseFileListCtrl::on_download_overlay_top_move, this);
        top->Unbind(wxEVT_SIZE, &TimelapseFileListCtrl::on_download_overlay_top_size, this);
    }
}

void TimelapseFileListCtrl::on_download_overlay_scroll(wxScrollWinEvent& e)
{
    for (TimelapseFileItem* item : m_items)
        item->updateDownloadTintGeometry();
    e.Skip();
}

void TimelapseFileListCtrl::on_download_overlay_top_move(wxMoveEvent& e)
{
    for (TimelapseFileItem* item : m_items)
        item->updateDownloadTintGeometry();
    e.Skip();
}

void TimelapseFileListCtrl::on_download_overlay_top_size(wxSizeEvent& e)
{
    for (TimelapseFileItem* item : m_items)
        item->updateDownloadTintGeometry();
    e.Skip();
}

wxPanel* TimelapseFileListCtrl::CreateHeaderPanel()
{
    //cj_4 WxDeclareFirst: declarations → headerPanel → headerOuter + SetSizer → headerSizer → { build } → Add.
    wxPanel*      headerPanel     = nullptr;
    wxBoxSizer*   headerOuter     = nullptr;
    wxBoxSizer*   headerSizer     = nullptr;
    wxPanel*      hdr_gap_cb      = nullptr;
    wxPanel*      hdr_gap_tn      = nullptr;
    wxPanel*      nameHeaderWrap  = nullptr;
    wxBoxSizer*   nameHeaderVBox  = nullptr;
    wxStaticText* nameHeader      = nullptr;
    wxPanel*      metaHeaderWrap  = nullptr;
    wxStaticText* mtimeHeader     = nullptr;
    wxStaticText* sizeHeader      = nullptr;
    wxBoxSizer*   metaHdrVBox     = nullptr;
    wxBoxSizer*   metaHdrHBox     = nullptr;

    headerPanel = new wxPanel(this);
    const wxColour header_bg = file_list_panel_background();
    headerPanel->SetBackgroundColour(header_bg);
    const int header_row_h = headerPanel->FromDIP(kFileListRowHeightDip);
    headerPanel->SetMinSize(wxSize(-1, header_row_h));
    const int row_pad      = headerPanel->FromDIP(4);
    const int row_lead     = headerPanel->FromDIP(10);
    const int g_cb_thumb   = headerPanel->FromDIP(kFileListGapCbToThumbDip);
    const int g_thumb_name = headerPanel->FromDIP(kFileListGapThumbToNameDip);
    const int thumb_hdr    = headerPanel->FromDIP(kFileListThumbDip);

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
        nameHeaderVBox = new wxBoxSizer(wxVERTICAL);
        nameHeaderWrap->SetSizer(nameHeaderVBox);
        {
            nameHeader = new wxStaticText(nameHeaderWrap, wxID_ANY, _L("Name"));
            nameHeader->SetFont(file_list_header_label_font());
            nameHeader->SetForegroundColour(timelapse_file_list_header_text_colour());
        }
        nameHeaderVBox->AddStretchSpacer(1);
        nameHeaderVBox->Add(nameHeader, 0, wxALIGN_LEFT);
        nameHeaderVBox->AddStretchSpacer(1);
        nameHeaderWrap->SetMaxSize(wxSize(headerPanel->FromDIP(kModelListNameColMaxDip), -1));

        metaHeaderWrap = new wxPanel(headerPanel);
        metaHeaderWrap->SetBackgroundColour(header_bg);
        metaHdrVBox = new wxBoxSizer(wxVERTICAL);
        metaHdrHBox = new wxBoxSizer(wxHORIZONTAL);
        metaHeaderWrap->SetSizer(metaHdrVBox);
        {
            mtimeHeader = new wxStaticText(metaHeaderWrap, wxID_ANY, _L("Last modified"));
            mtimeHeader->SetMinSize(wxSize(headerPanel->FromDIP(120), -1));
            mtimeHeader->SetMaxSize(wxSize(headerPanel->FromDIP(120), -1));
            mtimeHeader->SetFont(file_list_header_label_font());
            mtimeHeader->SetForegroundColour(timelapse_file_list_header_text_colour());
            sizeHeader = new wxStaticText(metaHeaderWrap, wxID_ANY, _L("File size"));
            sizeHeader->SetMinSize(wxSize(headerPanel->FromDIP(72), -1));
            sizeHeader->SetMaxSize(wxSize(headerPanel->FromDIP(72), -1));
            sizeHeader->SetFont(file_list_header_label_font());
            sizeHeader->SetForegroundColour(timelapse_file_list_header_text_colour());
        }
        metaHdrHBox->Add(mtimeHeader, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_pad);
        metaHdrHBox->Add(sizeHeader, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_pad);
        metaHdrVBox->AddStretchSpacer(1);
        metaHdrVBox->Add(metaHdrHBox, 0, wxALIGN_CENTER_VERTICAL);
        metaHdrVBox->AddStretchSpacer(1);
        const int meta_bundle_w = headerPanel->FromDIP(120) + headerPanel->FromDIP(72) + 3 * row_pad;
        metaHeaderWrap->SetMinSize(wxSize(meta_bundle_w, header_row_h));
        metaHeaderWrap->SetMaxSize(wxSize(meta_bundle_w, header_row_h));

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
    headerSizer->Add(nameHeaderWrap, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxRIGHT, 0);
    headerSizer->Add(metaHeaderWrap, 0, wxALIGN_CENTER_VERTICAL);

    headerOuter->Add(headerSizer, 1, wxEXPAND);
    headerOuter->Add(m_header_sep, 0, wxEXPAND);

    return headerPanel;
}

//cj_3
void TimelapseFileListCtrl::syncHeaderSelectAllState()
{
    if (!m_header_select_all_cb || m_items.empty()) {
        if (m_header_select_all_cb)
            m_header_select_all_cb->SetValue(false);
        return;
    }
    bool all = true;
    for (TimelapseFileItem* item : m_items) {
        if (!item->IsSelected()) {
            all = false;
            break;
        }
    }
    m_header_select_all_cb->SetValue(all);
}

//cj_3
void TimelapseFileListCtrl::postSelectionAggregateEvent()
{
    int selectNum = 0;
    for (TimelapseFileItem* item : m_items) {
        if (item->IsSelected()) {
            ++selectNum;
        }
    }
    wxCommandEvent e(wxEVT_CHECKBOX, GetId());
    e.SetEventObject(this);
    e.SetInt(selectNum);
    wxPostEvent(this, e);
}

void TimelapseFileListCtrl::onCheckChange(wxCommandEvent& e)
{
    int selectNum = 0;
    for (TimelapseFileItem* item : m_items) {
        if (item->IsSelected())
            ++selectNum;
    }
    if (selectNum > 2)
        selectNum = 2;
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
void TimelapseFileListCtrl::SelectAllRows(bool selected)
{
    m_bulk_updating_selection = true;
    for (TimelapseFileItem* item : m_items) {
        item->SetSelected(selected);
    }
    m_bulk_updating_selection = false;
    if (m_header_select_all_cb)
        m_header_select_all_cb->SetValue(selected && !m_items.empty());
    syncHeaderSelectAllState();
    postSelectionAggregateEvent();
}

//cj_3
void TimelapseFileListCtrl::AfterRowDownloadUiChanged()
{
    const wxColour bg = file_list_panel_background();
    const wxColour fg = timelapse_file_row_text_colour();
    for (TimelapseFileItem* it : m_items)
        it->sync_row_surface_colours(bg, fg, it == m_hovered_row);
    syncHeaderSelectAllState();
    postSelectionAggregateEvent();
}

//cj_3
bool TimelapseFileListCtrl::HasActiveFileDownload() const
{
    for (TimelapseFileItem* it : m_items) {
        if (it && it->isFileDownloading())
            return true;
    }
    return false;
}

void TimelapseFileListCtrl::AddItem(const wxString& name, const wxBitmap& image, const wxString& size_text, const wxString& modified_text)
{
    TimelapseFileItem* item = new TimelapseFileItem(this, name, image, size_text, modified_text);
    m_items.push_back(item);
    m_mainSizer->Add(item, 0, wxEXPAND);
    //cj_3 Bind on the toggle itself: wxEVT_TOGGLEBUTTON is not propagated to the row panel by default.
    if (FileListBitmapCheckBox* cb = item->getCheckBox())
        cb->Bind(wxEVT_TOGGLEBUTTON, &TimelapseFileListCtrl::onCheckChange, this);
    item->SetMinSize(wxSize(-1, FromDIP(kFileListRowHeightDip) + FromDIP(kFileListRowSeparatorDip)));
    item->setLocalCopyExists(local_file_exists_in_download_dir(name));
    item->sync_row_surface_colours(file_list_panel_background(), timelapse_file_row_text_colour());
    m_mainSizer->Layout();
    //cj_3
    FitInside();
}

void TimelapseFileListCtrl::RemoveSelectedItems()
{
    for (auto iter = m_items.begin(); iter != m_items.end();) {
        if ((*iter)->IsSelected()) {
            if (m_hovered_row == *iter)
                m_hovered_row = nullptr;
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

//cj_3
void TimelapseFileListCtrl::RemoveItems(const std::vector<TimelapseFileItem*>& rows)
{
    if (rows.empty())
        return;
    for (TimelapseFileItem* row : rows) {
        if (!row)
            continue;
        auto it = std::find(m_items.begin(), m_items.end(), row);
        if (it == m_items.end())
            continue;
        if (m_hovered_row == *it)
            m_hovered_row = nullptr;
        m_mainSizer->Detach(*it);
        (*it)->Destroy();
        m_items.erase(it);
    }
    syncHeaderSelectAllState();
    postSelectionAggregateEvent();
    m_mainSizer->Layout();
    FitInside();
}

void TimelapseFileListCtrl::ClearAll()
{
    m_hovered_row = nullptr;
    for (auto* item : m_items) {
        m_mainSizer->Detach(item);
        item->Destroy();
    }
    m_items.clear();
    //cj_3
    if (m_header_select_all_cb)
        m_header_select_all_cb->SetValue(false);
    postSelectionAggregateEvent();
    m_mainSizer->Layout();
    //cj_3
    FitInside();
}

std::vector<TimelapseFileItem*> TimelapseFileListCtrl::GetSelectedItems()
{
    std::vector<TimelapseFileItem*> selected;
    for (auto* item : m_items) {
        if (item->IsSelected())
            selected.push_back(item);
    }
    return selected;
}

//cj_4
TimelapseFileItem* TimelapseFileListCtrl::FindItemByName(const wxString& name) const
{
    for (TimelapseFileItem* it : m_items) {
        if (it && it->GetName() == name)
            return it;
    }
    return nullptr;
}

//cj_4
void TimelapseFileListCtrl::SelectOnlyItem(TimelapseFileItem* item)
{
    m_bulk_updating_selection = true;
    for (TimelapseFileItem* it : m_items) {
        if (it)
            it->SetSelected(it == item);
    }
    m_bulk_updating_selection = false;
    syncHeaderSelectAllState();
    postSelectionAggregateEvent();
}

bool TimelapseFileListCtrl::UpdateItemThumbnail(const wxString& name, const wxBitmap& image)
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

} // namespace Slic3r::GUI
