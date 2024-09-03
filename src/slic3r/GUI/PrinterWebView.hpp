#ifndef slic3r_PrinterWebView_hpp_
#define slic3r_PrinterWebView_hpp_


#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>

//B35
//B45
#if defined __linux__
#include <boost/log/trivial.hpp>
#include <wx/wx.h>
#include <thread>
#include <boost/format.hpp>
#endif

//B45
#include "PrintHostDialogs.hpp"
#include <wx/tokenzr.h>

//B64
#include "../QIDI/QIDINetwork.hpp"
#include <boost/thread.hpp>
#include "./Widgets/SwitchButton.hpp"
#include "./Widgets/DeviceButton.hpp"
namespace Slic3r {
namespace GUI {

//y28
enum WebState
{
    isDisconnect,
    isLocalWeb,
    isNetWeb
};


class PrinterWebView : public wxPanel {
public:
    PrinterWebView(wxWindow *parent);
    virtual ~PrinterWebView();

    wxBoxSizer *init_menu_bar(wxPanel *Panel);
    wxBoxSizer *init_login_bar(wxPanel *Panel);
    void        init_scroll_window(wxPanel *Panel);
    void        CreatThread();
    void load_url(wxString& url);
    void        load_net_url(std::string url, std::string ip);
    void UpdateState();
    void OnClose(wxCloseEvent& evt);

    void        OnZoomButtonClick(wxCommandEvent &event);
    void        OnRefreshButtonClick(wxCommandEvent &event);
    void OnAddButtonClick(wxCommandEvent &event);
    void OnDeleteButtonClick(wxCommandEvent &event);
    void OnEditButtonClick(wxCommandEvent &event);

    void        OnLoginButtonClick(wxCommandEvent &event);
    void RunScript(const wxString &javascript);
    //void OnScriptMessageReceived(wxWebViewEvent &event);
    void OnScriptMessage(wxWebViewEvent &evt);
    void UpdateLayout();
    void OnScroll(wxScrollWinEvent &event);
    void OnScrollup(wxScrollWinEvent &event);
    void OnScrolldown(wxScrollWinEvent &event);
    //void SendRecentList(int images);
    void SetButtons(std::vector<DeviceButton *> buttons);
    void  AddButton(const wxString &                           device_name,
                    const wxString &                           ip,
                    const wxString &                           machine_type,
                    const wxString &                           fullname,
                    bool                                       isSelected,
                   bool                isQIDI);
    void                        DeleteButton();
    void                        UnSelectedButton();
    void ShowNetPrinterButton();
    void ShowLocalPrinterButton();
#if QDT_RELEASE_TO_PUBLIC
    void AddNetButton(const Device device);
#endif

    void DeleteNetButton();
    void                        RefreshButton();
    void SetUpdateHandler(const std::function<void(wxCommandEvent &)> &handler) { m_handlerl = handler; }
    void StopStatusThread()
    {
        m_stopThread = true;
        if (m_statusThread.joinable()) {
            m_statusThread.join();
        }
    };
    void SetPauseThread(bool status) { m_pauseThread = status; };
    void SetPresetChanged(bool status);
    void SetLoginStatus(bool status);
    std::string NormalizeVendor(const std::string& str);


    std::vector<DeviceButton *> GetButton() { return m_buttons; };
    bool                        GetNetMode() { return m_isNetMode; };
    std::vector<DeviceButton *> GetNetButton() { return m_net_buttons; };
    wxString GetWeburl(){ return m_web; };
    void load_disconnect_url(wxString& url);

private:
    wxBoxSizer *leftallsizer;

    wxBoxSizer *                          devicesizer;
    wxBoxSizer *                          allsizer;
    bool        m_isSimpleMode = false;
    bool                                  m_isNetMode    = false;
    wxButton *arrow_button;
    wxStaticText *    text_static;

    int height = 0; 
    wxString  m_web;
    wxString m_ip;
    std::function<void(wxCommandEvent &)> m_handlerl;
    std::function<void(wxCommandEvent &)> m_delete_handlerl;

    wxScrolledWindow *          leftScrolledWindow;
    wxPanel *         leftPanel;


    std::vector<DeviceButton *>           m_buttons;
    std::vector<DeviceButton *>           m_net_buttons;
    std::string                           m_select_type;
    std::thread                           m_statusThread;
    std::atomic<bool>                     m_stopThread{false};
    std::atomic<bool>                     m_pauseThread{true};

    wxWebView* m_browser;
    long m_zoomFactor;

    DeviceButton *                        add_button;
    DeviceButton *                        delete_button;
    DeviceButton *                        edit_button;
    DeviceButton *                        refresh_button;
    DeviceButton *                        login_button;
    bool                                  m_isloginin;
    DeviceSwitchButton*                        toggleBar;
    wxStaticBitmap *                      staticBitmap;

    std::map<std::string, DynamicPrintConfig> m_machine;
    std::string select_machine_name;
    WebState webisNetMode = isDisconnect;
    std::set<std::string> m_exit_host;
};

// y13
class RoundButton : public wxButton
{
public:
    RoundButton(wxWindow* parent, wxWindowID id, const wxString& label, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize)
        : wxButton(parent, id, label, pos, size, wxBORDER_NONE)
    {
        m_hovered = false;
        Bind(wxEVT_ENTER_WINDOW, &RoundButton::OnMouseEnter, this);
        Bind(wxEVT_LEAVE_WINDOW, &RoundButton::OnMouseLeave, this);
        Bind(wxEVT_PAINT, &RoundButton::OnPaint, this);
    }

protected:
    void OnMouseEnter(wxMouseEvent& event)
    {
        m_hovered = true;
        Refresh();
    }

    void OnMouseLeave(wxMouseEvent& event)
    {
        m_hovered = false;
        Refresh();
    }

    void OnPaint(wxPaintEvent& event)
    {
        wxPaintDC dc(this);


        wxSize size = GetClientSize();
        int x = size.x;
        int y = size.y;

        if (m_hovered)
        {
            dc.SetBrush(wxBrush(wxColour(60, 60, 63), wxBRUSHSTYLE_SOLID));  
        }
        else
        {
            dc.SetBrush(wxBrush(GetBackgroundColour(), wxBRUSHSTYLE_SOLID));  
        }

        dc.SetPen(wxPen(GetBackgroundColour(), 1, wxPENSTYLE_TRANSPARENT));


        wxBitmap img = GetBitmap();
        wxSize img_size = img.GetSize();
  
        int radius = std::max(img_size.x, img_size.y) / 2;
        dc.DrawCircle(size.x / 2, size.y / 2, radius + 5);
        dc.DrawBitmap(img, (size.x - img_size.x) / 2, (size.y - img_size.y) / 2);
    }

private:
    bool m_hovered;  
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
