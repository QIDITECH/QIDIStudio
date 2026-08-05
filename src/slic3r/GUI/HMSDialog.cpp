#include "HMSDialog.hpp"
#include "QDSDeviceManager.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { namespace GUI {

static constexpr int kHmsRowFixedWidth = 734;   // message row fixed width (DIP)
static constexpr int kHmsSepWidth      = 698;   // separator width (DIP)

// ────────────────────────────────────────────────────────────────
// HMSIndicatorButton
// ────────────────────────────────────────────────────────────────

HMSIndicatorButton::HMSIndicatorButton(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(38, 24))
{
    //cj_5 Match title bar: dark (45,45,49) / light (245,245,248).
    SetBackgroundColour(wxGetApp().dark_mode() ? wxColour(45, 45, 49) : wxColour(245, 245, 248));
    SetCursor(wxCURSOR_HAND);

    m_img_default = create_scaled_bitmap("notification_default", this, 20);
    m_img_dot     = create_scaled_bitmap("notification_dot",     this, 20);

    m_bmp = new wxStaticBitmap(this, wxID_ANY, m_img_default);
    // Forward click from bitmap child to parent so statusbar binding fires.
    m_bmp->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxMouseEvent evt(e);
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    });
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddStretchSpacer();
    sizer->Add(m_bmp, 0, wxALIGN_CENTER_HORIZONTAL);
    sizer->AddStretchSpacer();
    SetSizer(sizer);
}

void HMSIndicatorButton::set_has_errors(bool has)
{
    if (m_has_errors == has) return;
    m_has_errors = has;
    if (m_bmp) {
        m_bmp->SetBitmap(has ? m_img_dot : m_img_default);
    }
}

// ────────────────────────────────────────────────────────────────
// HMSDialog
// ────────────────────────────────────────────────────────────────

HMSDialog::HMSDialog(wxWindow* parent, const std::vector<QDSDeviceErrorData>& items, const std::string& machine_type)
    : wxDialog(parent, wxID_ANY, wxEmptyString,
               wxDefaultPosition, wxDefaultSize,
               wxBORDER_NONE | wxFRAME_NO_TASKBAR)
    , m_items(items)
    , m_machine_type(machine_type)
{
    SetSize(800, 673);
    CenterOnParent();

    const bool dark = wxGetApp().dark_mode();
    const wxColour page_bg   = dark ? wxColour(45, 45, 49)  : wxColour(245, 245, 248);
    const wxColour card_bg   = dark ? wxColour(0, 0, 0)     : wxColour(255, 255, 255);
    const wxColour sep_col   = dark ? wxColour(80, 80, 85)  : wxColour(220, 220, 225);
    const wxColour empty_fg  = dark ? wxColour(140, 140, 148) : wxColour(160, 160, 165);
    const wxColour text_fg   = dark ? wxColour(220, 220, 225) : wxColour(50, 50, 55);

    m_page_bg = page_bg;
    m_card_bg = card_bg;
    m_sep_col = sep_col;
    m_empty_fg = empty_fg;
    m_text_fg  = text_fg;

    SetBackgroundColour(page_bg);
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    //cj_5 Custom title bar with close button, always matches content background.
    auto* title_bar = new wxPanel(this);
    title_bar->SetBackgroundColour(page_bg);
    title_bar->SetMinSize(wxSize(-1, FromDIP(56)));
    auto* title_sz = new wxBoxSizer(wxHORIZONTAL);
    auto* title_txt = new wxStaticText(title_bar, wxID_ANY, _L("QDC Notifications"));
    title_txt->SetFont(Label::Head_18);
    title_txt->SetForegroundColour(text_fg);
    title_sz->Add(title_txt, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(16));

    //cj_5 Hand-drawn X close button, no border.
    auto* close_btn = new wxPanel(title_bar, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(28), FromDIP(28)));
    close_btn->SetBackgroundColour(page_bg);
    close_btn->SetCursor(wxCURSOR_HAND);
    close_btn->SetCanFocus(false);
    close_btn->Bind(wxEVT_PAINT, [text_fg](wxPaintEvent& evt) {
        auto* w = static_cast<wxWindow*>(evt.GetEventObject());
        wxPaintDC dc(w);
        wxSize sz = w->GetSize();
        dc.SetPen(wxPen(text_fg, 2));
        int m = 8;
        dc.DrawLine(m, m, sz.x - m, sz.y - m);
        dc.DrawLine(sz.x - m, m, m, sz.y - m);
    });
    close_btn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
        EndModal(wxID_CANCEL);
    });
    title_sz->Add(close_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(32));
    title_bar->SetSizer(title_sz);
    main_sizer->Add(title_bar, 0, wxEXPAND);

    auto* title_sep = new wxPanel(this);
    title_sep->SetBackgroundColour(sep_col);
    title_sep->SetMinSize(wxSize(-1, 1));
    main_sizer->Add(title_sep, 0, wxEXPAND);

    // Scrolled content
    m_scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scrolled->SetBackgroundColour(page_bg);
    m_scrolled->SetScrollRate(0, FromDIP(5));
    m_top_sizer = new wxBoxSizer(wxVERTICAL);
    m_scrolled->SetSizer(m_top_sizer);

    main_sizer->Add(m_scrolled, 1, wxEXPAND);

    SetSizer(main_sizer);

    CallAfter([this]() { refresh_ui(); });

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
        EndModal(wxID_CANCEL);
    });
}

