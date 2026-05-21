
#include "ModelFileListView.hpp"
#include "FileListDownloadTintFrame.hpp"
#include "FileListBitmapCheckBox.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../DownloadManager.hpp"
#include "StateColor.hpp"
#include "Label.hpp"
#include "../StatusPanel.hpp"

#include <wx/stattext.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/filename.h>
#include <wx/popupwin.h>
#include <algorithm>
#include <cmath>

namespace Slic3r::GUI {

wxDEFINE_EVENT(EVT_MODEL_FILE_LIST_COMMAND, wxCommandEvent);

namespace {

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

static wxColour file_list_row_separator_colour()
{
    return wxGetApp().dark_mode() ? wxColour(76, 76, 85) : wxColour(238, 238, 238);
}

static wxColour file_list_panel_background()
{
    return wxGetApp().dark_mode() ? wxColour(45, 45, 49) : wxColour(255, 255, 255);
}

static wxColour file_list_primary_text_colour()
{
    return wxGetApp().dark_mode() ? wxColour(0xB2, 0xB3, 0xB5) : wxColour(140, 140, 140);
}

//cj_4
static wxColour file_list_row_hover_background()
{
    return wxGetApp().dark_mode() ? wxColour(52, 52, 58) : wxColour(236, 242, 255);
}

//cj_4 Idle face for Local/UFD toggle; matches row separator tone so dark theme does not flash #EEE on refresh.
static wxColour mount_filter_button_idle_background()
{
    return file_list_row_separator_colour();
}

static void apply_model_file_mount_button_chrome(Button* button)
{
    if (button == nullptr)
        return;
    //cj_4
    const wxColour idle_bg = mount_filter_button_idle_background();
    StateColor add_btn_bg(
        std::pair<wxColour, int>(wxColour(0, 66, 255), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(116, 168, 255), StateColor::Hovered),
        std::pair<wxColour, int>(idle_bg, StateColor::Normal));
    //cj_4
    StateColor btn_text_color(std::pair<wxColour, int>(file_list_primary_text_colour(), StateColor::Normal));
    button->SetBackgroundColor(add_btn_bg);
    button->SetBorderWidth(0);
    //cj_3
    button->SetFont(Label::Body_13);
    button->SetTextColor(btn_text_color);
    //cj_4 MSW StaticBox clears the buffer with GetBackgroundColour(); use panel bg so dark mode never shows RGB(238,238,238).
    button->SetBackgroundColour(file_list_panel_background());
}

static void set_mount_filter_button_unselected(Button* b)
{
    if (b == nullptr)
        return;
    //cj_4
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

//cj_3
// Button::SetFont (via apply_model_file_mount_button_chrome) runs messureSize(); re-apply equal min/max after chrome.
static void fix_model_file_mount_toggle_button_layout(Button* local_btn, Button* ufd_btn)
{
    if (local_btn == nullptr || ufd_btn == nullptr)
        return;
    const wxSize box = local_btn->FromDIP(wxSize(62, 20));
    for (Button* b : { local_btn, ufd_btn }) {
        b->SetMinSize(box);
        b->SetMaxSize(box);
    }
}

static wxFont file_list_row_content_font()
{
#ifdef __WXOSX_MAC__
    return ::Label::Body_10;
#else
    return wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC"));
#endif
}

static wxFont file_list_header_label_font()
{
#ifdef __WXOSX_MAC__
    return ::Label::Body_11;
#else
    return wxFont(11, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, wxT("HarmonyOS Sans SC"));
#endif
}

static wxColour file_list_local_exists_text_colour()
{
    return wxGetApp().dark_mode() ? wxColour(150, 195, 255) : wxColour(95, 145, 235);
}

//cj_4
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

static constexpr int kFileListRowHeightDip         = 35;
static constexpr int kFileListRowSeparatorDip      = 2;
static constexpr int kFileListThumbDip             = 18;
static constexpr int kModelListWeightColDip        = 93;
static constexpr int kModelListTimeColDip          = 78;
static constexpr int kModelListNameColMaxDip       = 350;
static constexpr int kModelListNameHeaderToggleBandMaxDip = 260;
static constexpr int kModelListNameHeaderToggleToWeightPadDip =
    kModelListNameColMaxDip - kModelListNameHeaderToggleBandMaxDip;
static constexpr int kModelListGapActionToThumbDip = 10;
static constexpr int kModelListGapThumbToNameDip   = 10;
static constexpr int kModelListGapNameToWeightDip  = 0;
static constexpr int kModelListGapWeightToTimeDip  = 3;
static constexpr int kModelListGapTimeToEdgeDip    = 2;
//cj_4 Match FileListBitmapCheckBox column width so idle rows align with the header (no empty strip before the thumbnail).
static constexpr int kModelListActionColDip        = kFileListBitmapCheckboxScalablePxCnt;
static constexpr int k_download_tint_alpha         = 120;

static wxColour file_list_download_tint_colour()
{
    return wxColour(95, 145, 235);
}

static wxPanel* make_gap_panel(wxWindow* parent, int width_px, int row_h_px, const wxColour& bg)
{
    wxPanel* p = new wxPanel(parent);
    p->SetBackgroundColour(bg);
    p->SetMinSize(wxSize(width_px, row_h_px));
    p->SetMaxSize(wxSize(width_px, row_h_px));
    return p;
}

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

//cj_4
static wxColour file_list_context_menu_disabled_text_colour()
{
    return wxGetApp().dark_mode() ? wxColour(90, 90, 95) : wxColour(180, 180, 180);
}

//cj_4 Native wxMenu cannot use a hand cursor on MSW; wxDialog::ShowModal blocks all other clicks — use a transient popup instead.
class FileListContextMenuPopup : public wxPopupTransientWindow
{
public:
    FileListContextMenuPopup(wxWindow* parent, ModelFileListView* host, const wxString& storage_path, bool downloading, bool reveal_enabled);

    void PopupAt(const wxPoint& screen_origin);

protected:
    void OnDismiss() override;

private:
    void on_escape(wxKeyEvent& e);

    ModelFileListView* m_host{ nullptr };
    wxString           m_storage_path;
};

FileListContextMenuPopup::FileListContextMenuPopup(wxWindow* parent, ModelFileListView* host, const wxString& storage_path, bool downloading, bool reveal_enabled)
    : wxPopupTransientWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
    , m_host(host)
    , m_storage_path(storage_path)
{
    SetCursor(wxCURSOR_HAND);
    const wxColour bg     = file_list_panel_background();
    const wxColour fg     = file_list_primary_text_colour();
    const wxColour fg_dis = file_list_context_menu_disabled_text_colour();
    SetBackgroundColour(bg);
    Bind(wxEVT_CHAR_HOOK, &FileListContextMenuPopup::on_escape, this);

    auto* vs        = new wxBoxSizer(wxVERTICAL);
    const int row_h = FromDIP(30);
    const int hpad  = FromDIP(12);
    const int min_w = FromDIP(200);

    auto add_row = [&](const wxString& text, ModelFileListCommandType cmd, bool enabled) {
        auto* row = new wxPanel(this);
        row->SetBackgroundColour(bg);
        row->SetMinSize(wxSize(min_w, row_h));
        auto* st = new wxStaticText(row, wxID_ANY, text);
        st->SetFont(Label::Body_15);
        st->SetForegroundColour(enabled ? fg : fg_dis);
        auto* hs = new wxBoxSizer(wxHORIZONTAL);
        hs->AddSpacer(hpad);
        hs->Add(st, 0, wxALIGN_CENTER_VERTICAL);
        hs->AddSpacer(hpad);
        row->SetSizer(hs);
        if (enabled) {
            row->SetCursor(wxCURSOR_HAND);
            st->SetCursor(wxCURSOR_HAND);
            auto activate = [this, cmd](wxMouseEvent&) {
                ModelFileListView* h = m_host;
                const wxString     p = m_storage_path;
                if (h)
                    h->post_list_command(cmd, p);
                Dismiss();
            };
            row->Bind(wxEVT_LEFT_DOWN, activate);
            st->Bind(wxEVT_LEFT_DOWN, activate);
            const wxColour base = bg;
            //cj_4 Moving from wxPanel onto child wxStaticText sends LEAVE on the panel; only clear when pointer left the row rect.
            auto enter_h = [row](wxMouseEvent&) {
                row->SetBackgroundColour(file_list_row_hover_background());
                row->Refresh();
            };
            auto leave_h = [row, base](wxMouseEvent&) {
                const wxPoint mp = wxGetMousePosition();
                if (!row->GetScreenRect().Contains(mp)) {
                    row->SetBackgroundColour(base);
                    row->Refresh();
                }
            };
            row->Bind(wxEVT_ENTER_WINDOW, enter_h);
            row->Bind(wxEVT_LEAVE_WINDOW, leave_h);
            st->Bind(wxEVT_ENTER_WINDOW, enter_h);
            st->Bind(wxEVT_LEAVE_WINDOW, leave_h);
            //cj_4 Motion over label: child gets events first; ENTER/LEAVE can be skipped on some builds.
            st->Bind(wxEVT_MOTION, [enter_h](wxMouseEvent& e) {
                enter_h(e);
                e.Skip();
            });
            row->Bind(wxEVT_MOTION, [enter_h](wxMouseEvent& e) {
                enter_h(e);
                e.Skip();
            });
        } else {
            row->SetCursor(wxCURSOR_ARROW);
            st->SetCursor(wxCURSOR_ARROW);
        }
        vs->Add(row, 0, wxEXPAND);
    };

    add_row(_L("Print"), ModelFileListCommandType::Print, !downloading);
    add_row(_L("Download"), ModelFileListCommandType::Download, !downloading);
    add_row(_L("Delete"), ModelFileListCommandType::Delete, !downloading);
    add_row(_L("Open Folder"), ModelFileListCommandType::RevealLocal, !downloading && reveal_enabled);

    SetSizer(vs);
    Fit();
}

void FileListContextMenuPopup::PopupAt(const wxPoint& screen_origin)
{
    const wxSize sz   = GetSize();
    wxPoint      pos  = screen_origin;
    const int    didx = GetParent() ? wxDisplay::GetFromWindow(GetParent()) : wxDisplay::GetFromPoint(screen_origin);
    if (didx != wxNOT_FOUND) {
        const wxRect geom = wxDisplay(didx).GetClientArea();
        if (pos.x + sz.x > geom.GetRight())
            pos.x = std::max(geom.GetLeft(), geom.GetRight() - sz.x);
        if (pos.y + sz.y > geom.GetBottom())
            pos.y = std::max(geom.GetTop(), geom.GetBottom() - sz.y);
        if (pos.x < geom.GetLeft())
            pos.x = geom.GetLeft();
        if (pos.y < geom.GetTop())
            pos.y = geom.GetTop();
    }
    //cj_4 wxPopupWindow is top-level (wxNonOwnedWindow): SetPosition is always screen coordinates. Never ScreenToClient(parent).
    SetPosition(pos);
    Popup();
}

void FileListContextMenuPopup::OnDismiss()
{
    wxPopupTransientWindow::OnDismiss();
    Destroy();
}

void FileListContextMenuPopup::on_escape(wxKeyEvent& e)
{
    if (e.GetKeyCode() == WXK_ESCAPE)
        Dismiss();
    else
        e.Skip();
}

} // namespace

class ModelFileListRow : public wxPanel
{
public:
    ModelFileListRow(ModelFileListView* host,
        wxWindow*           parent,
        const wxString&     storage_path,
        const wxBitmap&     image,
        double              weight,
        const wxString&     estimated_time);

