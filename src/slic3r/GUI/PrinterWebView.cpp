#include "PrinterWebView.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>

#include <slic3r/GUI/Widgets/WebView.hpp>

#include "PhysicalPrinterDialog.hpp"
//B45
#include <wx/regex.h>
#include <boost/regex.hpp>
#include <wx/graphics.h>
//B55
#include "../Utils/PrintHost.hpp"

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {
// //B45
PrinterWebView::PrinterWebView(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    m_isSimpleMode = wxGetApp().app_config->get("machine_list_minification") == "1";
    m_isNetMode    = wxGetApp().app_config->get("machine_list_net") == "1";
    // y21
    wxGetApp().get_login_info();
    m_isloginin = wxGetApp().is_QIDILogin();
    //y33
    if (m_isloginin)
        m_user_head_name = wxGetApp().app_config->get("user_head_name");

    m_select_type  = "null";

    allsizer = new wxBoxSizer(wxHORIZONTAL);
    devicesizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *buttonSizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *menuPanelSizer = new wxBoxSizer(wxVERTICAL);
    leftallsizer               = new wxBoxSizer(wxVERTICAL);

    devicesizer->SetMinSize(wxSize(300, -1));

    devicesizer->Layout();

    devicesizer->Add(0, 3);

    init_scroll_window(this);

    wxPanel *titlePanel = new wxPanel(this, wxID_ANY);
    titlePanel->SetBackgroundColour(wxColour(38, 38, 41));
    // y13
    wxPanel *menuPanel = new wxPanel(titlePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxTAB_TRAVERSAL | wxBU_RIGHT);
    menuPanel->SetSizer(menuPanelSizer);
    menuPanel->SetBackgroundColour(wxColour(51, 51, 55));

    wxBoxSizer *loginsizer = init_login_bar(titlePanel);

    buttonSizer->Add(0, 10);
    buttonSizer->Add(loginsizer, wxSizerFlags().Border(wxALL, 1).Expand());
    buttonSizer->Add(0, 10);

    wxBoxSizer *menu_bar_sizer = new wxBoxSizer(wxHORIZONTAL);

    menu_bar_sizer = init_menu_bar(menuPanel);

    titlePanel->SetSizer(buttonSizer);

    toggleBar = new DeviceSwitchButton(menuPanel);
    toggleBar->SetSize(327);
    toggleBar->SetMaxSize({em_unit(this) * 40, -1});
    toggleBar->SetLabels(_L("Local"), _L("Link"));
    toggleBar->SetValue(m_isNetMode);
    toggleBar->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &evt) {
        bool is_checked = evt.GetInt();
        toggleBar->SetValue(is_checked);

        m_isNetMode = is_checked;
        if (!m_isNetMode) {
            wxGetApp().app_config->set("machine_list_net", "0");
            ShowLocalPrinterButton();
            //y23 //y24
            if (into_u8(m_web).find("missing_connection") != std::string::npos)
            {
                //y30
                wxString url = wxString::Format("file://%s/web/qidi/missing_connection.html", from_u8(resources_dir()));
                wxString strlang = wxGetApp().current_language_code_safe();
                if (strlang != "")
                    url = wxString::Format("file://%s/web/qidi/missing_connection.html?lang=%s", from_u8(resources_dir()), strlang);
                load_disconnect_url(url);
            }
        } else {
            wxGetApp().app_config->set("machine_list_net", "1");
            ShowNetPrinterButton();
            //y23 //y24
            if (into_u8(m_web).find("missing_connection") != std::string::npos)
            {
                //y30
                wxString url = wxString::Format("file://%s/web/qidi/link_missing_connection.html", from_u8(resources_dir()));
                wxString strlang = wxGetApp().current_language_code_safe();
                if (strlang != "")
                    url = wxString::Format("file://%s/web/qidi/link_missing_connection.html?lang=%s", from_u8(resources_dir()), strlang);
                load_disconnect_url(url);
            }
        }
        leftScrolledWindow->Scroll(0, 0);
        UpdateLayout();
        UpdateState();
    });

    menuPanelSizer->Add(toggleBar);

    menuPanelSizer->Add(menu_bar_sizer, wxSizerFlags(0).Expand().Align(wxALIGN_TOP).Border(wxALL, 0));
    menuPanelSizer->Add(0, 5);

    buttonSizer->Add(menuPanel, wxSizerFlags(1).Expand());

    titlePanel->Layout();


    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    SetSizer(allsizer);

    leftallsizer->Add(titlePanel, wxSizerFlags(0).Expand());
    leftallsizer->Add(leftScrolledWindow, wxSizerFlags(1).Expand());

    // y13
    wxPanel* line_area = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(1,10));
    line_area->SetBackgroundColour(wxColor(66, 66, 69));

    allsizer->Add(leftallsizer, wxSizerFlags(0).Expand());
    allsizer->Add(line_area, 0, wxEXPAND);
    allsizer->Add(m_browser, wxSizerFlags(1).Expand().Border(wxALL, 0));

    // Zoom
    m_zoomFactor = 100;

    // B45
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterWebView::OnScriptMessage, this);

    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);

    // B64
    if (m_isSimpleMode) {
        arrow_button->SetBitmap(create_scaled_bitmap("arrow-right-s-line", nullptr, 25));
        devicesizer->SetMinSize(wxSize(190, -1));
        toggleBar->SetSize(237);
        leftScrolledWindow->SetMinSize(wxSize(190, -1));
        devicesizer->Layout();
        leftScrolledWindow->Layout();
        buttonSizer->Layout();
        allsizer->Layout();
    }
    // B64
    // SetPresetChanged(true);
    SetLoginStatus(m_isloginin);
    // CreatThread();
}
wxBoxSizer *PrinterWebView::init_login_bar(wxPanel *Panel)
{
    wxBoxSizer *buttonsizer = new wxBoxSizer(wxHORIZONTAL);
    //y33
    if (m_isSimpleMode)
        staticBitmap = new wxStaticBitmap(Panel, wxID_ANY, create_scaled_bitmap_of_login(m_user_head_name, this, 40));
    else 
        staticBitmap = new wxStaticBitmap(Panel, wxID_ANY, create_scaled_bitmap_of_login(m_user_head_name, this, 60));


    StateColor  text_color(std::pair<wxColour, int>(wxColour(57, 57, 61), StateColor::Disabled),
                          std::pair<wxColour, int>(wxColour(68, 121, 251), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(68, 121, 251), StateColor::Hovered),
                          std::pair<wxColour, int>(wxColour(198, 198, 200), StateColor::Normal));

    StateColor btn_bg(std::pair<wxColour, int>(wxColour(38, 38, 41), StateColor::Disabled),
                      std::pair<wxColour, int>(wxColour(38, 38, 41), StateColor::Pressed),
                      std::pair<wxColour, int>(wxColour(38, 38, 41), StateColor::Hovered),
                      std::pair<wxColour, int>(wxColour(38, 38, 41), StateColor::Normal));

    login_button = new DeviceButton(Panel, _L("Login/Register"), "", wxBU_LEFT);
    login_button->SetTextColor(text_color);
    login_button->SetBackgroundColor(btn_bg);
    login_button->SetBorderColor(btn_bg);
    login_button->SetCanFocus(false);
    login_button->SetIsSimpleMode(m_isSimpleMode);
    login_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnLoginButtonClick, this);
    buttonsizer->AddSpacer(5);
    buttonsizer->Add(staticBitmap);
    buttonsizer->Add(login_button, wxSizerFlags().Border(wxALL, 1).Expand());

    return buttonsizer;
}
wxBoxSizer *PrinterWebView::init_menu_bar(wxPanel *Panel)
{
    wxBoxSizer *buttonsizer = new wxBoxSizer(wxHORIZONTAL);

    StateColor add_btn_bg(std::pair<wxColour, int>(wxColour(57, 57, 61), StateColor::Disabled),
                          std::pair<wxColour, int>(wxColour(138, 138, 141), StateColor::Pressed),
                          std::pair<wxColour, int>(wxColour(85, 85, 90), StateColor::Hovered),
                          std::pair<wxColour, int>(wxColour(74, 74, 79), StateColor::Normal));

    // B63
    add_button = new DeviceButton(Panel, "add_machine_list_able", wxBU_LEFT);
    add_button->SetBackgroundColor(add_btn_bg);
    add_button->SetBorderColor(wxColour(57, 51, 55));
    add_button->SetCanFocus(false);
    buttonsizer->Add(add_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 1));
    add_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnAddButtonClick, this);

    // B63
    delete_button = new DeviceButton(Panel, "delete_machine_list_able", wxBU_LEFT);
    delete_button->SetBackgroundColor(add_btn_bg);
    delete_button->SetBorderColor(wxColour(57, 51, 55));
    delete_button->SetCanFocus(false);
    buttonsizer->Add(delete_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 1));
    delete_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnDeleteButtonClick, this);

    // B63
    edit_button = new DeviceButton(Panel, "edit_machine_list_able", wxBU_LEFT);
    edit_button->SetBackgroundColor(add_btn_bg);
    edit_button->SetBorderColor(wxColour(57, 51, 55));
    edit_button->SetCanFocus(false);
    buttonsizer->Add(edit_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 1));
    edit_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnEditButtonClick, this);

    refresh_button = new DeviceButton(Panel, "refresh_machine_list_able", wxBU_LEFT);
    refresh_button->SetBackgroundColor(add_btn_bg);
    refresh_button->SetBorderColor(wxColour(57, 51, 55));
    refresh_button->SetCanFocus(false);
    buttonsizer->Add(refresh_button, wxSizerFlags().Align(wxALIGN_LEFT).CenterVertical().Border(wxALL, 1));
    refresh_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnRefreshButtonClick, this);

    text_static = new wxStaticText(Panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
    text_static->SetForegroundColour(wxColour(255, 255, 255));
    text_static->SetFont(wxFont(wxFontInfo(18).Bold()));
    text_static->SetMinSize(wxSize(300 - 11 * add_button->GetSize().GetWidth(), -1));
    buttonsizer->Add(text_static, wxSizerFlags().Align(wxALIGN_LEFT).Border(wxALL, 5));

    if (m_isSimpleMode)
        text_static->Hide();

    arrow_button = new RoundButton(Panel, wxID_ANY, "", wxDefaultPosition, wxSize(35, 35));
    arrow_button->SetBackgroundColour(Panel->GetBackgroundColour());
    arrow_button->SetForegroundColour(Panel->GetBackgroundColour());
    // arrow_button->SetMinSize(wxSize(40, -1));
    if (m_isSimpleMode)
        arrow_button->SetBitmap(create_scaled_bitmap("arrow-right-s-line", nullptr, 25));
    else
        arrow_button->SetBitmap(create_scaled_bitmap("arrow-left-s-line", nullptr, 25));
    buttonsizer->AddStretchSpacer(1);
    buttonsizer->Add(arrow_button, wxSizerFlags().Align(wxALIGN_RIGHT).CenterVertical().Border(wxALL, 1));
    arrow_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnZoomButtonClick, this);

    buttonsizer->Layout();

    return buttonsizer;
}
 void PrinterWebView::init_scroll_window(wxPanel* Panel) {
     leftScrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
     leftScrolledWindow->SetBackgroundColour(wxColour(38, 38, 41));
     leftScrolledWindow->SetSizer(devicesizer);
     leftScrolledWindow->SetScrollRate(10, 10);
     leftScrolledWindow->SetMinSize(wxSize(300, -1));
     leftScrolledWindow->FitInside();
     leftScrolledWindow->Bind(wxEVT_SCROLLWIN_TOP, &PrinterWebView::OnScroll, this);
     leftScrolledWindow->Bind(wxEVT_SCROLLWIN_BOTTOM, &PrinterWebView::OnScroll, this);
     leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEUP, &PrinterWebView::OnScroll, this);
     leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEDOWN, &PrinterWebView::OnScroll, this);
     leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEUP, &PrinterWebView::OnScroll, this);
     leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &PrinterWebView::OnScroll, this);
 }
 void PrinterWebView::CreatThread() {
     std::thread myThread([&]() {
         while (true) {
             for (DeviceButton *button : m_buttons) {
                 if (m_stopThread)
                     break;
                 if (m_pauseThread)
                     break;

                 BOOST_LOG_TRIVIAL(error) << "machine IP: " << (button->getIPLabel());
                 std::string printer_host = button->getIPLabel().ToStdString();
                 DynamicPrintConfig cfg_t;
                 cfg_t.set_key_value("print_host", new ConfigOptionString(printer_host));
                 cfg_t.set_key_value("host_type", new ConfigOptionString("ptfff"));
                 cfg_t.set_key_value("printhost_apikey", new ConfigOptionString(into_u8(button->GetApikey())));
                 std::unique_ptr<PrintHost> printhost(PrintHost::get_print_host(&cfg_t));
                 if (!printhost) {
                     BOOST_LOG_TRIVIAL(error) << ("Could not get a valid Printer Host reference");
                     return;
                 }
                 wxString    msg;
                 std::string state    = "standby";
                 float       progress = 0;
                 std::pair<std::string, float> state_progress = printhost->get_status_progress(msg);
                 state                = state_progress.first;

                 if ((button->GetStateText()).ToStdString() != state)
                     button->SetStateText(state);

                 if (state == "printing") {
                     progress        = state_progress.second * 100;
                     int progressInt = static_cast<int>(progress);
                     button->SetProgressText(wxString::Format(wxT("(%d%%)"), progressInt));
                 }
             }
 #if QDT_RELEASE_TO_PUBLIC
             auto m_devices = wxGetApp().get_devices();
             int  count     = 0;
             for (const auto &device : m_devices) {
                 if (m_stopThread)
                     break;
                 if (m_pauseThread)
                     break;
                 if (!m_net_buttons.empty()) {
                     BOOST_LOG_TRIVIAL(error) << "machine IP: " << device.local_ip;
                     std::unique_ptr<PrintHost> printhost(PrintHost::get_print_host_url(device.url, device.local_ip));
                     if (!printhost) {
                         BOOST_LOG_TRIVIAL(error) << ("Could not get a valid Printer Host reference");
                         return;
                     }
                     wxString    msg;
                     std::string state    = "standby";
                     float       progress = 0;
                     std::pair<std::string, float> state_progress = printhost->get_status_progress(msg);
                     state                = state_progress.first;
                     BOOST_LOG_TRIVIAL(error) << ("State:", state);
                     if ((m_net_buttons[count]->GetStateText()).ToStdString() != state)
                         m_net_buttons[count]->SetStateText(state);

                     if (state == "printing") {
                         progress        = state_progress.second * 100;
                         int progressInt = static_cast<int>(progress);
                         m_net_buttons[count]->SetProgressText(wxString::Format(wxT("(%d%%)"), progressInt));
                     }
                     count++;
                 }
             }
 #endif
             if (m_stopThread)
                 break;
             boost::this_thread::sleep(boost::posix_time::seconds(1));
         }
     });
     m_statusThread = std::move(myThread);
 }
 void PrinterWebView::SetPresetChanged(bool status) {
     if (status) {
         StopStatusThread();
         m_stopThread = false;
         DeleteButton();
         DeleteNetButton();
         m_machine.clear();
         m_exit_host.clear();
         //ShowLocalPrinterButton();
         PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
         PhysicalPrinterCollection& ph_printers = preset_bundle.physical_printers;
         std::set<std::string> qidi_printers = preset_bundle.get_vendors();
         std::string actice_url = "";
         for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
             std::string host = (it->config.opt_string("print_host"));
             std::string apikey = (it->config.opt_string("printhost_apikey"));
             std::string preset_name = (it->config.opt_string("preset_name"));
             bool isQIDI_printer = false;
             if (qidi_printers.find(preset_name) != qidi_printers.end())
                 isQIDI_printer = true;

             std::string full_name = it->get_full_name(preset_name);
             std::string model_id;
             if (isQIDI_printer)
                 model_id = preset_name;
             else
                 model_id = "my_printer";
             const DynamicPrintConfig* cfg_t = &(it->config);

             const auto        opt = cfg_t->option<ConfigOptionEnum<PrintHostType>>("host_type");
             auto       host_type = opt != nullptr ? opt->value : htOctoPrint;

             // y13
             bool is_selected = false;
             if (!select_machine_name.empty())
                 if (select_machine_name == it->get_short_name(full_name))
                 {
                     is_selected = true;
                     select_machine_name = "";
                 }


             //BOOST_LOG_TRIVIAL(error) << preset_name;
             //BOOST_LOG_TRIVIAL(error) << full_name;
             //BOOST_LOG_TRIVIAL(error) << (it->get_short_name(full_name));
             //BOOST_LOG_TRIVIAL(error) << (it->get_preset_name(full_name));
             //BOOST_LOG_TRIVIAL(error) << model_id;
             AddButton((it->get_short_name(full_name)), host, model_id, full_name, is_selected,
                 (host_type == htOctoPrint), apikey);
             m_machine.insert(std::make_pair((it->get_short_name(full_name)), *cfg_t));
             //y25
             m_exit_host.insert(host);
         }
#if QDT_RELEASE_TO_PUBLIC
         auto m_devices = wxGetApp().get_devices();

         for (const auto& device : m_devices) {
             AddNetButton(device);
         }
#endif
         CreatThread();
     }
    //y40
     if(m_isNetMode)
         ShowNetPrinterButton();
     else
         ShowLocalPrinterButton();

    if (webisNetMode == isNetWeb) {
        for (DeviceButton* button : m_net_buttons) {
            if (button->getIPLabel().find(m_ip) != std::string::npos) {
                button->SetIsSelected(true);
                break;
            }
        }
    } else if (webisNetMode == isLocalWeb) {
        for (DeviceButton* button : m_buttons) {
            if (button->getIPLabel().find(m_ip) != std::string::npos) {
                button->SetIsSelected(true);
                break;
            }
        }
    }
    else
    {
        wxString strlang = wxGetApp().current_language_code_safe();
        if (m_isNetMode)
        {
        //y30
            wxString url = wxString::Format("file://%s/web/qidi/link_missing_connection.html", from_u8(resources_dir()));
            if(strlang != "")
                url = wxString::Format("file://%s/web/qidi/link_missing_connection.html?lang=%s", from_u8(resources_dir()), strlang);
            load_disconnect_url(url);
        }
        else
        {
        //y30
            wxString url = wxString::Format("file://%s/web/qidi/missing_connection.html", from_u8(resources_dir()));
            if (strlang != "")
                url = wxString::Format("file://%s/web/qidi/missing_connection.html?lang=%s", from_u8(resources_dir()), strlang);
            load_disconnect_url(url);
        }
    }
    UpdateState();
    UpdateLayout();
 }
 void PrinterWebView::SetLoginStatus(bool status) { 
     m_isloginin = status;
     if (m_isloginin) {
         /*login_button->SetLabel("123456");*/
 #if QDT_RELEASE_TO_PUBLIC
         wxString    msg;
         QIDINetwork m_qidinetwork;
         std::string name = m_qidinetwork.user_info(msg);
         // y1
         wxString wxname = from_u8(name);
         login_button->SetLabel(wxname);
         std::vector<Device> m_devices = m_qidinetwork.get_device_list(msg);
         //y33
         m_user_head_name = wxGetApp().app_config->get("user_head_name");
         SetPresetChanged(true);
 #endif
         m_isloginin = true;
         UpdateState();
     } else {
         login_button->SetLabel(_L("Login/Register"));
 #if QDT_RELEASE_TO_PUBLIC
         std::vector<Device> devices;
         wxGetApp().set_devices(devices);
 #endif
        // y28
         if (webisNetMode == isNetWeb)
             webisNetMode = isDisconnect;
        m_user_head_name = "";
         SetPresetChanged(true);
         UpdateState();
         wxGetApp().get_login_info();
     }
 }


 PrinterWebView::~PrinterWebView()
 {
     BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
     SetEvtHandlerEnabled(false);
     BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
 }

