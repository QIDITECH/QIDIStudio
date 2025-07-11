#include "QDTStatusBarPrint.hpp"

#include <wx/timer.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/frame.h>
#include "wx/evtloop.h"
#include <wx/gdicmn.h>
#include "GUI_App.hpp"

#include "I18N.hpp"

#include <iostream>
#include <regex>

namespace Slic3r {

wxDEFINE_EVENT(EVT_SHOW_ERROR_INFO, wxCommandEvent);

QDTStatusBarPrint::QDTStatusBarPrint(wxWindow *parent, int id)
 : m_self{new wxPanel(parent, id == -1 ? wxID_ANY : id)}
    , m_sizer(new wxBoxSizer(wxHORIZONTAL))
{
    m_self->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_top = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);


    top_panel = new wxPanel(m_self, wxID_ANY);
    top_panel->SetBackgroundColour(*wxWHITE);
    top_panel->SetMinSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(26)));
    top_panel->SetMaxSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(26)));

    m_status_text = new wxStaticText(top_panel, wxID_ANY, wxEmptyString);
    m_status_text->SetForegroundColour(wxColour(107, 107, 107));
    m_status_text->SetMaxSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(26)));
    m_status_text->SetMinSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(26)));
    m_status_text->SetFont(::Label::Body_13);

    StateColor btn_bt_white(std::pair<wxColour, int>(wxColour(0x90, 0x90, 0x90), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));


    StateColor btn_txt_white(std::pair<wxColour, int>(wxColour("#FFFFFE"), StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal));

    m_cancelbutton = new Button(m_self, _L("Cancel"));
    m_cancelbutton->SetMinSize(wxSize(m_self->FromDIP(80), m_self->FromDIP(32)));
    m_cancelbutton->SetMaxSize(wxSize(m_self->FromDIP(80), m_self->FromDIP(32)));
    m_cancelbutton->SetBackgroundColor(btn_bt_white);
    m_cancelbutton->SetBorderColor(btn_bd_white);
    m_cancelbutton->SetTextColor(btn_txt_white);
    m_cancelbutton->SetCornerRadius(4);
    m_cancelbutton->Bind(wxEVT_BUTTON,
        [this](wxCommandEvent &evt) {
        m_was_cancelled = true;
        if (m_cancel_cb_fina)
            m_cancel_cb_fina();
    });

    m_stext_percent = new wxStaticText(top_panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_stext_percent->SetForegroundColour(wxColour(107, 107, 107));
    m_stext_percent->SetFont(::Label::Head_13);
    m_stext_percent->Wrap(-1);

    m_sizer_status_text = new wxBoxSizer(wxHORIZONTAL);
    m_link_show_error = new Label(top_panel, _L("Check the reason"));
    m_link_show_error->SetForegroundColour(wxColour("#4479fb"));
    m_link_show_error->SetFont(::Label::Head_13);
    m_link_show_error->Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { this->m_self->SetCursor(wxCURSOR_HAND); });
    m_link_show_error->Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) { this->m_self->SetCursor(wxCURSOR_ARROW); });

    m_bitmap_show_error_close = create_scaled_bitmap("link_more_error_close", nullptr, 7);
    m_bitmap_show_error_open = create_scaled_bitmap("link_more_error_open", nullptr, 7);
    m_static_bitmap_show_error = new wxStaticBitmap(top_panel, wxID_ANY, m_bitmap_show_error_open, wxDefaultPosition, wxSize(m_self->FromDIP(7), m_self->FromDIP(7)));

    m_link_show_error->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {this->m_self->SetCursor(wxCURSOR_HAND); });
    m_link_show_error->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {this->m_self->SetCursor(wxCURSOR_ARROW); });
    m_link_show_error->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        if (!m_show_error_info_state) { m_show_error_info_state = true; m_static_bitmap_show_error->SetBitmap(m_bitmap_show_error_close); }
        else { m_show_error_info_state = false; m_static_bitmap_show_error->SetBitmap(m_bitmap_show_error_open); }
        wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_ERROR_INFO);
        wxQueueEvent(this->m_self->GetParent(), evt);
    });


    m_link_show_error->Hide();
    m_static_bitmap_show_error->Hide();
    m_static_bitmap_show_error->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {this->m_self->SetCursor(wxCURSOR_HAND); });
    m_static_bitmap_show_error->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {this->m_self->SetCursor(wxCURSOR_ARROW); });
    m_static_bitmap_show_error->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        if (!m_show_error_info_state) {m_show_error_info_state = true;m_static_bitmap_show_error->SetBitmap(m_bitmap_show_error_close);}
        else {m_show_error_info_state = false;m_static_bitmap_show_error->SetBitmap(m_bitmap_show_error_open);}
        wxCommandEvent* evt = new wxCommandEvent(EVT_SHOW_ERROR_INFO);
        wxQueueEvent(this->m_self->GetParent(), evt);
    });

    m_sizer_status_text->Add(m_link_show_error, 0, wxALIGN_CENTER, 0);
    m_sizer_status_text->Add(0, 0, 0, wxLEFT, top_panel->FromDIP(4));
    m_sizer_status_text->Add(m_static_bitmap_show_error, 0, wxALIGN_CENTER, 0);

    //y58
    m_prog = new QDTGauge(m_self, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, m_self->FromDIP(6)), wxGA_HORIZONTAL);
    m_prog->SetMinSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(6)));
    m_prog->SetMaxSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(6)));
    m_prog->SetValue(0);

    m_sizer_bottom->Add(m_prog, 1, wxALIGN_CENTER, 0);
    m_sizer_bottom->Add(0, 0, 1, wxEXPAND, 0);


    m_sizer_top->Add(m_status_text, 1, wxEXPAND, 0);
    m_sizer_top->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_top->Add(m_sizer_status_text, 0, wxALIGN_CENTER, 0);
    m_sizer_top->Add(m_stext_percent, 0, wxEXPAND, 0);

    top_panel->SetSizer(m_sizer_top);
    top_panel->Layout();

    m_sizer_body->Add(top_panel, 0, wxEXPAND, 0);
    m_sizer_body->Add(m_sizer_bottom, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 1, wxEXPAND, 0);

    wxBoxSizer *m_sizer_button = new wxBoxSizer(wxVERTICAL);
    m_sizer_button->Add(m_cancelbutton, 0, wxALIGN_CENTER, 0);

    m_sizer->Add(m_sizer_body, 1, wxALIGN_CENTER, 0);
    m_sizer->Add(m_sizer_button, 0, wxALIGN_CENTER, 0);

    m_self->SetSizer(m_sizer);
    m_self->Layout();
    m_sizer->Fit(m_self);
}