    ~ModelFileListRow() override;

    wxString get_storage_path() const { return m_storage_path; }
    bool     is_usb_storage() const { return m_is_usb; }
    bool     is_file_downloading() const { return m_download_fraction >= 0.f; }

    void set_local_copy_exists(bool exists);
    void sync_row_surface_colours(const wxColour& bg_base, const wxColour& fg, bool hover);
    void apply_label_colour(const wxColour& fg);
    void set_hover(bool h);
    bool get_hover() const { return m_hover; }

    void begin_file_download(const std::string& task_id);
    void set_download_progress_fraction(float fraction01);
    void update_download_chrome();
    void update_download_tint_geometry();
    void on_cancel_download(wxCommandEvent& e);
    //cj_4
    void set_thumbnail(const wxBitmap& bmp);
    void finish_download_ui();

private:
    void bind_row_mouse_recursive(wxWindow* w);
    void on_mouse_enter(wxMouseEvent& e);
    void on_mouse_leave(wxMouseEvent& e);
    //cj_4
    void on_mouse_motion(wxMouseEvent& e);
    void on_left_down(wxMouseEvent& e);
    void on_right_down(wxMouseEvent& e);
    //cj_4 screen_pt: top-left of menu should match pointer (use ClientToScreen from event source).
    void show_row_context_menu(const wxPoint& screen_pt);
    void update_name_label_width();
    void refresh_local_copy_label_from_disk();