// // B55
 void PrinterWebView::AddButton(const wxString &    device_name,
                                const wxString &    ip,
                                const wxString &    machine_type,
                                const wxString &    fullname,
                                bool                isSelected,
                                bool                isQIDI,
                                const wxString&     apikey)
 {
     wxString Machine_Name = Machine_Name.Format("%s%s", machine_type, "_thumbnail");

     StateColor mac_btn_bg(std::pair<wxColour, int>(wxColour(147, 147, 150), StateColor::Pressed),
                     std::pair<wxColour, int>(wxColour(76, 76, 80), StateColor::Hovered),
                     std::pair<wxColour, int>(wxColour(67, 67, 71), StateColor::Normal));

     //y48
     wxString machine_icon_path = from_u8(Slic3r::resources_dir() + "/" + "profiles" + "/" + "thumbnail" + "/") + Machine_Name;

     DeviceButton *machine_button = new DeviceButton(leftScrolledWindow, fullname, machine_icon_path, wxBU_LEFT, wxSize(80, 80), device_name, ip, apikey);
     machine_button->SetBackgroundColor(mac_btn_bg);
     machine_button->SetBorderColor(wxColour(57, 51, 55));
     machine_button->SetCanFocus(false);
     machine_button->SetIsSimpleMode(m_isSimpleMode);

     machine_button->Bind(wxEVT_BUTTON, [this, ip](wxCommandEvent &event) {
         FormatUrl(into_u8(ip));
     });
     devicesizer->Add(machine_button, wxSizerFlags().Border(wxALL, 1).Expand());
     devicesizer->Layout();
     m_buttons.push_back(machine_button);

     // y13
     if (isSelected)
         FormatUrl(into_u8(ip));
 }
