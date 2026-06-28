#include "TimelapsePanelHost.hpp"
#include "TimelapseFileList.hpp"
#include "TimelapseUiEvents.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "Label.hpp"

#include <wx/display.h>
#include <wx/popupwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include <algorithm>
#include <functional>

namespace Slic3r::GUI {

namespace {

//cj_3
// Match ModelFileListView::FileListContextMenuPopup chrome (same font and colours as model file list context menu).
static wxColour file_list_panel_background()
{
    return wxGetApp().dark_mode() ? wxColour(45, 45, 49) : wxColour(255, 255, 255);
}

static wxColour file_list_primary_text_colour()
{
    return wxGetApp().dark_mode() ? wxColour(0xB2, 0xB3, 0xB5) : wxColour(140, 140, 140);
}

//cj_3
static wxColour file_list_row_hover_background()
{
    return wxGetApp().dark_mode() ? wxColour(52, 52, 58) : wxColour(236, 242, 255);
}

//cj_3
static wxColour file_list_context_menu_disabled_text_colour()
{
    return wxGetApp().dark_mode() ? wxColour(90, 90, 95) : wxColour(180, 180, 180);
}

} // namespace

namespace detail {

//cj_3
class TimelapseContextMenuPopup : public wxPopupTransientWindow
{
public:
    enum class Mode { DeleteOnlyMulti, FullFour };

    TimelapseContextMenuPopup(TimelapsePanelHost* host, TimelapseFileItem* hit, const wxString& file_name, Mode mode, bool reveal_ok = true);

    void PopupAt(const wxPoint& screen_origin);

protected:
    void OnDismiss() override;

private:
    void on_escape(wxKeyEvent& e);