    ModelFileListView* m_host{ nullptr };
    wxString           m_storage_path;
    wxString           m_display_name;
    bool               m_is_usb{ false };
    double             m_weight{ 0. };
    wxString           m_estimated_time;
    bool               m_hover{ false };
    bool               m_local_copy_exists{ false };
    float              m_download_fraction{ -1.f };
    std::string        m_file_download_task_id;

    wxPanel*              m_action_cell{ nullptr };
    wxPanel*              m_gap_action_to_thumb{ nullptr };
    Button*               m_cancel_download_btn{ nullptr };
    wxPanel*              m_image_panel{ nullptr };
    wxStaticBitmap*       m_image{ nullptr };
    wxPanel*              m_gap_thumb_name{ nullptr };
    wxPanel*              m_name_panel{ nullptr };
    wxStaticText*         m_name_text{ nullptr };
    wxPanel*              m_weight_time_panel{ nullptr };
    wxStaticText*         m_weight_text{ nullptr };
    wxStaticText*         m_time_text{ nullptr };
    wxPanel*              m_row_sep{ nullptr };
    FileListDownloadTintPanel* m_download_tint_panel{ nullptr };
};

ModelFileListRow::ModelFileListRow(ModelFileListView* host,
    wxWindow*           parent,
    const wxString&     storage_path,
    const wxBitmap&     image,
    double              weight,
    const wxString&     estimated_time)
    : wxPanel(parent, wxID_ANY)
    , m_host(host)
    , m_storage_path(storage_path)
    , m_display_name(model_file_list_display_label(storage_path))
    , m_is_usb(model_file_list_path_is_usb(storage_path))
    , m_weight(weight)
    , m_estimated_time(estimated_time)
{
    const int row_lead         = parent->FromDIP(10);
    const int thumb_sz         = parent->FromDIP(kFileListThumbDip);
    const int row_h            = parent->FromDIP(kFileListRowHeightDip);
    const int sep_h            = parent->FromDIP(kFileListRowSeparatorDip);
    const int g_action_thumb   = parent->FromDIP(kModelListGapActionToThumbDip);
    const int g_thumb_name     = parent->FromDIP(kModelListGapThumbToNameDip);
    const int g_name_wt        = parent->FromDIP(kModelListGapNameToWeightDip);
    const int g_wt_mid         = parent->FromDIP(kModelListGapWeightToTimeDip);
    const int g_time_end       = parent->FromDIP(kModelListGapTimeToEdgeDip);
    const int col_weight_w     = parent->FromDIP(kModelListWeightColDip);
    const int col_time_w       = parent->FromDIP(kModelListTimeColDip);
    const int w_time_bundle_w  = col_weight_w + g_wt_mid + col_time_w + g_time_end;
    const int action_col       = parent->FromDIP(kModelListActionColDip);

    wxBoxSizer* outer     = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(outer);

    m_action_cell = new wxPanel(this);
    m_action_cell->SetBackgroundColour(file_list_panel_background());
    wxBoxSizer* action_sz = new wxBoxSizer(wxVERTICAL);
    m_action_cell->SetSizer(action_sz);
    const int cb_cell_w = action_col;
    //cj_3
    // Keep the action column width when idle so showing the cancel button does not shift the thumbnail and text.
    m_action_cell->SetMinSize(wxSize(cb_cell_w, row_h));
    m_action_cell->SetMaxSize(wxSize(cb_cell_w, row_h));
    m_cancel_download_btn = new Button(m_action_cell, wxEmptyString, "file_cancel", 0, cb_cell_w, wxID_ANY);
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
    m_cancel_download_btn->Bind(wxEVT_BUTTON, &ModelFileListRow::on_cancel_download, this);
    m_cancel_download_btn->SetCanFocus(false);
    m_cancel_download_btn->Hide();
    action_sz->AddStretchSpacer(1);
    action_sz->Add(m_cancel_download_btn, 0, wxALIGN_CENTER_HORIZONTAL);
    action_sz->AddStretchSpacer(1);

    m_gap_action_to_thumb = make_gap_panel(this, g_action_thumb, row_h, file_list_panel_background());

    m_image_panel = new wxPanel(this);
    m_image_panel->SetBackgroundColour(file_list_panel_background());
    wxBoxSizer* img_col = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* img_row = new wxBoxSizer(wxHORIZONTAL);
    m_image_panel->SetSizer(img_col);
    m_image = new wxStaticBitmap(m_image_panel, wxID_ANY, image);
    m_image->SetMinSize(wxSize(thumb_sz, thumb_sz));
    m_image->SetMaxSize(wxSize(thumb_sz, thumb_sz));
    img_row->Add(m_image, 0, wxALIGN_CENTER_VERTICAL);
    img_col->AddStretchSpacer(1);
    img_col->Add(img_row, 0, wxALIGN_CENTER_HORIZONTAL);
    img_col->AddStretchSpacer(1);
    m_image_panel->SetMinSize(wxSize(thumb_sz, row_h));
    m_image_panel->SetMaxSize(wxSize(thumb_sz, row_h));

    m_gap_thumb_name = make_gap_panel(this, g_thumb_name, row_h, file_list_panel_background());

    m_name_panel = new wxPanel(this);
    m_name_panel->SetBackgroundColour(file_list_panel_background());
    wxBoxSizer* nameSizer = new wxBoxSizer(wxVERTICAL);
    m_name_panel->SetSizer(nameSizer);
    m_name_text = new wxStaticText(m_name_panel, wxID_ANY, m_display_name);
    m_name_text->SetFont(file_list_row_content_font());
    nameSizer->AddStretchSpacer(1);
    nameSizer->Add(m_name_text, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT, 0);
    nameSizer->AddStretchSpacer(1);
    m_name_panel->SetMaxSize(wxSize(parent->FromDIP(kModelListNameColMaxDip), -1));
    m_name_panel->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        update_name_label_width();
        e.Skip();
    });

    wxString weightStr = wxString::Format("%.2fg", weight);
    wxFont   row_font  = file_list_row_content_font();
    wxString time_compact(estimated_time);
    time_compact.Replace(" ", wxEmptyString, true);
    time_compact.Replace("\t", wxEmptyString, true);

