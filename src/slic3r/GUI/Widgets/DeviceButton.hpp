#ifndef slic3r_GUI_DeviceButton2_hpp_
#define slic3r_GUI_DeviceButton2_hpp_

#include "../wxExtensions.hpp"
#include "StaticBox.hpp"

class DeviceButton : public StaticBox
{
    wxSize textSize;
    wxSize minSize; // set by outer
    wxSize paddingSize;
    ScalableBitmap active_icon;
    ScalableBitmap inactive_icon;

    StateColor   text_color;

    bool pressedDown = false;
    bool m_selected  = true;
    bool canFocus  = true;

    static const int buttonWidth = 200;
    static const int buttonHeight = 50;

public:
    //y40
    DeviceButton(wxString name_text, wxString ip_text, wxString apikey_text);

    DeviceButton(wxWindow *parent,
                  wxString  text,
                  wxString  icon     = "",
                  long      style    = 0,
                  wxSize    iconSize = wxSize(16, 16),
                  wxString  name_text = "",
                  wxString  ip_text   = "",
                  wxString  apikey_text = "");

    DeviceButton(wxWindow* parent, wxString icon, long style);

    bool Create(wxWindow* parent, wxString text, wxString icon = "", long style = 0, wxSize iconSize = wxSize(16, 16));

    void SetLabel(const wxString& label) override;

    void SetIconWithSize(const wxString &icon, wxSize iconSize);

    void SetIcon(const wxString &icon);

    void SetInactiveIcon(const wxString& icon);

    void SetMinSize(const wxSize& size) override;
    
    void SetPaddingSize(const wxSize& size);
    
    void SetTextColor(StateColor const &color);

    void SetTextColorNormal(wxColor const &color);

    void SetSelected(bool selected = true) { m_selected = selected; }

    bool Enable(bool enable = true) override;

    void SetCanFocus(bool canFocus) override;

    void SetIsSimpleMode(bool isSimpleMode);

    void SetIsSelected(bool isSelected);

    void SetStateText(const wxString &text);

    void SetProgressText(const wxString &text);

    void SetNameText(const wxString &text);

    void SetIPText(const wxString &text);

    bool GetIsSelected() { return m_isSelected; }

    wxString getIPLabel() { return m_ip_text; };

    wxString GetStateText() { return m_state_text; }

    wxString getNameLabel() { return m_name_text; }
    //y40
    wxString GetApikey() { return m_apikey; };

    void Rescale();

protected:
#ifdef __WIN32__
    WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    bool AcceptsFocus() const override;

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);
    void mouseCaptureLost(wxMouseCaptureLostEvent &event);
    void keyDownUp(wxKeyEvent &event);

    void sendButtonEvent();

    wxString m_name_text;
    wxString m_ip_text;
    wxString m_icon_text;
    wxString m_state_text = "standby";
    wxString m_progress_text = "(0%)";
    bool     m_isSimpleMode = true;
    bool     m_isSelected   = false;
    wxString m_apikey;

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_Button_hpp_