void QDTStatusBarPrint::set_prog_block()
{
}

int QDTStatusBarPrint::get_progress() const
{
    return m_prog->GetValue();
}

void QDTStatusBarPrint::set_progress(int val)
{
    if(val < 0) return;

    //add the logic for arrange/orient jobs, which don't call stop_busy
    if (!m_prog->IsShown()) {
        m_sizer->Show(m_prog);
        m_sizer->Show(m_cancelbutton);
    }
    m_prog->SetValue(val);
    set_percent_text(wxString::Format("%d%%", val));

    m_sizer->Layout();
}

int QDTStatusBarPrint::get_range() const
{
    return m_prog->GetRange();
}

void QDTStatusBarPrint::set_range(int val)
{
    if(val != m_prog->GetRange()) {
        m_prog->SetRange(val);
    }
}

void QDTStatusBarPrint::clear_percent()
{
    //set_percent_text(wxEmptyString);
    if (!m_link_show_error->IsShown()) /*do not hide cancel if there are errors*/ {
        m_cancelbutton->Hide();
    }
}

void QDTStatusBarPrint::show_error_info(wxString msg, int code, wxString description, wxString extra)
{
    set_status_text(msg);
    m_prog->Hide();
    m_stext_percent->Hide();
    m_link_show_error->Show();
    m_static_bitmap_show_error->Show();

    top_panel->SetMinSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(32)));
    top_panel->SetMaxSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(32)));
    m_status_text->SetMaxSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(32)));
    m_status_text->SetMinSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(32)));

    m_cancelbutton->Show();
    m_self->Layout();
    m_sizer->Layout();
}

void QDTStatusBarPrint::show_progress(bool show)
{
    if (show) {
        m_sizer->Show(m_prog);
        m_sizer->Layout();
    }
    else {
        m_sizer->Hide(m_prog);
        m_sizer->Layout();
    }
}

void QDTStatusBarPrint::start_busy(int rate)
{
    m_busy = true;
    show_progress(true);
    show_cancel_button();
}

void QDTStatusBarPrint::stop_busy()
{
    show_progress(false);
    hide_cancel_button();
    m_prog->SetValue(0);
    m_sizer->Layout();
    m_busy = false;
}

void QDTStatusBarPrint::set_cancel_callback_fina(QDTStatusBarPrint::CancelFn ccb)
{
    m_cancel_cb_fina = ccb;
     if (ccb) {
        m_sizer->Show(m_cancelbutton);
    } else {
        m_sizer->Hide(m_cancelbutton);
    }
}

void QDTStatusBarPrint::set_cancel_callback(QDTStatusBarPrint::CancelFn ccb) {
    /*  m_cancel_cb = ccb;
      if (ccb) {
          m_sizer->Show(m_cancelbutton);
      }
      else {
          m_sizer->Hide(m_cancelbutton);
      }
      m_sizer->Layout();*/
}

wxPanel* QDTStatusBarPrint::get_panel()
{
    return m_self;
}

bool QDTStatusBarPrint::is_english_text(wxString str)
{
    std::regex reg("^[0-9a-zA-Z]+$");
    std::smatch matchResult;

    std::string pattern_Special = "{}[]<>~!@#$%^&*(),.?/ :";
    for (auto i = 0; i < str.Length(); i++) {
        std::string regex_str = wxString(str[i]).ToStdString();
        if (std::regex_match(regex_str, matchResult, reg)) {
            continue;
        }
        else {
            int result = pattern_Special.find(regex_str.c_str());
            if (result < 0 || result > pattern_Special.length()) {
                return false;
            }
        }
    }
    return true;
}