// y22
 std::string PrinterWebView::NormalizeVendor(const std::string& str) 
 {
     std::string normalized;
     for (char c : str) {
         if (std::isalnum(c)) {
             normalized += std::tolower(c);
         }
     }
     return normalized;
 }

 #if QDT_RELEASE_TO_PUBLIC
 void PrinterWebView::AddNetButton(const Device device)
 {
     // y2
     std::set<std::string> qidi_printers = wxGetApp().preset_bundle->get_vendors();

     wxString    Machine_Name;
     wxString    device_name;
     // y22
     if (!device.machine_type.empty()) 
     {
         device_name = device.device_name;
         std::string extracted = device.machine_type;
         for (std::string machine_vendor : qidi_printers)
         {
             if (NormalizeVendor(machine_vendor) == NormalizeVendor(extracted))
             {
                 Machine_Name = Machine_Name.Format("%s%s", machine_vendor, "_thumbnail");
                 break;
             }
         }
     }
     else
     {
         device_name = device.device_name;
         std::size_t found = device.device_name.find('@');
         if (found != std::string::npos)
         {
            std::string extracted = device.device_name.substr(found + 1);
            for (std::string machine_vendor : qidi_printers)
            {
                if (NormalizeVendor(machine_vendor) == NormalizeVendor(extracted))
                {
                    Machine_Name = Machine_Name.Format("%s%s", machine_vendor, "_thumbnail");
                    break;
                }
            }
         }
     }

     if (Machine_Name.empty())
     {
         Machine_Name = Machine_Name.Format("%s%s", "my_printer", "_thumbnail");
     }

     StateColor mac_btn_bg(std::pair<wxColour, int>(wxColour(147, 147, 150), StateColor::Pressed),
                     std::pair<wxColour, int>(wxColour(76, 76, 80), StateColor::Hovered),
                     std::pair<wxColour, int>(wxColour(67, 67, 71), StateColor::Normal));
     QIDINetwork m_qidinetwork;

     //y48
     wxString machine_icon_path = from_u8(Slic3r::resources_dir() + "/" + "profiles" + "/" + "thumbnail" + "/") + Machine_Name;

     // device_name                  = m_qidinetwork.UTF8ToGBK(device_name.c_str());
     DeviceButton *machine_button = new DeviceButton(leftScrolledWindow, device.device_name, machine_icon_path, wxBU_LEFT, wxSize(80, 80),

                                                     device_name, device.local_ip);
     machine_button->SetBackgroundColor(mac_btn_bg);
     machine_button->SetBorderColor(wxColour(57, 51, 55));
     machine_button->SetCanFocus(false);
     machine_button->SetIsSimpleMode(m_isSimpleMode);

     machine_button->Bind(wxEVT_BUTTON, [this, device](wxCommandEvent &event) {
         FormatNetUrl(device.link_url, device.local_ip, device.isSpecialMachine);
     });

     devicesizer->Add(machine_button, wxSizerFlags().Border(wxALL, 1).Expand());
     devicesizer->Layout();
     m_net_buttons.push_back(machine_button);
}
 #endif

 void PrinterWebView::RefreshButton()
 {
     Refresh();
     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_buttons) {
             button->Refresh();
         }
         add_button->Refresh();
         delete_button->Refresh();
         edit_button->Refresh();
         refresh_button->Refresh();
         login_button->Refresh();
     }
 }
 void PrinterWebView::UnSelectedButton()
 {
     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_buttons) {
             button->SetIsSelected(false);
         }
     }
 }
 void PrinterWebView::DeleteButton()
 {
     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_buttons) {
             delete button;
         }
         m_buttons.clear();
     }
 }
 void PrinterWebView::DeleteNetButton()
 {
     if (m_net_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_net_buttons) {
             delete button;
         }
         m_net_buttons.clear();
     }
 }
 void PrinterWebView::ShowNetPrinterButton()
 {
     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_buttons) {
             button->Hide();
         }
     }
     if (m_net_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_net_buttons) {
             button->Show();
         }
     }
     leftScrolledWindow->Layout();
     Refresh();
 }
 void PrinterWebView::ShowLocalPrinterButton()
 {
     if (m_net_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_net_buttons) {
             button->Hide();
         }
     }
     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_buttons) {
             button->Show();
                
         }
     }
     leftScrolledWindow->Layout();
     Refresh();
 }
 void PrinterWebView::SetButtons(std::vector<DeviceButton *> buttons) { m_buttons = buttons; }
 void PrinterWebView::OnZoomButtonClick(wxCommandEvent &event)
 {
     m_isSimpleMode = !m_isSimpleMode;
     if (!m_isSimpleMode) {
         text_static->Show();
         //y33
         staticBitmap->SetBitmap(create_scaled_bitmap_of_login(m_user_head_name, this, 60));
         login_button->SetIsSimpleMode(m_isSimpleMode);
         wxGetApp().app_config->set("machine_list_minification", "0");
         toggleBar->SetSize(327);
         devicesizer->SetMinSize(wxSize(300, -1));
         leftScrolledWindow->SetMinSize(wxSize(300, -1));
         arrow_button->SetBitmap(create_scaled_bitmap("arrow-left-s-line", nullptr,25));
         for (DeviceButton *button : m_buttons) {
             button->SetIsSimpleMode(m_isSimpleMode);
         }
         for (DeviceButton *button : m_net_buttons) {
             button->SetIsSimpleMode(m_isSimpleMode);
         }
     } else {
         text_static->Hide();
         //y33
        staticBitmap->SetBitmap(create_scaled_bitmap_of_login(m_user_head_name, this, 40));
         login_button->SetIsSimpleMode(m_isSimpleMode);

         wxGetApp().app_config->set("machine_list_minification", "1");
         toggleBar->SetSize(237);
         arrow_button->SetBitmap(create_scaled_bitmap("arrow-right-s-line", nullptr, 25));
         devicesizer->SetMinSize(wxSize(190, -1));
         leftScrolledWindow->SetMinSize(wxSize(190, -1));
         for (DeviceButton *button : m_buttons) {
             button->SetIsSimpleMode(m_isSimpleMode);
         }
         for (DeviceButton *button : m_net_buttons) {
             button->SetIsSimpleMode(m_isSimpleMode);
         }
     }
    // y22
     devicesizer->Layout();

     leftScrolledWindow->Layout();

     allsizer->Layout();
     UpdateLayout();
 }
 void PrinterWebView::OnRefreshButtonClick(wxCommandEvent &event)
 {
     PresetBundle &preset_bundle = *wxGetApp().preset_bundle;

     PhysicalPrinterCollection &ph_printers = preset_bundle.physical_printers;

     std::vector<std::string> vec1;
     std::vector<std::string> vec2;
     for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
         for (const std::string &preset_name : it->get_preset_names()) {
             std::string full_name = it->get_full_name(preset_name);
             vec1.push_back(full_name);
         }
     }

     for (DeviceButton *button : m_buttons) {
         vec2.push_back(button->GetLabel().ToStdString());
     }

     bool result1 = std::equal(vec1.begin(), vec1.end(), vec2.begin(), vec2.end());
     vec1.clear();
     vec2.clear();
     bool result2 = true;