    m_weight_time_panel = new wxPanel(this);
    m_weight_time_panel->SetBackgroundColour(file_list_panel_background());
    wxBoxSizer* wt_h = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* wt_v = new wxBoxSizer(wxVERTICAL);
    m_weight_time_panel->SetSizer(wt_v);
    m_weight_text = new wxStaticText(m_weight_time_panel, wxID_ANY, weightStr);
    m_weight_text->SetFont(row_font);
    m_weight_text->SetMinSize(wxSize(col_weight_w, -1));
    m_weight_text->SetMaxSize(wxSize(col_weight_w, -1));
    m_time_text = new wxStaticText(m_weight_time_panel, wxID_ANY, time_compact);
    m_time_text->SetFont(row_font);
    m_time_text->SetMinSize(wxSize(col_time_w, -1));
    m_time_text->SetMaxSize(wxSize(col_time_w, -1));
    wt_h->Add(m_weight_text, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxRIGHT, g_wt_mid);
    wt_h->Add(m_time_text, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxRIGHT, g_time_end);
    wt_v->AddStretchSpacer(1);
    wt_v->Add(wt_h, 0, wxALIGN_CENTER_VERTICAL);
    wt_v->AddStretchSpacer(1);
    m_weight_time_panel->SetMinSize(wxSize(w_time_bundle_w, row_h));
    m_weight_time_panel->SetMaxSize(wxSize(w_time_bundle_w, row_h));

    m_row_sep = new wxPanel(this);
    m_row_sep->SetBackgroundColour(file_list_row_separator_colour());
    m_row_sep->SetMinSize(wxSize(-1, sep_h));
    m_row_sep->SetMaxSize(wxSize(-1, sep_h));

    //cj_4 Same as DeviceModelList: left inset on the first column, not a separate lead panel.
    mainSizer->Add(m_action_cell, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_lead);
    mainSizer->Add(m_gap_action_to_thumb, 0, wxALIGN_CENTER_VERTICAL);
    mainSizer->Add(m_image_panel, 0, wxALIGN_CENTER_VERTICAL);
    mainSizer->Add(m_gap_thumb_name, 0, wxALIGN_CENTER_VERTICAL);
    mainSizer->Add(m_name_panel, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxRIGHT, g_name_wt);
    mainSizer->Add(m_weight_time_panel, 0, wxALIGN_CENTER_VERTICAL);

    outer->Add(mainSizer, 1, wxEXPAND);
    outer->Add(m_row_sep, 0, wxEXPAND);

    m_download_tint_panel = new FileListDownloadTintPanel(this, file_list_download_tint_colour(), k_download_tint_alpha);
    m_download_tint_panel->Hide();
    m_download_tint_panel->Lower();
    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        update_download_tint_geometry();
        e.Skip();
    });

    SetMinSize(wxSize(-1, row_h + sep_h));
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_local_copy_exists = local_file_exists_in_download_dir(m_storage_path);
    apply_label_colour(file_list_primary_text_colour());
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxPaintDC dc(this);
        const wxRect rect = GetClientRect();
        dc.SetBrush(wxBrush(GetBackgroundColour()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(rect);
    });

    bind_row_mouse_recursive(this);
    //cj_4 Entire row uses hand cursor (children override with their own if needed).
    SetCursor(wxCURSOR_HAND);
    CallAfter([this]() { update_name_label_width(); });
}

ModelFileListRow::~ModelFileListRow()
{
    if (m_download_tint_panel) {
        m_download_tint_panel->Destroy();
        m_download_tint_panel = nullptr;
    }
}

void ModelFileListRow::bind_row_mouse_recursive(wxWindow* w)
{
    w->Bind(wxEVT_ENTER_WINDOW, &ModelFileListRow::on_mouse_enter, this);
    w->Bind(wxEVT_LEAVE_WINDOW, &ModelFileListRow::on_mouse_leave, this);
    //cj_4 ENTER/LEAVE alone miss updates when moving quickly between child widgets; motion keeps hover in sync.
    w->Bind(wxEVT_MOTION, &ModelFileListRow::on_mouse_motion, this);
    w->Bind(wxEVT_LEFT_DOWN, &ModelFileListRow::on_left_down, this);
    w->Bind(wxEVT_RIGHT_DOWN, &ModelFileListRow::on_right_down, this);
    //cj_4
    w->SetCursor(wxCURSOR_HAND);
    for (wxWindow* ch : w->GetChildren())
        bind_row_mouse_recursive(ch);
}

void ModelFileListRow::on_mouse_enter(wxMouseEvent& e)
{
    if (!is_file_downloading())
        set_hover(true);
    e.Skip();
}

void ModelFileListRow::on_mouse_leave(wxMouseEvent& e)
{
    const wxPoint mp = wxGetMousePosition();
    if (!GetScreenRect().Contains(mp))
        set_hover(false);
    e.Skip();
}

//cj_4
void ModelFileListRow::on_mouse_motion(wxMouseEvent& e)
{
    (void)e;
    if (!is_file_downloading())
        set_hover(true);
}

void ModelFileListRow::on_left_down(wxMouseEvent& e)
{
    if (is_file_downloading() && m_cancel_download_btn && m_cancel_download_btn->IsShown()) {
        wxWindow* obj = dynamic_cast<wxWindow*>(e.GetEventObject());
        if (obj && (obj == m_cancel_download_btn || m_cancel_download_btn->IsDescendant(obj))) {
            e.Skip();
            return;
        }
    }
    wxWindow* src = dynamic_cast<wxWindow*>(e.GetEventObject());
    const wxPoint scr = src ? src->ClientToScreen(e.GetPosition()) : ClientToScreen(e.GetPosition());
    show_row_context_menu(scr);
}

void ModelFileListRow::on_right_down(wxMouseEvent& e)
{
    wxWindow* src = dynamic_cast<wxWindow*>(e.GetEventObject());
    const wxPoint scr = src ? src->ClientToScreen(e.GetPosition()) : ClientToScreen(e.GetPosition());
    show_row_context_menu(scr);
}

void ModelFileListRow::show_row_context_menu(const wxPoint& screen_pt)
{
    const bool dl = is_file_downloading();
    //cj_4
    auto* pop = new FileListContextMenuPopup(this, m_host, m_storage_path, dl, m_local_copy_exists);
    pop->PopupAt(screen_pt);
}

void ModelFileListRow::set_local_copy_exists(bool exists)
{
    m_local_copy_exists = exists;
}

void ModelFileListRow::apply_label_colour(const wxColour& fg)
{
    const wxColour use_fg = m_local_copy_exists ? file_list_local_exists_text_colour() : fg;
    if (m_name_text)
        m_name_text->SetForegroundColour(use_fg);
    if (m_weight_text)
        m_weight_text->SetForegroundColour(use_fg);
    if (m_time_text)
        m_time_text->SetForegroundColour(use_fg);
    if (m_cancel_download_btn)
        m_cancel_download_btn->SetBackgroundColor(StateColor(GetBackgroundColour()));
}

