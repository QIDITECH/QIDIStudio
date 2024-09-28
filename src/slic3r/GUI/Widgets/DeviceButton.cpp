#include "DeviceButton.hpp"

#include <wx/dcgraph.h>
#include <wx/dc.h>
#include <wx/dcclient.h>

BEGIN_EVENT_TABLE(DeviceButton, StaticBox)

EVT_LEFT_DOWN(DeviceButton::mouseDown)
EVT_LEFT_UP(DeviceButton::mouseReleased)
EVT_MOUSE_CAPTURE_LOST(DeviceButton::mouseCaptureLost)
EVT_KEY_DOWN(DeviceButton::keyDownUp)
EVT_KEY_UP(DeviceButton::keyDownUp)

// catch paint events
EVT_PAINT(DeviceButton::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

//y40
DeviceButton::DeviceButton(wxString name_text, wxString ip_text, wxString apikey_text) : paddingSize(10, 8), m_name_text(name_text), m_ip_text(ip_text), m_apikey(apikey_text)
{
    background_color = StateColor(
        std::make_pair(0x262629, (int) StateColor::Disabled),
        std::make_pair(0x37EE7C, (int) StateColor::Hovered | StateColor::Checked),
        std::make_pair(0x00AE42, (int) StateColor::Checked),
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Hovered), 
        std::make_pair(0x262629, (int) StateColor::Normal));
    text_color       = StateColor(
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Disabled), 
        std::make_pair(*wxBLACK, (int) StateColor::Normal));
}

//y40
DeviceButton::DeviceButton(wxWindow *parent,
                             wxString  text,
                             wxString  icon,
                             long      style,
                             wxSize    iconSize /* = wxSize(16, 16)*/,
                             wxString  name_text,
                             wxString  ip_text,
                             wxString  apikey_text)
    : DeviceButton(name_text,ip_text, apikey_text)
{
    Create(parent, text, icon, style, iconSize);
}


DeviceButton::DeviceButton(wxWindow* parent,
    wxString  icon,
    long      style)
{
    // y20
    paddingSize = wxSize(12, 6);
    wxSize iconSize = wxSize(20, 20);
    Create(parent, "", icon, style, iconSize);
}

bool DeviceButton::Create(wxWindow *parent, wxString text, wxString icon, long style, wxSize iconSize /* = wxSize(16, 16)*/)
{
    StaticBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);
    state_handler.attach({&text_color});
    state_handler.update_binds();
    wxWindow::SetFont(wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    // y1
    std::string test_string     = text.ToStdString();
    wxString    device_nameText = wxString::FromUTF8(test_string);
    wxWindow::SetLabel(device_nameText);

    if (!icon.IsEmpty()) {
        this->active_icon = ScalableBitmap(this, icon.ToStdString(), iconSize.x);
    }
    messureSize();
    return true;
}

void DeviceButton::SetLabel(const wxString &label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

void DeviceButton::SetIconWithSize(const wxString &icon, wxSize iconSize)
{
    if (!icon.IsEmpty()) {
        this->active_icon = ScalableBitmap(this, icon.ToStdString(), iconSize.x);
    } else {
        this->active_icon = ScalableBitmap();
    }
    messureSize();
    Refresh();
}

void DeviceButton::SetIcon(const wxString &icon)
{
    if (!icon.IsEmpty()) {
        this->active_icon = ScalableBitmap(this, icon.ToStdString(), this->active_icon.px_cnt());
    }
    else
    {
        this->active_icon = ScalableBitmap();
    }
    Refresh();
}

void DeviceButton::SetInactiveIcon(const wxString &icon)
{
    if (!icon.IsEmpty()) {
        this->inactive_icon = ScalableBitmap(this, icon.ToStdString(), this->active_icon.px_cnt());
    } else {
        this->inactive_icon = ScalableBitmap();
    }
    Refresh();
}

void DeviceButton::SetMinSize(const wxSize &size)
{
    minSize = size;
    messureSize();
}

void DeviceButton::SetPaddingSize(const wxSize &size)
{
    paddingSize = size;
    messureSize();
}

void DeviceButton::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void DeviceButton::SetTextColorNormal(wxColor const &color)
{
    text_color.setColorForStates(color, 0);
    Refresh();
}

bool DeviceButton::Enable(bool enable)
{
    bool result = wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
    }
    return result;
}

void DeviceButton::SetCanFocus(bool canFocus) { this->canFocus = canFocus; }