bool QDTStatusBarPrint::format_text(wxStaticText* dc, int width, const wxString& text, wxString& multiline_text)
{
    bool multiline = false;
    multiline_text = text;
    if (width > 0 && dc->GetTextExtent(text).x > width) {
        size_t start = 0;
        while (true) {
            size_t idx = size_t(-1);
            for (size_t i = start; i < multiline_text.Len(); i++) {
                if (multiline_text[i] == ' ') {
                    if (dc->GetTextExtent(multiline_text.SubString(start, i)).x < width)
                        idx = i;
                    else {
                        if (idx == size_t(-1)) idx = i;
                        break;
                    }
                }
            }
            if (idx == size_t(-1)) break;
            multiline = true;
            multiline_text[idx] = '\n';
            start = idx + 1;
            if (dc->GetTextExtent(multiline_text.Mid(start)).x < width) break;
        }
    }
    return multiline;
    //return dc->GetTextExtent(multiline_text);
}


void QDTStatusBarPrint::set_status_text(const wxString& txt)
{
    //auto txtss = "Sending the printing task has timed out.\nPlease try again!";
    //auto txtss = "The printing project is being uploaded... 25%%";
    //m_status_text->SetLabelText(txtss);
    //wxString str;
    //format_text(m_status_text, m_self->FromDIP(300), txt, str);

    //if (m_status_text->GetTextExtent(txt).x > m_self->FromDIP(360)) {
    //    m_status_text->SetSize(m_self->FromDIP(360), m_self->FromDIP(40));
    //}
    //if (m_prog->IsShown()) {
    //    top_panel->SetMinSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(26)));
    //    top_panel->SetMaxSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(26)));
    //    m_status_text->SetMaxSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(26)));
    //    m_status_text->SetMinSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(26)));
    //} else {
    //    top_panel->SetMinSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(32)));
    //    top_panel->SetMaxSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(32)));
    //    m_status_text->SetMaxSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(32)));
    //    m_status_text->SetMinSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(32)));
    //}
    m_status_text->SetLabelText(txt);
    m_self->Layout();

    //m_status_text->Wrap(m_self->FromDIP(360));
    //m_status_text->Layout();
    //m_self->Layout();
    //if (is_english_text(str)) m_status_text->Wrap(m_self->FromDIP(280));
}

void QDTStatusBarPrint::set_percent_text(const wxString &txt)
{
    m_stext_percent->SetLabelText(txt);
}

void QDTStatusBarPrint::set_status_text(const std::string& txt)
{
    this->set_status_text(txt.c_str());
}

void QDTStatusBarPrint::set_status_text(const char *txt)
{
    this->set_status_text(wxString::FromUTF8(txt));
    get_panel()->GetParent()->Layout();
    get_panel()->GetParent()->Update();
}

void QDTStatusBarPrint::msw_rescale() {
    //set_prog_block();
    m_cancelbutton->SetMinSize(wxSize(m_self->FromDIP(56), m_self->FromDIP(24)));
}

wxString QDTStatusBarPrint::get_status_text() const
{
    return m_status_text->GetLabelText();
}

bool QDTStatusBarPrint::update_status(wxString &msg, bool &was_cancel, int percent, bool yield)
{
    set_status_text(msg);
    if (percent >= 0)
        this->set_progress(percent);

    if (yield)
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    was_cancel = m_was_cancelled;
    return true;
}

void QDTStatusBarPrint::reset()
{
    m_link_show_error->Hide();
    m_static_bitmap_show_error->Hide();
    m_prog->Show();
    m_stext_percent->Show();
    m_cancelbutton->Enable();
    m_cancelbutton->Show();
    m_was_cancelled = false;

    top_panel->SetMinSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(26)));
    top_panel->SetMaxSize(wxSize(m_self->FromDIP(550), m_self->FromDIP(26)));
    m_status_text->SetMaxSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(26)));
    m_status_text->SetMinSize(wxSize(m_self->FromDIP(400), m_self->FromDIP(26)));

    set_status_text("");
    set_progress(0);
    set_percent_text(wxString::Format("%d%%", 0));
}

void QDTStatusBarPrint::set_font(const wxFont &font)
{
    m_self->SetFont(font);
}

void QDTStatusBarPrint::show_cancel_button()
{
    m_sizer->Show(m_cancelbutton);
    m_sizer->Layout();
}

void QDTStatusBarPrint::hide_cancel_button()
{
    m_sizer->Hide(m_cancelbutton);
    m_cancelbutton->Hide();
    m_sizer->Layout();
}

void QDTStatusBarPrint::change_button_label(wxString name)
{
    m_cancelbutton->SetLabel(name);
}

void QDTStatusBarPrint::disable_cancel_button()
{
    m_cancelbutton->Disable();
}

void QDTStatusBarPrint::enable_cancel_button()
{
    m_cancelbutton->Enable();
}

}