void ModelFileListRow::sync_row_surface_colours(const wxColour& bg_base, const wxColour& fg, bool hover)
{
    const wxColour bg = hover ? file_list_row_hover_background() : bg_base;
    SetBackgroundColour(bg);
    if (m_action_cell)
        m_action_cell->SetBackgroundColour(bg);
    if (m_gap_action_to_thumb)
        m_gap_action_to_thumb->SetBackgroundColour(bg);
    if (m_image_panel)
        m_image_panel->SetBackgroundColour(bg);
    if (m_gap_thumb_name)
        m_gap_thumb_name->SetBackgroundColour(bg);
    if (m_name_panel)
        m_name_panel->SetBackgroundColour(bg);
    if (m_weight_time_panel)
        m_weight_time_panel->SetBackgroundColour(bg);
    if (m_cancel_download_btn)
        m_cancel_download_btn->SetBackgroundColor(StateColor(bg));
    if (m_row_sep)
        m_row_sep->SetBackgroundColour(file_list_row_separator_colour());
    apply_label_colour(fg);
    Refresh();
}

void ModelFileListRow::set_hover(bool h)
{
    if (m_hover == h)
        return;
    m_hover = h;
    if (!m_host)
        return;
    //cj_4 set_hovered_row always paints hover=true; never use it when clearing (was leaving stale highlight / wrong m_hovered_row).
    if (h)
        m_host->set_hovered_row(this);
    else
        m_host->clear_hovered_row_if(this);
}

void ModelFileListRow::update_name_label_width()
{
    if (!m_name_text || !m_name_panel)
        return;
    wxClientDC dc(m_name_text);
    dc.SetFont(m_name_text->GetFont());
    const int hpad = m_name_panel->FromDIP(4);
    int       w    = m_name_panel->GetClientSize().GetWidth() - 2 * hpad;
    if (w < 1)
        w = 1;
    m_name_text->SetLabel(wxControl::Ellipsize(m_display_name, dc, wxELLIPSIZE_END, w));
}

void ModelFileListRow::refresh_local_copy_label_from_disk()
{
    m_local_copy_exists = local_file_exists_in_download_dir(m_storage_path);
    apply_label_colour(file_list_primary_text_colour());
    Refresh();
}

void ModelFileListRow::update_download_chrome()
{
    const bool dl = is_file_downloading();
    if (m_cancel_download_btn) {
        m_cancel_download_btn->Show(dl);
    }
    const int row_h     = FromDIP(kFileListRowHeightDip);
    const int action_w  = FromDIP(kModelListActionColDip);
    const int sep_h     = FromDIP(kFileListRowSeparatorDip);
    //cj_3
    if (m_action_cell) {
        m_action_cell->SetMinSize(wxSize(action_w, row_h));
        m_action_cell->SetMaxSize(wxSize(action_w, row_h));
    }
    SetMinSize(wxSize(-1, row_h + sep_h));
    Layout();
    if (wxWindow* p = GetParent()) {
        p->Layout();
        if (auto* sw = dynamic_cast<wxScrolledWindow*>(p))
            sw->FitInside();
    }
}

void ModelFileListRow::update_download_tint_geometry()
{
    if (!m_download_tint_panel)
        return;
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
    const wxSize sz           = GetClientSize();
    const int    line_reserve = FromDIP(kFileListRowSeparatorDip);
    int          exclude_left = 0;
    if (m_image_panel)
        exclude_left = std::clamp(m_image_panel->GetRect().GetLeft(), 0, sz.GetWidth());
    int content_right = sz.GetWidth();
    if (m_weight_time_panel)
        content_right = std::clamp(m_weight_time_panel->GetRect().GetRight() + 1, exclude_left, sz.GetWidth());
    const int content_w = std::max(0, content_right - exclude_left);
    const int h         = std::max(0, sz.GetHeight() - line_reserve);
    const float ff      = std::clamp(m_download_fraction, 0.f, 1.f);
    int         w       = static_cast<int>(std::floor(static_cast<double>(content_w) * static_cast<double>(ff)));
    w                   = std::clamp(w, 0, content_w);
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

void ModelFileListRow::begin_file_download(const std::string& task_id)
{
    m_file_download_task_id = task_id;
    m_download_fraction     = 0.f;
    set_hover(false);
    update_download_chrome();
    update_download_tint_geometry();
    if (m_host)
        m_host->AfterRowDownloadUiChanged();
}

void ModelFileListRow::set_download_progress_fraction(float fraction01)
{
    m_download_fraction = fraction01;
    update_download_tint_geometry();
}

void ModelFileListRow::on_cancel_download(wxCommandEvent& e)
{
    (void)e;
    if (!m_file_download_task_id.empty())
        DownloadManager::getInstance().cancelFileDownload(m_file_download_task_id);
    m_download_fraction = -1.f;
    update_download_chrome();
    update_download_tint_geometry();
    if (m_host)
        m_host->AfterRowDownloadUiChanged();
}

void ModelFileListRow::set_thumbnail(const wxBitmap& bmp)
{
    if (m_image)
        m_image->SetBitmap(bmp);
}

void ModelFileListRow::finish_download_ui()
{
    m_file_download_task_id.clear();
    m_download_fraction = -1.f;
    update_download_chrome();
    update_download_tint_geometry();
    refresh_local_copy_label_from_disk();
    if (m_host)
        m_host->AfterRowDownloadUiChanged();
}

void ModelFileListView::post_list_command(ModelFileListCommandType cmd, const wxString& storage_path)
{
    if (!m_event_target)
        return;
    wxCommandEvent e(EVT_MODEL_FILE_LIST_COMMAND);
    e.SetInt(static_cast<int>(cmd));
    e.SetString(storage_path);
    e.SetEventObject(this);
    wxPostEvent(m_event_target, e);
}

void ModelFileListView::set_hovered_row(ModelFileListRow* row)
{
    const wxColour bg = file_list_panel_background();
    const wxColour fg = file_list_primary_text_colour();
    if (m_hovered_row && m_hovered_row != row) {
        m_hovered_row->sync_row_surface_colours(bg, fg, false);
        m_hovered_row = nullptr;
    }
    m_hovered_row = row;
    if (m_hovered_row)
        m_hovered_row->sync_row_surface_colours(bg, fg, true);
}

//cj_4
void ModelFileListView::clear_hovered_row_if(ModelFileListRow* row)
{
    if (!row || m_hovered_row != row)
        return;
    const wxColour bg = file_list_panel_background();
    const wxColour fg = file_list_primary_text_colour();
    m_hovered_row->sync_row_surface_colours(bg, fg, false);
    m_hovered_row = nullptr;
}

ModelFileListView::ModelFileListView(wxWindow* parent, wxWindow* event_target, wxWindowID id)
    : wxScrolledWindow(parent, id)
    , m_event_target(event_target)
{
    SetMinSize(wxSize(602, 915));
    SetMaxSize(wxSize(602, 915));
    m_main_sizer = new wxBoxSizer(wxVERTICAL);
    m_header_panel = create_header_panel();
    m_main_sizer->Add(m_header_panel, 0, wxEXPAND);
    SetSizer(m_main_sizer);
    //cj_4
    SetScrollRate(10, 30);
    EnableScrolling(false, true);

    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& e) {
        sync_file_list_surface_colours();
        e.Skip();
    });
    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        e.Skip();
        for (ModelFileListRow* row : m_rows)
            row->update_download_tint_geometry();
    });
    Bind(wxEVT_SCROLLWIN_TOP, &ModelFileListView::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_BOTTOM, &ModelFileListView::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_LINEUP, &ModelFileListView::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_LINEDOWN, &ModelFileListView::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_PAGEUP, &ModelFileListView::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_PAGEDOWN, &ModelFileListView::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_THUMBTRACK, &ModelFileListView::on_download_overlay_scroll, this);
    Bind(wxEVT_SCROLLWIN_THUMBRELEASE, &ModelFileListView::on_download_overlay_scroll, this);
    m_top_level_bound = wxGetTopLevelParent(this);
    if (m_top_level_bound) {
        m_top_level_bound->Bind(wxEVT_MOVE, &ModelFileListView::on_download_overlay_top_move, this);
        m_top_level_bound->Bind(wxEVT_SIZE, &ModelFileListView::on_download_overlay_top_size, this);
    }
    sync_file_list_surface_colours();
}

