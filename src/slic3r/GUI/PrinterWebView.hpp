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
#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#endif

#include <boost/thread.hpp>


#include <atomic>
#include "./Widgets/SwitchButton.hpp"
#include "./Widgets/DeviceButton.hpp"

#include "OctoPrint.hpp"




namespace Slic3r {
namespace GUI {

//y28
enum WebState
{
    isDisconnect,
    isLocalWeb,
	isNetWeb
};
//cj_2
class StatusPanel;
//cj_2
class QDSDeviceManager;

class PrinterWebView : public wxPanel {
public:
    PrinterWebView(wxWindow *parent);
    virtual ~PrinterWebView();





    wxBoxSizer *init_menu_bar(wxPanel *Panel);
    void        init_scroll_window(wxPanel *Panel);
    void        CreatThread();
    void load_url(wxString& url);
    void        load_net_url(wxString& url, wxString& ip);
    void UpdateState();
    void OnClose(wxCloseEvent& evt);

    void        OnRefreshButtonClick(wxCommandEvent &event);
    void OnAddButtonClick(wxCommandEvent &event);
    void OnDeleteButtonClick(wxCommandEvent &event);
    void OnEditButtonClick(wxCommandEvent &event);

    void RunScript(const wxString &javascript);
    //void OnScriptMessageReceived(wxWebViewEvent &event);
    void OnScriptMessage(wxWebViewEvent &evt);
    void UpdateLayout();
    void OnScroll(wxScrollWinEvent &event);
    void OnScrollup(wxScrollWinEvent &event);
    void OnScrolldown(wxScrollWinEvent &event);

    void onStatusPanelTask(wxCommandEvent& event);
    //cj_1
    void onSetBoxTask(wxCommandEvent& event);
    //cj_1
    void onRefreshRfid(wxCommandEvent& event);
    //void SendRecentList(int images);
    void SetButtons(std::vector<DeviceButton *> buttons);
    void  AddButton(const wxString &                           device_name,
                    const wxString &                           ip,
                    const wxString &                           machine_type,
                    const wxString &                           fullname,
                    bool                                       isSelected,
                   bool                                         isQIDI,
                    const wxString &                            apikey);
    void                        DeleteButton();
    void                        UnSelectedButton();
    void ShowNetPrinterButton();
    void ShowLocalPrinterButton();
#if QDT_RELEASE_TO_PUBLIC
    void AddNetButton(const NetDevice device);
#endif

    void DeleteNetButton();
    void                        RefreshButton();
    void SetUpdateHandler(const std::function<void(wxCommandEvent &)> &handler) { m_handlerl = handler; }
    void SetPresetChanged(bool status);
    void SetLoginStatus(bool status);
    std::string NormalizeVendor(const std::string& str);


    std::vector<DeviceButton *> GetButton() { return m_buttons; };
    bool                        GetNetMode() { return m_isNetMode; };
    std::vector<DeviceButton *> GetNetButton() { return m_net_buttons; };
    wxString GetWeburl(){ return m_web; };

    //y53
    wxString GetWebIp(){return m_ip;};
    bool IsNetUrl() {return webisNetMode == isNetWeb;};
    void load_disconnect_url();
    void FormatNetUrl(std::string link_url, std::string local_ip, bool isSpecialMachine);
    void FormatUrl(std::string link_url);

    //y74
    QDSDeviceManager* m_device_manager;

    void onSSEMessageHandle(const std::string& event, const std::string& data);

    //y76
    void pauseCamera();

private:
    // cj_1
	void HideDeviceButtons(std::vector<DeviceButton*>& buttons);
	void HideAllDeviceButtons();

    // cj_1
	void cancelAllDevButtonSelect();

    // cj_1
    void clearStatusPanelData();

    void ShowDeviceButtons(std::vector<DeviceButton*>& buttons, bool isShow = true);


	void updateDeviceButton(const std::string& device_id, std::string new_status);
	void updateDeviceParameter(const std::string& device_id);
    void updateDeviceConnectType(const std::string& device_id, const std::string& device_ip);
	void InitDeviceManager();
	void initEventToTaskPath();
	void bindTaskHandle();
    void init_select_machine();

private:
    wxBoxSizer *leftallsizer;

    wxBoxSizer *                          devicesizer;
    wxBoxSizer *                          allsizer;
    bool                                  m_isNetMode    = false;

    int height = 0; 
    wxString  m_web;
    wxString m_ip;
    std::function<void(wxCommandEvent &)> m_handlerl;
    std::function<void(wxCommandEvent &)> m_delete_handlerl;

    wxScrolledWindow *          leftScrolledWindow;
    wxPanel *         leftPanel;

    StatusPanel* t_status_page;
    std::vector<DeviceButton *>           m_buttons;
    std::vector<DeviceButton *>           m_net_buttons;


    std::string                           m_select_type;

    wxWebView* m_browser;
    long m_zoomFactor;

    DeviceButton *                        add_button;
    DeviceButton *                        delete_button;
    DeviceButton *                        edit_button;
    DeviceButton *                        refresh_button;
    wxStaticBitmap *                      staticBitmap;

    std::map<std::string, DynamicPrintConfig> m_machine;
    std::string select_machine_name{ "" };
    //std::string m_teststr;
    std::string m_cur_deviceId{ "" };
    WebState webisNetMode = isDisconnect;
    std::set<std::string> m_exit_host;
    bool m_isfluidd_1;             //y35

    //y74
    wxSimplebook* m_status_book;
    std::mutex m_ui_map_mutex;
    std::unordered_map<std::string, DeviceButton*> m_device_id_to_button;

	std::map<wxEventType, std::string> m_eventToTaskPath;
    std::map<wxEventType, std::string> m_boxEventToTaskPath;
    std::map<wxEventType, std::string> m_localEventToTaskPath;
    std::string m_userInfo;

#if QDT_RELEASE_TO_PUBLIC
    std::vector<NetDevice> m_net_devices;
    Environment m_env;

#endif
    std::atomic<bool> m_isloginin{false};
    

    //cj_2
    wxPanel* m_localPanel;
    DeviceButton* m_localDeviceExpand;
    wxStaticText* m_localTabel;
    bool m_localIsExpand{ true };

	wxPanel* m_netPanel;
    DeviceButton* m_netDeviceExpand;
    wxStaticText* m_netTable;
	bool m_netIsExpand{ true };


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