    TimelapsePanelHost* m_host{ nullptr };
    TimelapseFileItem*  m_hit{ nullptr };
    wxString            m_file_name;
};

TimelapseContextMenuPopup::TimelapseContextMenuPopup(
    TimelapsePanelHost* host, TimelapseFileItem* hit, const wxString& file_name, Mode mode, bool reveal_ok)
    : wxPopupTransientWindow(host, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
    , m_host(host)
    , m_hit(hit)
    , m_file_name(file_name)
{
    SetCursor(wxCURSOR_HAND);
    const wxColour border_color = wxGetApp().dark_mode() ? wxColour(90, 90, 98) : wxColour(205, 205, 212);
    const wxColour bg     = wxGetApp().dark_mode() ? wxColour(50, 50, 55) : wxColour(243, 243, 247);
    const wxColour fg     = file_list_primary_text_colour();
    const wxColour fg_dis = file_list_context_menu_disabled_text_colour();
    SetBackgroundColour(border_color);
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    const int border_w   = FromDIP(1);
    Bind(wxEVT_CHAR_HOOK, &TimelapseContextMenuPopup::on_escape, this);
    Bind(wxEVT_PAINT, [this, border_color](wxPaintEvent&) {
        wxPaintDC dc(this);
        const wxSize sz = GetSize();
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(border_color));
        dc.DrawRectangle(0, 0, sz.x, sz.y);
    });

    auto* content = new wxPanel(this);
    content->SetBackgroundColour(bg);

    auto* vs        = new wxBoxSizer(wxVERTICAL);
    const int row_h = FromDIP(30);
    const int hpad  = FromDIP(12);
    const int min_w = FromDIP(200);

    auto add_row = [&](const wxString& text, bool enabled, const std::function<void()>& on_activate) {
        auto* row = new wxPanel(content);
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
            auto activate = [this, on_activate](wxMouseEvent&) {
                on_activate();
                Dismiss();
            };
            row->Bind(wxEVT_LEFT_DOWN, activate);
            st->Bind(wxEVT_LEFT_DOWN, activate);
            const wxColour base = bg;
            auto               enter_h = [row](wxMouseEvent&) {
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

    if (mode == Mode::DeleteOnlyMulti) {
        add_row(_L("Delete"), true, [this]() {
            wxCommandEvent del(EVT_TIMELAPSE_REQUEST_DELETE, wxID_ANY);
            if (m_host && m_host->m_event_target)
                m_host->m_event_target->ProcessWindowEvent(del);
        });
    } else {
        add_row(_L("Play"), true, [this]() {
            wxCommandEvent out(EVT_TIMELAPSE_PLAY_FILE, wxID_ANY);
            out.SetString(m_file_name);
            if (m_host && m_host->m_event_target)
                m_host->m_event_target->ProcessWindowEvent(out);
        });
        add_row(_L("Delete"), true, [this]() {
            //cj_3
            // Do not change list selection; pass target file via event string for delete UI.
            wxCommandEvent del(EVT_TIMELAPSE_REQUEST_DELETE, wxID_ANY);
            del.SetString(m_file_name);
            if (m_host && m_host->m_event_target)
                m_host->m_event_target->ProcessWindowEvent(del);
        });
        add_row(_L("Download"), true, [this]() {
            wxCommandEvent out(EVT_TIMELAPSE_DOWNLOAD_ONE, wxID_ANY);
            out.SetString(m_file_name);
            if (m_host && m_host->m_event_target)
                m_host->m_event_target->ProcessWindowEvent(out);
        });
        add_row(_L("Open Folder"), reveal_ok, [this, reveal_ok]() {
            if (!reveal_ok)
                return;
            wxCommandEvent out(EVT_TIMELAPSE_REVEAL_FILE, wxID_ANY);
            out.SetString(m_file_name);
            if (m_host && m_host->m_event_target)
                m_host->m_event_target->ProcessWindowEvent(out);
        });
    }

    content->SetSizer(vs);

    auto* outer_sizer = new wxBoxSizer(wxVERTICAL);
    outer_sizer->Add(content, 1, wxALL | wxEXPAND, border_w);
    SetSizer(outer_sizer);
    Fit();
}

void TimelapseContextMenuPopup::PopupAt(const wxPoint& screen_origin)
{
    const wxSize sz  = GetSize();
    wxPoint      pos = screen_origin;
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
    SetPosition(pos);
    Popup();
}

void TimelapseContextMenuPopup::OnDismiss()
{
    wxPopupTransientWindow::OnDismiss();
    Destroy();
}

void TimelapseContextMenuPopup::on_escape(wxKeyEvent& e)
{
    if (e.GetKeyCode() == WXK_ESCAPE)
        Dismiss();
    else
        e.Skip();
}

} // namespace detail

TimelapsePanelHost::TimelapsePanelHost(wxWindow* parent, wxWindow* event_target)
    : wxPanel(parent, wxID_ANY)
    , m_event_target(event_target)
{
    m_list = new TimelapseFileListCtrl(this);
    auto* sz = new wxBoxSizer(wxVERTICAL);
    sz->Add(m_list, 1, wxEXPAND);
    SetSizer(sz);

    m_list->Bind(EVT_TIMELAPSE_ROW_ACTIVATE, &TimelapsePanelHost::on_row_activate, this);
    m_list->Bind(EVT_TIMELAPSE_ROW_CONTEXT, &TimelapsePanelHost::on_row_context, this);
}

bool TimelapsePanelHost::HasActiveFileDownload() const
{
    return m_list && m_list->HasActiveFileDownload();
}

void TimelapsePanelHost::on_row_activate(wxCommandEvent& e)
{
    wxCommandEvent out(EVT_TIMELAPSE_PLAY_FILE, wxID_ANY);
    out.SetString(e.GetString());
    if (m_event_target)
        m_event_target->ProcessWindowEvent(out);
}

void TimelapsePanelHost::on_row_context(wxCommandEvent& e)
{
    show_context_menu(e.GetString());
}

void TimelapsePanelHost::show_context_menu(const wxString& file_name)
{
    if (!m_list || file_name.empty())
        return;
    TimelapseFileItem* hit = m_list->FindItemByName(file_name);
    if (!hit)
        return;

    const std::vector<TimelapseFileItem*> selected = m_list->GetSelectedItems();
    const int                             n        = static_cast<int>(selected.size());

    if (n >= 2) {
        if (!hit->IsSelected())
            return;
        //cj_3
        auto* pop = new detail::TimelapseContextMenuPopup(this, hit, file_name, detail::TimelapseContextMenuPopup::Mode::DeleteOnlyMulti);
        pop->PopupAt(wxGetMousePosition());
        return;
    }

    if (n == 1) {
        TimelapseFileItem* only = selected.front();
        if (hit != only || !hit->IsSelected())
            return;
    }

    const bool local_ok = timelapse_local_file_ready(file_name);
    //cj_3
    auto* pop = new detail::TimelapseContextMenuPopup(this, hit, file_name, detail::TimelapseContextMenuPopup::Mode::FullFour, local_ok);
    pop->PopupAt(wxGetMousePosition());
}

} // namespace Slic3r::GUI