ModelFileListView::~ModelFileListView()
{
    if (m_top_level_bound) {
        m_top_level_bound->Unbind(wxEVT_MOVE, &ModelFileListView::on_download_overlay_top_move, this);
        m_top_level_bound->Unbind(wxEVT_SIZE, &ModelFileListView::on_download_overlay_top_size, this);
    }
}

void ModelFileListView::on_download_overlay_scroll(wxScrollWinEvent& e)
{
    for (ModelFileListRow* row : m_rows)
        row->update_download_tint_geometry();
    e.Skip();
}

void ModelFileListView::on_download_overlay_top_move(wxMoveEvent& e)
{
    for (ModelFileListRow* row : m_rows)
        row->update_download_tint_geometry();
    e.Skip();
}

void ModelFileListView::on_download_overlay_top_size(wxSizeEvent& e)
{
    for (ModelFileListRow* row : m_rows)
        row->update_download_tint_geometry();
    e.Skip();
}

wxPanel* ModelFileListView::create_header_panel()
{
    wxPanel*      headerPanel = new wxPanel(this);
    const wxColour header_bg  = file_list_panel_background();
    headerPanel->SetBackgroundColour(header_bg);
    const int header_row_h = headerPanel->FromDIP(kFileListRowHeightDip);
    headerPanel->SetMinSize(wxSize(-1, header_row_h));
    const int row_lead       = headerPanel->FromDIP(10);
    const int thumb_hdr      = headerPanel->FromDIP(kFileListThumbDip);
    const int action_col_hdr = headerPanel->FromDIP(kModelListActionColDip);
    const int g_action_thumb = headerPanel->FromDIP(kModelListGapActionToThumbDip);
    const int g_thumb_name   = headerPanel->FromDIP(kModelListGapThumbToNameDip);
    const int g_name_wt      = headerPanel->FromDIP(kModelListGapNameToWeightDip);
    const int g_wt_mid       = headerPanel->FromDIP(kModelListGapWeightToTimeDip);
    const int g_time_end     = headerPanel->FromDIP(kModelListGapTimeToEdgeDip);
    wxBoxSizer* headerOuter = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    headerPanel->SetSizer(headerOuter);

    //cj_3
    // Match ModelFileListRow: action column + gap to thumbnail (header had only the gap, shifting weight/time left).
    wxPanel* hdr_pre_thumb = new wxPanel(headerPanel);
    hdr_pre_thumb->SetBackgroundColour(header_bg);
    const int hdr_pre_thumb_w = action_col_hdr + g_action_thumb;
    hdr_pre_thumb->SetMinSize(wxSize(hdr_pre_thumb_w, header_row_h));
    hdr_pre_thumb->SetMaxSize(wxSize(hdr_pre_thumb_w, header_row_h));

    wxPanel* hdr_gap_tn = new wxPanel(headerPanel);
    hdr_gap_tn->SetBackgroundColour(header_bg);
    hdr_gap_tn->SetMinSize(wxSize(g_thumb_name, header_row_h));
    hdr_gap_tn->SetMaxSize(wxSize(g_thumb_name, header_row_h));

    wxPanel* nameHeaderWrap = new wxPanel(headerPanel);
    nameHeaderWrap->SetBackgroundColour(header_bg);
    wxBoxSizer* nameHeaderRow = new wxBoxSizer(wxHORIZONTAL);
    nameHeaderWrap->SetSizer(nameHeaderRow);
    wxStaticText* nameHeader = new wxStaticText(nameHeaderWrap, wxID_ANY, _L("Name"));
    nameHeader->SetFont(file_list_header_label_font());

    wxPanel* mountToggleWrap = new wxPanel(nameHeaderWrap);
    mountToggleWrap->SetBackgroundColour(header_bg);
    wxBoxSizer* mountVBox = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* mountHBox = new wxBoxSizer(wxHORIZONTAL);
    mountToggleWrap->SetSizer(mountVBox);
	
    m_btn_mount_local = new Button(mountToggleWrap, _L("Local"), wxEmptyString, 0, 0, wxID_ANY);
    //cj_3
    m_btn_mount_usb   = new Button(mountToggleWrap, _L("UFD"), wxEmptyString, 0, 0, wxID_ANY);
    m_btn_mount_local->SetCanFocus(false);
    m_btn_mount_usb->SetCanFocus(false);
    apply_model_file_mount_button_chrome(m_btn_mount_local);
    apply_model_file_mount_button_chrome(m_btn_mount_usb);
    fix_model_file_mount_toggle_button_layout(m_btn_mount_local, m_btn_mount_usb);
    //cj_4
    {
        const double corner_r = static_cast<double>(m_btn_mount_local->FromDIP(4));
        m_btn_mount_local->SetCornerRadius(corner_r);
        m_btn_mount_usb->SetCornerRadius(corner_r);
    }
    m_btn_mount_local->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_mount_filter == ModelListMountFilterV2::Local)
            return;
        m_mount_filter = ModelListMountFilterV2::Local;
        apply_mount_filter_visibility();
        update_mount_filter_button_styles();
    });
    m_btn_mount_usb->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (m_mount_filter == ModelListMountFilterV2::Usb)
            return;
        m_mount_filter = ModelListMountFilterV2::Usb;
        apply_mount_filter_visibility();
        update_mount_filter_button_styles();
    });
    mountHBox->Add(m_btn_mount_local, 0, wxALIGN_CENTER_VERTICAL);
    mountHBox->Add(m_btn_mount_usb, 0, wxALIGN_CENTER_VERTICAL);
    mountVBox->AddStretchSpacer(1);
    mountVBox->Add(mountHBox, 0, wxALIGN_CENTER_HORIZONTAL);
    mountVBox->AddStretchSpacer(1);
    //cj_4
    m_mount_toggle_wrap = mountToggleWrap;
    
    wxPanel* hdr_toggle_to_weight_pad = new wxPanel(nameHeaderWrap);
    hdr_toggle_to_weight_pad->SetBackgroundColour(header_bg);
    const int g_hdr_toggle_wt = headerPanel->FromDIP(kModelListNameHeaderToggleToWeightPadDip);
    hdr_toggle_to_weight_pad->SetMinSize(wxSize(g_hdr_toggle_wt, header_row_h));
    hdr_toggle_to_weight_pad->SetMaxSize(wxSize(g_hdr_toggle_wt, header_row_h));

    nameHeaderRow->Add(nameHeader, 0, wxALIGN_CENTER_VERTICAL);
    nameHeaderRow->AddStretchSpacer(1);
    nameHeaderRow->AddSpacer(headerPanel->FromDIP(50));
    nameHeaderRow->Add(mountToggleWrap, 0, wxALIGN_CENTER_VERTICAL);
    nameHeaderRow->Add(hdr_toggle_to_weight_pad, 0, wxALIGN_CENTER_VERTICAL);
    nameHeaderWrap->SetMaxSize(wxSize(headerPanel->FromDIP(kModelListNameColMaxDip), header_row_h));

    const int col_weight_w = headerPanel->FromDIP(kModelListWeightColDip);
    const int col_time_w   = headerPanel->FromDIP(kModelListTimeColDip);

    wxPanel* hdr_thumb = new wxPanel(headerPanel);
    hdr_thumb->SetBackgroundColour(header_bg);
    hdr_thumb->SetMinSize(wxSize(thumb_hdr, header_row_h));
    hdr_thumb->SetMaxSize(wxSize(thumb_hdr, header_row_h));

    wxPanel* wt_header_panel = new wxPanel(headerPanel);
    wt_header_panel->SetBackgroundColour(header_bg);
    wxBoxSizer* hdr_wt_v = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* hdr_wt_h = new wxBoxSizer(wxHORIZONTAL);
    wt_header_panel->SetSizer(hdr_wt_v);
    wxStaticText* weightHeader = new wxStaticText(wt_header_panel, wxID_ANY, _L("Filament"));
    weightHeader->SetFont(file_list_header_label_font());
    weightHeader->SetMinSize(wxSize(col_weight_w, -1));
    weightHeader->SetMaxSize(wxSize(col_weight_w, -1));
    wxStaticText* timeHeader = new wxStaticText(wt_header_panel, wxID_ANY, _L("Time"));
    timeHeader->SetFont(file_list_header_label_font());
    timeHeader->SetMinSize(wxSize(col_time_w, -1));
    timeHeader->SetMaxSize(wxSize(col_time_w, -1));
    hdr_wt_h->Add(weightHeader, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxRIGHT, g_wt_mid);
    hdr_wt_h->Add(timeHeader, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxRIGHT, g_time_end);
    hdr_wt_v->AddStretchSpacer(1);
    hdr_wt_v->Add(hdr_wt_h, 0, wxALIGN_CENTER_VERTICAL);
    hdr_wt_v->AddStretchSpacer(1);
    const int w_time_bundle_w = col_weight_w + g_wt_mid + col_time_w + g_time_end;
    wt_header_panel->SetMinSize(wxSize(w_time_bundle_w, header_row_h));
    wt_header_panel->SetMaxSize(wxSize(w_time_bundle_w, header_row_h));

    //cj_4 Match data row: wxLEFT on first block (action column + gap to thumbnail).
    headerSizer->Add(hdr_pre_thumb, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, row_lead);
    headerSizer->Add(hdr_thumb, 0, wxALIGN_CENTER_VERTICAL);
    headerSizer->Add(hdr_gap_tn, 0, wxALIGN_CENTER_VERTICAL);
    headerSizer->Add(nameHeaderWrap, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxRIGHT, g_name_wt);
    headerSizer->Add(wt_header_panel, 0, wxALIGN_CENTER_VERTICAL);

    headerOuter->Add(headerSizer, 1, wxEXPAND);
    m_header_sep = new wxPanel(headerPanel);
    m_header_sep->SetBackgroundColour(file_list_row_separator_colour());
    m_header_sep->SetMinSize(wxSize(-1, headerPanel->FromDIP(kFileListRowSeparatorDip)));
    m_header_sep->SetMaxSize(wxSize(-1, headerPanel->FromDIP(kFileListRowSeparatorDip)));
    headerOuter->Add(m_header_sep, 0, wxEXPAND);

    refresh_mount_usb_toggle_visibility();
    return headerPanel;
}