#if QDT_RELEASE_TO_PUBLIC
             wxString    msg;
             QIDINetwork m_qidinetwork;
             m_qidinetwork.get_device_list(msg);
             auto m_devices = wxGetApp().get_devices();
             for (const auto &device : m_devices) {
                 vec1.push_back(device.device_name);
             }
             for (DeviceButton *button : m_net_buttons) {
                 vec2.push_back(button->GetLabel().ToStdString());
             }
             result2 = std::equal(vec1.begin(), vec1.end(), vec2.begin(), vec2.end());
     #endif
     SetPresetChanged(!result1 || !result2);
     
     Refresh();
 }
 void PrinterWebView::OnLoginButtonClick(wxCommandEvent &event)
 {
     // B64
     wxGetApp().ShowUserLogin(true);
     devicesizer->Layout();

     leftScrolledWindow->Layout();

     allsizer->Layout();
     UpdateLayout();
 }
 void PrinterWebView::OnAddButtonClick(wxCommandEvent &event)
 {
    //y25
     PhysicalPrinterDialog dlg(this->GetParent(), "", m_machine, m_exit_host);
     if (dlg.ShowModal() == wxID_YES) {
         if (m_handlerl) {
             m_handlerl(event);
         }
         select_machine_name = dlg.get_name();
         SetPresetChanged(true);
         UpdateLayout();
         Refresh();
     }
 }
 void PrinterWebView::OnDeleteButtonClick(wxCommandEvent &event)
 {
     if (m_select_type == "local") {
         PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
         for (DeviceButton *button : m_buttons) {
             if ((button->GetIsSelected())) {
                 wxString msg;

 #if defined(__WIN32__)
                 msg += format_wxstr(_L("Are you sure you want to delete \"%1%\" printer?"), (button->GetLabel()));
 #else
                 msg += _L("Are you sure you want to delete ") + (button->GetLabel()) + _L("printer?");
 #endif
                 if (MessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() !=
                     wxID_YES)
                     return;
                 // y1
                 preset_bundle.physical_printers.select_printer(into_u8(button->GetLabel()));

                 preset_bundle.physical_printers.delete_selected_printer();
                 webisNetMode = isDisconnect;
                 SetPresetChanged(true);

                 UpdateLayout();
                 Refresh();
                 break;
             }
         }
         if (m_handlerl) {
             m_handlerl(event);
         }
     } else if (m_select_type == "net") {
         for (DeviceButton *button : m_net_buttons) {
             if ((button->GetIsSelected())) {
                 wxString msg;

 #if defined(__WIN32__)
                 msg += format_wxstr(_L("Are you sure you want to delete \"%1%\" printer?"), (button->GetLabel()));
 #else
                 msg += _L("Are you sure you want to delete ") + (button->GetLabel()) + _L("printer?");
 #endif
                 if (MessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() !=
                     wxID_YES)
                     return;
 #if QDT_RELEASE_TO_PUBLIC

                 auto devices = wxGetApp().get_devices();
                 for (const auto &device : devices) {
                     if (device.local_ip == (button->getIPLabel())) {
                         wxString    msg;
                         QIDINetwork m_qidinetwork;
                         m_qidinetwork.unbind(msg, device.id);
                         m_qidinetwork.get_device_list(msg);
                     }
                 }
 #endif         
                 webisNetMode = isDisconnect;
                 SetPresetChanged(true);

                 UpdateLayout();
                 Refresh();
                 break;
             }
         }
         if (m_handlerl) {
             m_handlerl(event);
         }
     }
 }
 void PrinterWebView::OnEditButtonClick(wxCommandEvent &event)
 {
     for (DeviceButton *button : m_buttons) {
         if ((button->GetIsSelected())) {
             // y1 //y25
             m_exit_host.erase(into_u8(button->getIPLabel()));
             PhysicalPrinterDialog dlg(this->GetParent(), (button->getNameLabel()), m_machine, m_exit_host);
             if (dlg.ShowModal() == wxID_YES) {
                 if (m_handlerl) {
                     m_handlerl(event);
                 }
                 m_ip = dlg.get_host();
                 FormatUrl(into_u8(m_ip));

                 SetPresetChanged(true);
             }
             break;
         }
     }
 }
 // void PrinterWebView::SendRecentList(int images)
 //{
 //    boost::property_tree::wptree req;
 //    boost::property_tree::wptree data;
 //    //wxGetApp().mainframe->get_recent_projects(data, images);
 //    req.put(L"sequence_id", "");
 //    req.put(L"command", L"studio_set_mallurl");
 //    //req.put_child(L"response", data);
 //    std::wostringstream oss;
 //    pt::write_json(oss, req, false);
 //    RunScript(wxString::Format("window.postMessage(%s)", oss.str()));
 //}
 void PrinterWebView::OnScriptMessage(wxWebViewEvent &evt)
 {
     wxLogMessage("Script message received; value = %s, handler = %s", evt.GetString(), evt.GetMessageHandler());
     // std::string response = wxGetApp().handle_web_request(evt.GetString().ToUTF8().data());
     // if (response.empty())
     //    return;
     // SendRecentList(1);
     ///* remove \n in response string */
     // response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
     // if (!response.empty()) {
     //    m_response_js         = wxString::Format("window.postMessage('%s')", response);
     //    wxCommandEvent *event = new wxCommandEvent(EVT_RESPONSE_MESSAGE, this->GetId());
     //    wxQueueEvent(this, event);
     //} else {
     //    m_response_js.clear();
     //}
 }
 void PrinterWebView::UpdateLayout()
 {
     wxSize size   = devicesizer->GetSize();
     int    height = size.GetHeight();
     int    Width  = size.GetWidth();
     leftScrolledWindow->SetVirtualSize(Width, height);
     devicesizer->Layout();

     leftScrolledWindow->Layout();

     leftScrolledWindow->FitInside();
     allsizer->Layout();
     if (!m_buttons.empty()) {
         for (DeviceButton *button : m_buttons) {
             button->Layout();
             button->Refresh();
         }
     }
     if (!m_net_buttons.empty()) {
         for (DeviceButton *button : m_net_buttons) {
             button->Layout();
             button->Refresh();
         }
     }
     //y33
     if (!m_isSimpleMode) 
        staticBitmap->SetBitmap(create_scaled_bitmap_of_login(m_user_head_name, this, 60));
     else 
        staticBitmap->SetBitmap(create_scaled_bitmap_of_login(m_user_head_name, this, 40));
 }
 void PrinterWebView::OnScrollup(wxScrollWinEvent &event)
 {
     height -= 5;
     leftScrolledWindow->Scroll(0, height);
     UpdateLayout();
     event.Skip();
 }
 void PrinterWebView::OnScrolldown(wxScrollWinEvent &event)
 {
     height += 5;
     leftScrolledWindow->Scroll(0, height);
     UpdateLayout();
     event.Skip();
 }
 void PrinterWebView::OnScroll(wxScrollWinEvent &event)
 {
     UpdateLayout();
     event.Skip();
 }