void DeviceButton::SetIsSimpleMode(bool isSimpleMode)
{
    m_isSimpleMode = isSimpleMode;
    if ((this->active_icon.bmp().IsOk())) {
        if (m_isSimpleMode) {
            SetIconWithSize(this->active_icon.name(), wxSize(30, 30));
        } else {
            SetIconWithSize(this->active_icon.name(), wxSize(80, 80));
        }
    } else {
        messureSize();
        Refresh();
    }
}

void DeviceButton::SetIsSelected(bool isSelected)
{
    m_isSelected = isSelected;
    if (m_isSelected) {
        StateColor calc_btn_bg(std::pair<wxColour, int>(wxColour(147, 147, 150), StateColor::Pressed),
                               std::pair<wxColour, int>(wxColour(100, 100, 105), StateColor::Hovered),
                               std::pair<wxColour, int>(wxColour(100, 100, 105), StateColor::Normal));
        SetBackgroundColor(calc_btn_bg);
    } else {
        StateColor calc_btn_bg(std::pair<wxColour, int>(wxColour(118, 118, 121), StateColor::Pressed),
                               std::pair<wxColour, int>(wxColour(76, 76, 80), StateColor::Hovered),
                               std::pair<wxColour, int>(wxColour(67, 67, 71), StateColor::Normal));
        SetBackgroundColor(calc_btn_bg);
    }
    Refresh();
}

void DeviceButton::SetStateText(const wxString &text)
{
    m_state_text = text;
    Refresh();
}

void DeviceButton::SetProgressText(const wxString &text)
{
    m_progress_text = text;
    Refresh();
}

void DeviceButton::SetNameText(const wxString &text)
{
    m_name_text = text;
    Refresh();
}

void DeviceButton::SetIPText(const wxString &text)
{
    m_ip_text = text;
    Refresh();
}

void DeviceButton::Rescale()
{
/*    if (this->active_icon.bmp().IsOk())
        this->active_icon.msw_rescale();

    if (this->inactive_icon.bmp().IsOk())
        this->inactive_icon.msw_rescale();

*/
    messureSize();
}