bool ModelFileListView::row_matches_mount_filter(const ModelFileListRow* row) const
{
    if (!row)
        return false;
    if (m_mount_filter == ModelListMountFilterV2::Local)
        return !row->is_usb_storage();
    return row->is_usb_storage();
}

void ModelFileListView::sync_new_item_mount_visibility(ModelFileListRow* row)
{
    if (!row)
        return;
    row->Show(row_matches_mount_filter(row));
}

void ModelFileListView::apply_mount_filter_visibility()
{
    for (ModelFileListRow* row : m_rows)
        sync_new_item_mount_visibility(row);
    m_main_sizer->Layout();
    FitInside();
    Refresh();
}

void ModelFileListView::refresh_mount_usb_toggle_visibility()
{
    if (!m_btn_mount_usb || !m_btn_mount_local || !m_mount_toggle_wrap)
        return;
    //cj_4
    bool has_usb = false;
    for (ModelFileListRow* row : m_rows) {
        if (row->is_usb_storage()) {
            has_usb = true;
            break;
        }
    }
    const int header_row_h = FromDIP(35);
    if (has_usb) {
        m_mount_toggle_wrap->Show();
        m_btn_mount_local->Show();
        m_btn_mount_usb->Show();
        m_mount_toggle_wrap->SetMinSize(wxSize(FromDIP(162), header_row_h));
        m_mount_toggle_wrap->SetMaxSize(wxSize(FromDIP(162), header_row_h));
    } else {
        if (m_mount_filter == ModelListMountFilterV2::Usb) {
            m_mount_filter = ModelListMountFilterV2::Local;
            apply_mount_filter_visibility();
        }
        m_mount_toggle_wrap->Hide();
    }
    update_mount_filter_button_styles();
    if (m_header_panel)
        m_header_panel->Layout();
    Layout();
    FitInside();
    Refresh();
}