void HMSDialog::notify_new_data()
{
    CallAfter([this]() { refresh_ui(); });
}

void HMSDialog::clear_panels()
{
    m_scrolled->DestroyChildren();
    m_top_sizer->Clear();
    m_scrolled->SetSizer(m_top_sizer);
}

void HMSDialog::refresh_ui()
{
    if (!m_scrolled) return;
    Freeze();
    clear_panels();

    static const wxColour colours[] = {
        wxColour(255, 80, 70),   // level 1: red
        wxColour(255, 160, 40),  // level 2: orange
        wxColour(68, 121, 251),  // level 3: blue
    };

    if (m_items.empty()) {
        m_top_sizer->AddStretchSpacer();
        auto* empty_bmp = new wxStaticBitmap(m_scrolled, wxID_ANY,
            create_scaled_bitmap("emptyNotify", m_scrolled, 120));
        m_top_sizer->Add(empty_bmp, 0, wxALIGN_CENTER_HORIZONTAL);
        m_top_sizer->AddStretchSpacer();
        m_scrolled->SetVirtualSize(m_scrolled->GetClientSize());
        m_scrolled->Layout();
        Thaw();
        return;
    }

    const int card_width = FromDIP(kHmsRowFixedWidth);

    //cj_5 Right margin to align arrow button with separator's right edge
    // Separator is centered: left/right margins = (card_width - sep_width) / 2
    const int sep_right_margin = (card_width - FromDIP(kHmsSepWidth)) / 2;

    auto* card = new wxPanel(m_scrolled);
    card->SetBackgroundColour(m_card_bg);
    //cj_5 Fix card width so separator and arrow alignment is deterministic
    card->SetMaxSize(wxSize(card_width, -1));
    auto* card_sizer = new wxBoxSizer(wxVERTICAL);
    card->SetSizer(card_sizer);

    //cj_5 Display in reverse order (newest first)
    //y83
    for (size_t idx = m_items.size(); idx-- > 0; ) {
        const auto& item = m_items[idx];
        int level = item.error_type;

        auto* row_panel = new wxPanel(card);
        row_panel->SetBackgroundColour(m_card_bg);
        const int row_h_px = FromDIP(70);
        row_panel->SetMinSize(wxSize(card_width, row_h_px));
        row_panel->SetMaxSize(wxSize(card_width, row_h_px));

        auto* row_v = new wxBoxSizer(wxVERTICAL);
        auto* row_h = new wxBoxSizer(wxHORIZONTAL);

        // Speaker icon, 15px from left
        std::string hms_notify_img = "hms_notify_lv" + std::to_string(level);
        auto* icon = new wxStaticBitmap(row_panel, wxID_ANY,
            create_scaled_bitmap(hms_notify_img, row_panel, 16));
        row_h->Add(icon, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(15));
        row_h->AddSpacer(FromDIP(10));

        // Message — clickable text with underline on hover
        wxString msg_text = item.error_message.empty()
            ? wxString::FromUTF8(item.error_code)
            : wxString::FromUTF8(item.error_message);
        
        auto* msg = new wxStaticText(row_panel, wxID_ANY, msg_text, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        msg->SetFont(Label::Head_14);
        msg->SetForegroundColour(m_text_fg);
        msg->SetCursor(wxCURSOR_HAND);
        
        // Capture data for click handler
        std::string err_code = item.error_code;
        std::string machine  = m_machine_type;
        
        // Hover effect: toggle underline font
        msg->Bind(wxEVT_ENTER_WINDOW, [msg](wxMouseEvent& ev) {
            wxFont f = msg->GetFont();
            f.SetUnderlined(true);
            msg->SetFont(f);
            msg->Refresh();
            ev.Skip();
        });
        msg->Bind(wxEVT_LEAVE_WINDOW, [msg](wxMouseEvent& ev) {
            wxFont f = msg->GetFont();
            f.SetUnderlined(false);
            msg->SetFont(f);
            msg->Refresh();
            ev.Skip();
        });
        
        // Click: open wiki
        msg->Bind(wxEVT_LEFT_UP, [err_code, machine](wxMouseEvent&) {
            std::string lang = wxGetApp().app_config->get_language_code();
            auto dash_pos = lang.find('-');
            if (dash_pos != std::string::npos)
                lang = lang.substr(0, dash_pos);

            std::string trimmed = err_code.length() > 4 ? err_code.substr(4) : err_code;

            std::string machine_norm;
            for (char c : machine) {
                if (std::isalnum(static_cast<unsigned char>(c)))
                    machine_norm += std::tolower(static_cast<unsigned char>(c));
            }
            std::string machine_slug;
            if (machine_norm.find("max4") != std::string::npos)
                machine_slug = "max4";
            else
                machine_slug = machine_norm;

            wxString url = wxString::Format("https://wiki.qidi3d.com/%s/QDC/%s_%s",
                lang, trimmed, machine_slug);
            wxLaunchDefaultBrowser(url);
        });

        // No extra right margin needed since arrow is gone
        row_h->Add(msg, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

        row_v->AddStretchSpacer();
        row_v->Add((wxSizer*)row_h, 0, wxEXPAND);
        row_v->AddStretchSpacer();
        row_panel->SetSizer(row_v);

        card_sizer->Add(row_panel, 0, wxEXPAND);

        // Separator — fixed width, centered
        if (idx > 0) {
            auto* sep_row = new wxBoxSizer(wxHORIZONTAL);
            auto* sep = new wxPanel(card);
            sep->SetBackgroundColour(m_sep_col);
            const int sep_w = FromDIP(kHmsSepWidth);
            sep->SetMinSize(wxSize(sep_w, 1));
            sep->SetMaxSize(wxSize(sep_w, 1));
            sep_row->AddStretchSpacer();
            sep_row->Add(sep, 0, wxEXPAND);
            sep_row->AddStretchSpacer();
            card_sizer->Add((wxSizer*)sep_row, 0, wxEXPAND);
        }
    }

    // Center card in scrolled window
    m_top_sizer->AddSpacer(FromDIP(66));
    m_top_sizer->Add(card, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(32));

    m_scrolled->SetSizerAndFit(m_top_sizer);
    m_scrolled->Layout();
    Thaw();
}

}} // namespace