void DeviceButton::paintEvent(wxPaintEvent &evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void DeviceButton::render(wxDC &dc)
{
    StaticBox::render(dc);
    int states = state_handler.states();
    wxSize size = GetSize();
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    wxSize szIcon;
    wxSize szContent = textSize;

    ScalableBitmap icon;
    if (m_selected || ((states & (int)StateColor::State::Hovered) != 0))
        icon = active_icon;
    else
        icon = inactive_icon;

    wxRect rcContent = {{0, 0}, size};
    wxSize offset    = (size - szContent) / 2;
    if (offset.x < 0)
        offset.x = 0;
    rcContent.Deflate(offset.x, offset.y);
    // y1
    std::string tempName_string = m_name_text.ToStdString();
    wxString    m_name_text     = wxString::FromUTF8(tempName_string);

    if (GetLabel() == "") {
        // y20
        dc.DrawBitmap(icon.bmp(), rcContent.x/2+1, rcContent.y/2-2);
    }
    // y2
    else if (m_ip_text == "" && m_name_text == "") {
        //dc.DrawBitmap(icon.get_bitmap(), 10, (GetSize().GetHeight() - icon.get_bitmap().GetHeight()) / 2);
        if (m_isSimpleMode) {
            dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        } else {
            dc.SetFont(wxFont(12, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        }
        dc.SetTextForeground(text_color.colorForStates(states));
        dc.DrawText(GetLabel(), rcContent.x / 2, size.y/2 - dc.GetTextExtent(GetLabel()).y / 2);
    }
    else if (m_isSimpleMode) {
        dc.SetFont(wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        dc.SetTextForeground(wxColour(230, 230, 230));
        dc.DrawText(m_name_text, 10, rcContent.y);
        int dotRadius = 4;
        int dotX      = size.x - dotRadius - 10;
        int dotY      = 10;
        if (m_isSelected) {
            dc.SetBrush(wxBrush(wxColour(68, 121, 251)));
            dc.SetPen(wxPen(wxColour(68, 121, 251)));
        } else {
            dc.SetBrush(wxBrush(wxColour(26, 26, 28)));
            dc.SetPen(wxPen(wxColour(26, 26, 28)));
        }
        dc.DrawCircle(dotX, dotY, dotRadius);

    } else {
         dc.DrawBitmap(icon.bmp(), 10, (GetSize().GetHeight() - icon.bmp().GetHeight()) / 2);

         dc.SetFont(wxFont(15, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
         dc.SetTextForeground(wxColour(230, 230, 230));
         dc.DrawText(m_name_text, 10 + icon.GetBmpWidth() + 10, rcContent.y-30);
         dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
         dc.SetTextForeground(wxColour(174, 174, 174));

         dc.DrawText("IP:" + m_ip_text, 10 + icon.GetBmpWidth() + 10, rcContent.y);

         wxBitmap m_bitmap_state = create_scaled_bitmap("printer_state", this, 20);
         dc.DrawBitmap(m_bitmap_state, 10 + icon.bmp().GetWidth() + 10, rcContent.y + m_bitmap_state.GetWidth(), true);

         dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
         dc.SetTextForeground(wxColour(174, 174, 174));

         dc.DrawText(m_state_text, 10 + icon.GetBmpWidth() + m_bitmap_state.GetWidth() + 15,
                     rcContent.y + m_bitmap_state.GetWidth() + (m_bitmap_state.GetWidth() - dc.GetTextExtent(m_state_text).y) / 2);
         if (m_state_text == "printing") {
             dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
             dc.SetTextForeground(wxColour(33, 148, 239));
             dc.DrawText(m_progress_text, 10 + icon.GetBmpWidth() + m_bitmap_state.GetWidth() + 77,
                         rcContent.y + m_bitmap_state.GetWidth() + (m_bitmap_state.GetWidth() - dc.GetTextExtent(m_progress_text).y) / 2+2);
         }
         int dotRadius = 4;
         int dotX      = size.x - dotRadius - 10;
         int dotY      = 10;
         if (m_isSelected) {
             dc.SetBrush(wxBrush(wxColour(68, 121, 251)));
             dc.SetPen(wxPen(wxColour(68, 121, 251)));
         } else {
             dc.SetBrush(wxBrush(wxColour(26, 26, 28)));
             dc.SetPen(wxPen(wxColour(26, 26, 28)));
         }
         dc.DrawCircle(dotX, dotY, dotRadius);

    }
}

void DeviceButton::messureSize()
{
    wxClientDC dc(this);
    textSize = dc.GetTextExtent(GetLabel());
    if (minSize.GetWidth() > 0) {
        wxWindow::SetMinSize(minSize);
        return;
    }
    wxSize szContent = textSize;
    if (this->active_icon.bmp().IsOk()) {
        if (szContent.y > 0) {
            //QDS norrow size between text and icon
            szContent.x += 5;
        }
        wxSize szIcon = this->active_icon.GetBmpSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
    }
    wxSize size = szContent + paddingSize * 2;
    if (m_isSimpleMode && m_ip_text != "")
        size.x = 180;
    else if (m_ip_text != "")
        size.x = 290;
    if (minSize.GetHeight() > 0)
        size.SetHeight(minSize.GetHeight());

        wxWindow::SetMinSize(size);

}

void DeviceButton::mouseDown(wxMouseEvent &event)
{
    event.Skip();
    pressedDown = true;
    if (canFocus)
        SetFocus();
        CaptureMouse();
}

void DeviceButton::mouseReleased(wxMouseEvent &event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        if (HasCapture())
            ReleaseMouse();
        if (wxRect({0, 0}, GetSize()).Contains(event.GetPosition()))
            sendButtonEvent();
    }
}

void DeviceButton::mouseCaptureLost(wxMouseCaptureLostEvent &event)
{
    wxMouseEvent evt;
    mouseReleased(evt);
}

void DeviceButton::keyDownUp(wxKeyEvent &event)
{
    if (event.GetKeyCode() == WXK_SPACE || event.GetKeyCode() == WXK_RETURN) {
        wxMouseEvent evt(event.GetEventType() == wxEVT_KEY_UP ? wxEVT_LEFT_UP : wxEVT_LEFT_DOWN);
        event.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        return;
    }
    if (event.GetEventType() == wxEVT_KEY_DOWN &&
        (event.GetKeyCode() == WXK_TAB || event.GetKeyCode() == WXK_LEFT || event.GetKeyCode() == WXK_RIGHT 
        || event.GetKeyCode() == WXK_UP || event.GetKeyCode() == WXK_DOWN))
        HandleAsNavigationKey(event);
    else
        event.Skip();
}

void DeviceButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}

#ifdef __WIN32__

WXLRESULT DeviceButton::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_GETDLGCODE) { return DLGC_WANTMESSAGE; }
    if (nMsg == WM_KEYDOWN) {
        wxKeyEvent event(CreateKeyEvent(wxEVT_KEY_DOWN, wParam, lParam));
        switch (wParam) {
        case WXK_RETURN: { // WXK_RETURN key is handled by default button
            GetEventHandler()->ProcessEvent(event);
            return 0;
        }
        }
    }
    return wxWindow::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

bool DeviceButton::AcceptsFocus() const { return canFocus; }