//y28
 void PrinterWebView::load_disconnect_url(wxString& url)
 {
     webisNetMode = isDisconnect;
     m_web = url;
     m_ip = "";
     m_browser->LoadURL(url);
     UpdateState();
 }

 void PrinterWebView::load_url(wxString &url)
 {
     if (m_browser == nullptr || m_web == url)
         return;
     m_web = url;
     m_browser->LoadURL(url);
     //y28
     webisNetMode = isLocalWeb;
     // B55
     if (url.Lower().starts_with("http"))
         url.Remove(0, 7);
     if (url.Lower().ends_with("10088"))
         url.Remove(url.length() - 6);
     m_ip = url;
     for (DeviceButton *button : m_net_buttons) {
         button->SetIsSelected(false);
     }

     for (DeviceButton *button : m_buttons) {
        //y31
         if ((button->getIPLabel()).find(m_ip) != std::string::npos)
             button->SetIsSelected(true);
         else
             button->SetIsSelected(false);
     }
     UpdateState();
 }
 void PrinterWebView::load_net_url(std::string url, std::string ip)
 {
     if (m_browser == nullptr || m_web == url)
        return;
    // y13
    m_web = url;
    m_ip = ip;
    //y34
    m_browser->LoadURL(m_web);
    //y28
    webisNetMode = isNetWeb;
    for (DeviceButton *button : m_buttons) {
        button->SetIsSelected(false);
    }

    for (DeviceButton *button : m_net_buttons) {
        if (m_ip == (button->getIPLabel()))
            button->SetIsSelected(true);
        else
            button->SetIsSelected(false);
    }
    UpdateState();
 }
 void PrinterWebView::UpdateState()
 {
     StateColor add_btn_bg(std::pair<wxColour, int>(wxColour(57, 57, 61), StateColor::Disabled),
                            std::pair<wxColour, int>(wxColour(138, 138, 141), StateColor::Pressed),
                             std::pair<wxColour, int>(wxColour(85, 85, 90), StateColor::Hovered),
                             std::pair<wxColour, int>(wxColour(74, 74, 79), StateColor::Normal));
    // y22
     if (!m_isNetMode){
         m_select_type = "local";
         add_button->SetIcon("add_machine_list_able");
         add_button->Enable(true);
         add_button->Refresh();
         delete_button->SetIcon("delete_machine_list_disable");
         delete_button->Enable(false);
         delete_button->Refresh();
         edit_button->SetIcon("edit_machine_list_disable");
         edit_button->Enable(false);
         edit_button->Refresh();
         refresh_button->SetIcon("refresh_machine_list_able");
         refresh_button->Enable(true);
         refresh_button->Refresh();
         login_button->Refresh();
         for (DeviceButton* button : m_buttons) {
             if (button->GetIsSelected()) {
                 delete_button->SetIcon("delete_machine_list_able");
                 delete_button->Enable(true);
                 delete_button->Refresh();
                 edit_button->SetIcon("edit_machine_list_able");
                 edit_button->Enable(true);
                 edit_button->Refresh();
             }
         }
     }else{
         m_select_type = "net";
         add_button->SetIcon("add_machine_list_disable");
         add_button->Enable(false);
         add_button->Refresh();
         delete_button->SetIcon("delete_machine_list_disable");
         delete_button->Enable(false);
         delete_button->Refresh();
         edit_button->SetIcon("edit_machine_list_disable");
         edit_button->Enable(false);
         edit_button->Refresh();
         refresh_button->SetIcon("refresh_machine_list_able");
         refresh_button->Enable(true);
         refresh_button->Refresh();
         login_button->Refresh();
         for (DeviceButton* button : m_net_buttons) {
             if (button->GetIsSelected()) {
                 delete_button->SetIcon("delete_machine_list_able");
                 delete_button->Enable(true);
             }
         }
     }
 }
 void PrinterWebView::OnClose(wxCloseEvent &evt) { this->Hide(); }
 void PrinterWebView::RunScript(const wxString &javascript)
 {
     // Remember the script we run in any case, so the next time the user opens
     // the "Run Script" dialog box, it is shown there for convenient updating.

     WebView::RunScript(m_browser, javascript);
 }

 void PrinterWebView::FormatNetUrl(std::string link_url, std::string local_ip, bool isSpecialMachine)
 {
     std::string formattedHost;
     if (isSpecialMachine)
     {
         if (wxGetApp().app_config->get("dark_color_mode") == "1")
             formattedHost = link_url + "&theme=dark";
         else
             formattedHost = link_url + "&theme=light";

         std::string formattedHost1 = "http://fluidd_" + formattedHost;
         std::string formattedHost2 = "http://fluidd2_" + formattedHost;
         if (formattedHost1 == m_web || formattedHost2 == m_web)
             return;

         if (m_isfluidd_1)
         {
             formattedHost = "http://fluidd_" + formattedHost;
             m_isfluidd_1 = false;
         }
         else
         {
             formattedHost = "http://fluidd2_" + formattedHost;
             m_isfluidd_1 = true;
         }
     }
     else
     {
         formattedHost = "http://" + link_url;
     }
     load_net_url(formattedHost, local_ip);
 }

 void PrinterWebView::FormatUrl(std::string link_url) 
 {
     wxString m_link_url = from_u8(link_url);
     wxString url;
     if (!m_link_url.Lower().starts_with("http"))
         url = wxString::Format("http://%s", m_link_url);

     if (!url.Lower().ends_with("10088"))
         url = wxString::Format("%s:10088", url);

     load_url(url);
 }

 void PrinterWebView::SetToggleBar(bool is_net_mode)
 {
     toggleBar->SetValue(is_net_mode);
     m_isNetMode = is_net_mode;
     UpdateState();
 }

} // GUI
} // Slic3r