void ModelFileListView::update_mount_filter_button_styles()
{
    if (!m_btn_mount_local || !m_btn_mount_usb)
        return;
    if (!m_mount_toggle_wrap || !m_mount_toggle_wrap->IsShown())
        return;
    const bool usb_shown = m_btn_mount_usb->IsShown();
    if (m_mount_filter == ModelListMountFilterV2::Local) {
        set_mount_filter_button_selected(m_btn_mount_local);
        if (usb_shown)
            set_mount_filter_button_unselected(m_btn_mount_usb);
    } else {
        set_mount_filter_button_unselected(m_btn_mount_local);
        if (usb_shown)
            set_mount_filter_button_selected(m_btn_mount_usb);
    }
}

void ModelFileListView::ClearAll()
{
    for (ModelFileListRow* row : m_rows) {
        m_main_sizer->Detach(row);
        row->Destroy();
    }
    m_rows.clear();
    m_hovered_row = nullptr;
    m_mount_filter = ModelListMountFilterV2::Local;
    refresh_mount_usb_toggle_visibility();
    m_main_sizer->Layout();
    FitInside();
}

void ModelFileListView::AddItem(const wxString& storage_path, const wxBitmap& image, double weight, const wxString& estimated_time)
{
    auto* row = new ModelFileListRow(this, this, storage_path, image, weight, estimated_time);
    m_rows.push_back(row);
    m_main_sizer->Add(row, 0, wxEXPAND);
    sync_new_item_mount_visibility(row);
    const wxColour bg = file_list_panel_background();
    const wxColour fg = file_list_primary_text_colour();
    row->set_local_copy_exists(local_file_exists_in_download_dir(storage_path));
    row->sync_row_surface_colours(bg, fg, false);
    refresh_mount_usb_toggle_visibility();
    m_main_sizer->Layout();
    FitInside();
}

bool ModelFileListView::UpdateItemThumbnail(const wxString& storage_path, const wxBitmap& image)
{
    for (ModelFileListRow* row : m_rows) {
        if (row->get_storage_path() == storage_path) {
            row->set_thumbnail(image);
            return true;
        }
    }
    return false;
}

void ModelFileListView::RemoveItemByStoragePath(const wxString& storage_path)
{
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        ModelFileListRow* row = *it;
        if (row->get_storage_path() == storage_path) {
            if (m_hovered_row == row)
                m_hovered_row = nullptr;
            m_main_sizer->Detach(row);
            row->Destroy();
            m_rows.erase(it);
            refresh_mount_usb_toggle_visibility();
            m_main_sizer->Layout();
            FitInside();
            return;
        }
    }
}

bool ModelFileListView::HasActiveFileDownload() const
{
    for (ModelFileListRow* row : m_rows) {
        if (row->is_file_downloading())
            return true;
    }
    return false;
}

void ModelFileListView::sync_file_list_surface_colours()
{
    const wxColour bg = file_list_panel_background();
    const wxColour fg = file_list_primary_text_colour();
    SetBackgroundColour(bg);
    if (m_header_panel) {
        m_header_panel->SetBackgroundColour(bg);
        apply_surface_to_panels(m_header_panel, bg);
        apply_fg_to_static_texts(m_header_panel, fg);
    }
    if (m_header_sep)
        m_header_sep->SetBackgroundColour(file_list_row_separator_colour());
    for (ModelFileListRow* row : m_rows) {
        row->set_local_copy_exists(local_file_exists_in_download_dir(row->get_storage_path()));
        row->sync_row_surface_colours(bg, fg, row == m_hovered_row);
    }
    if (m_btn_mount_local && m_btn_mount_usb && m_mount_toggle_wrap && m_mount_toggle_wrap->IsShown()) {
        apply_model_file_mount_button_chrome(m_btn_mount_local);
        apply_model_file_mount_button_chrome(m_btn_mount_usb);
        fix_model_file_mount_toggle_button_layout(m_btn_mount_local, m_btn_mount_usb);
    }
    update_mount_filter_button_styles();
    Refresh();
}

void ModelFileListView::refresh_local_exist_flags_from_disk()
{
    const wxColour bg = file_list_panel_background();
    const wxColour fg = file_list_primary_text_colour();
    for (ModelFileListRow* row : m_rows) {
        row->set_local_copy_exists(local_file_exists_in_download_dir(row->get_storage_path()));
        row->sync_row_surface_colours(bg, fg, row == m_hovered_row);
    }
}

void ModelFileListView::AfterRowDownloadUiChanged()
{
    const wxColour bg = file_list_panel_background();
    const wxColour fg = file_list_primary_text_colour();
    for (ModelFileListRow* row : m_rows)
        row->sync_row_surface_colours(bg, fg, row == m_hovered_row);
    //cj_3
    if (m_event_target) {
        if (auto* panel = dynamic_cast<StatusBasePanel*>(m_event_target))
            panel->sync_model_file_toolbar_after_list_download_change();
    }
}

static ModelFileListRow* find_row_by_path(std::vector<ModelFileListRow*>& rows, const wxString& path)
{
    for (ModelFileListRow* row : rows) {
        if (row->get_storage_path() == path)
            return row;
    }
    return nullptr;
}

void ModelFileListView::begin_file_download_for_path(const wxString& storage_path, const std::string& task_id)
{
    ModelFileListRow* row = find_row_by_path(m_rows, storage_path);
    if (row)
        row->begin_file_download(task_id);
}

void ModelFileListView::set_download_progress_for_path(const wxString& storage_path, float fraction01)
{
    ModelFileListRow* row = find_row_by_path(m_rows, storage_path);
    if (row)
        row->set_download_progress_fraction(fraction01);
}

void ModelFileListView::end_file_download_for_path(const wxString& storage_path, bool failed)
{
    (void)failed;
    ModelFileListRow* row = find_row_by_path(m_rows, storage_path);
    if (!row)
        return;
    row->finish_download_ui();
}

} // namespace Slic3r::GUI
