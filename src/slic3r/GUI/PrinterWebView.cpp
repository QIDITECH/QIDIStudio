#include "PrinterWebView.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"

// cj_1
#include <wx/wx.h>
#include <wx/timer.h>
#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <wx/simplebook.h>

#include <slic3r/GUI/Widgets/WebView.hpp>

#include "PhysicalPrinterDialog.hpp"
//B45
#include <wx/regex.h>
#include <boost/regex.hpp>
#include <wx/graphics.h>
//B55
#include "../Utils/PrintHost.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

#include <iostream>
#include <chrono>

#include "nlohmann/json.hpp"
#include <curl/curl.h>

#include "Tab.hpp"

namespace pt = boost::property_tree;


namespace Slic3r {
namespace GUI {

// //B45
PrinterWebView::PrinterWebView(wxWindow* parent) : 
    wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    wxPanel* line_area;
    wxBoxSizer* buttonSizer;
    wxBoxSizer* menuPanelSizer;
    wxPanel* titlePanel;
    wxPanel* menuPanel;
    wxBoxSizer* menu_bar_sizer;
    wxPanel* t_browser_panel;
    wxPanel* t_status_panel;
    wxBoxSizer* t_browser_sizer;
    wxBoxSizer* t_status_sizer;
    // cj_1 ui
    allsizer = new wxBoxSizer(wxHORIZONTAL);
    SetSizer(allsizer); {
        leftallsizer = new wxBoxSizer(wxVERTICAL); {
			titlePanel = new wxPanel(this, wxID_ANY);
			titlePanel->SetBackgroundColour(wxColour(255, 255, 255));
            buttonSizer = new wxBoxSizer(wxVERTICAL);
            titlePanel->SetSizer(buttonSizer); {


                wxStaticBitmap* staticBitmap = new wxStaticBitmap(titlePanel, wxID_ANY, create_scaled_bitmap("QIDIStudio_p", this, 26));
                buttonSizer->Add(staticBitmap, wxSizerFlags(0).Expand().Border(wxTOP, 10));

                buttonSizer->AddSpacer(10);
                line_area = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
		        line_area->SetBackgroundColour(wxColor(214, 214, 214));
                buttonSizer->Add(line_area, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
                buttonSizer->AddSpacer(5);

				menuPanelSizer = new wxBoxSizer(wxVERTICAL);
				menuPanel = new wxPanel(titlePanel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBU_LEFT | wxTAB_TRAVERSAL | wxBU_RIGHT);
				menuPanel->SetBackgroundColour(wxColour(255, 255, 255));
                menuPanel->SetSizer(menuPanelSizer); {
                    menu_bar_sizer = new wxBoxSizer(wxHORIZONTAL);
                    menu_bar_sizer = init_menu_bar(menuPanel);
                    menuPanelSizer->Add(menu_bar_sizer, wxSizerFlags(0).Expand().Align(wxALIGN_TOP).Border(wxALL, 0));
                    menuPanelSizer->Add(0, 5);
                }
            }
			buttonSizer->Add(0, 10);
			buttonSizer->Add(menuPanel, wxSizerFlags(1).Expand());

			devicesizer = new wxBoxSizer(wxVERTICAL);
			devicesizer->SetMinSize(wxSize(220, -1));
			devicesizer->Layout();
			devicesizer->Add(0, 3);
			init_scroll_window(this);
        }
		leftallsizer->Add(titlePanel, wxSizerFlags(0).Expand());
        leftallsizer->AddSpacer(8);
		leftallsizer->Add(leftScrolledWindow, wxSizerFlags(1).Expand());

		//y74
        m_status_book = new wxSimplebook(this, wxID_ANY); {
			t_browser_panel = new wxPanel(m_status_book);
            t_browser_sizer = new wxBoxSizer(wxHORIZONTAL);
            t_browser_panel->SetSizer(t_browser_sizer); {
				m_browser = WebView::CreateWebView(t_browser_panel, "");
				if (m_browser == nullptr) {
					wxLogError("Could not init m_browser");
					return;
				}
            }
            t_browser_sizer->Add(m_browser, wxSizerFlags(1).Expand());

			t_status_panel = new wxPanel(m_status_book);
			t_status_sizer = new wxBoxSizer(wxHORIZONTAL);
            t_status_panel->SetSizer(t_status_sizer); {
			    t_status_page = new StatusPanel(t_status_panel);
            }
			t_status_sizer->Add(t_status_page, wxSizerFlags(1).Expand());

        }
		m_status_book->AddPage(t_browser_panel, "", false);
		m_status_book->AddPage(t_status_panel, "", false);
    }
    //y74
    allsizer->Add(leftallsizer, wxSizerFlags(0).Expand());
    allsizer->Add(m_status_book, wxSizerFlags(1).Expand().Border(wxALL, 0));

    devicesizer->SetMinSize(wxSize(220, -1));
    devicesizer->Layout();
    leftScrolledWindow->Layout();
    buttonSizer->Layout();
    allsizer->Layout();


    m_status_book->ChangeSelection(0);

    //y74
    InitDeviceManager();
    initEventToTaskPath();

    // B45
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterWebView::OnScriptMessage, this);

    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);

    bool m_isloginin = (wxGetApp().app_config->get("user_token") != "");
    SetLoginStatus(m_isloginin);

    // cj_1
	t_status_panel->Bind(EVT_SET_COLOR, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_TYPE, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_VENDOR, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_LOAD, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_UNLOAD, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVT_AMS_REFRESH_RFID, &PrinterWebView::onRefreshRfid, this);
    bindTaskHandle();

    QDSFilamentConfig::getInstance();

    init_select_machine();
}

wxBoxSizer* PrinterWebView::init_menu_bar(wxPanel* Panel)
{
    wxBoxSizer* buttonsizer = new wxBoxSizer(wxHORIZONTAL);

    StateColor add_btn_bg(std::pair<wxColour, int>(wxColour(153, 153, 153), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 66, 255), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(116, 168, 255), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(68, 121, 251), StateColor::Normal));

    buttonsizer->AddSpacer(16);
    
    add_button = new DeviceButton(Panel, "add_machine_list_able", wxBU_LEFT);
    add_button->SetBackgroundColor(add_btn_bg);
    add_button->SetCanFocus(false);
    add_button->SetCornerRadius(4);
    
    buttonsizer->Add(add_button, 0, wxALIGN_CENTER_VERTICAL);
    
    buttonsizer->AddSpacer(14);
    
    add_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnAddButtonClick, this);

    delete_button = new DeviceButton(Panel, "delete_machine_list_able", wxBU_LEFT);
    delete_button->SetBackgroundColor(add_btn_bg);
    delete_button->SetCanFocus(false);
    delete_button->SetCornerRadius(4);
    
    buttonsizer->Add(delete_button, 0, wxALIGN_CENTER_VERTICAL);
    
    buttonsizer->AddSpacer(14);
    
    delete_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnDeleteButtonClick, this);

    edit_button = new DeviceButton(Panel, "edit_machine_list_able", wxBU_LEFT);
    edit_button->SetBackgroundColor(add_btn_bg);
    edit_button->SetCanFocus(false);
    edit_button->SetCornerRadius(4);
    
    buttonsizer->Add(edit_button, 0, wxALIGN_CENTER_VERTICAL);
    
    buttonsizer->AddSpacer(14);
    
    edit_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnEditButtonClick, this);

    refresh_button = new DeviceButton(Panel, "refresh_machine_list_able", wxBU_LEFT);
    refresh_button->SetBackgroundColor(add_btn_bg);
    refresh_button->SetCanFocus(false);
    refresh_button->SetCornerRadius(4);
    
    buttonsizer->Add(refresh_button, 0, wxALIGN_CENTER_VERTICAL);
    
    buttonsizer->AddSpacer(16);
    
    refresh_button->Bind(wxEVT_BUTTON, &PrinterWebView::OnRefreshButtonClick, this);

    buttonsizer->Layout();
    return buttonsizer;
}

void PrinterWebView::init_scroll_window(wxPanel* Panel) {
    leftScrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    leftScrolledWindow->SetBackgroundColour(wxColour(255, 255, 255));
    leftScrolledWindow->SetSizer(devicesizer);
    leftScrolledWindow->SetScrollRate(10, 10);
    leftScrolledWindow->SetMinSize(wxSize(220, -1));
    leftScrolledWindow->FitInside();
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_TOP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_BOTTOM, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEUP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEDOWN, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEUP, &PrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &PrinterWebView::OnScroll, this);
}

 void PrinterWebView::SetPresetChanged(bool status) {
     if (status) {
        clearStatusPanelData();
        DeleteButton();
        DeleteNetButton();
        leftScrolledWindow->DestroyChildren();
        devicesizer->Clear();
        m_device_manager->stopAllConnection();
        m_device_id_to_button.clear();
        m_machine.clear();
        m_exit_host.clear();


        PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
        PhysicalPrinterCollection& ph_printers = preset_bundle.physical_printers;

        if(!ph_printers.empty()){
            wxBoxSizer* label_boxsizer = new wxBoxSizer(wxHORIZONTAL);
            wxStaticText* LocalDevicesLabel = new wxStaticText(leftScrolledWindow, wxID_ANY, "Local");
            LocalDevicesLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
            LocalDevicesLabel->SetForegroundColour(wxColour(74, 74, 74));
            label_boxsizer->AddSpacer(15);
            //.Border(wxTOP, 3)
            label_boxsizer->Add(LocalDevicesLabel, wxSizerFlags(0).CenterVertical());

            devicesizer->Add(label_boxsizer);

            devicesizer->AddSpacer(6);
        

            //y50
            std::set<std::string> qidi_printers;
            const auto enabled_vendors = wxGetApp().app_config->vendors();
            for (const auto vendor : enabled_vendors) {
                std::map<std::string, std::set<std::string>> model_map = vendor.second;
                for (auto model_name : model_map) {
                    qidi_printers.emplace(model_name.first);
                }
            }
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


                AddButton(from_u8(it->get_short_name(full_name)), host, model_id, from_u8(full_name), is_selected,
                    (host_type == htOctoPrint), apikey);
                m_machine.insert(std::make_pair((it->get_short_name(full_name)), *cfg_t));
                //y25
                m_exit_host.insert(host);
            }

            devicesizer->AddSpacer(7);
        }

        //y76
        if(m_isloginin){
            wxBoxSizer* label_boxsizer_online = new wxBoxSizer(wxHORIZONTAL);
            wxStaticText* NetDevicesLabel;
            bool is_link = wxGetApp().is_link_connect();
            if(is_link){
                NetDevicesLabel = new wxStaticText(leftScrolledWindow, wxID_ANY, "Link");
            } else {
                NetDevicesLabel = new wxStaticText(leftScrolledWindow, wxID_ANY, "QIDI Maker");
            }
            NetDevicesLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
            NetDevicesLabel->SetForegroundColour(wxColour(74, 74, 74));
            label_boxsizer_online->AddSpacer(15);
            label_boxsizer_online->Add(NetDevicesLabel, wxSizerFlags(0).CenterVertical());
            devicesizer->Add(label_boxsizer_online);
            devicesizer->AddSpacer(7);
        }
 #if QDT_RELEASE_TO_PUBLIC
        m_net_devices = wxGetApp().get_devices();
        for (const auto& device : m_net_devices) {
            AddNetButton(device);
        }
#endif
    }
    //y40
    if (webisNetMode == isNetWeb) {
        for (DeviceButton* button : m_net_buttons) {
            wxString button_ip = button->getIPLabel();
            if (button_ip.Lower().starts_with("http"))
                button_ip.Remove(0, 7);
            if (button_ip.Lower().ends_with("10088"))
                button_ip.Remove(button_ip.length() - 6);
            if (button_ip == m_ip) {
                wxEvtHandler* handler = button->GetEventHandler();
                if (handler)
                {
                    wxCommandEvent evt(wxEVT_BUTTON, button->GetId());
                    evt.SetEventObject(button);
                    handler->ProcessEvent(evt);
                }
                //button->SetIsSelected(true);
                break;
            }
        }
    }
    else if (webisNetMode == isLocalWeb) {
        for (DeviceButton* button : m_buttons) {
            if (button->getIPLabel() == m_ip) {
                

                button->SetIsSelected(true);
                for (auto it = m_device_id_to_button.begin(); it != m_device_id_to_button.end(); ++it) {
                    if (it->second == button) {
                        m_cur_deviceId = it->first;
                    }
                }
                m_device_manager->setSelected(m_cur_deviceId);

                auto select_dev = m_device_manager->getSelectedDevice();

                if(select_dev){
                    m_device_manager->reconnectDevice(m_cur_deviceId);
                    m_status_book->ChangeSelection(1);
                    this->webisNetMode = isLocalWeb;
                    UpdateState();
                    allsizer->Layout();
                    if (wxGetApp().mainframe) {
                        wxGetApp().mainframe->is_webview = false;
                    }
                    wxGetApp().app_config->set("machine_list_net", "0");
                } else {
                    wxEvtHandler* handler = button->GetEventHandler();
                    if (handler)
                    {
                        wxCommandEvent evt(wxEVT_BUTTON, button->GetId());
                        evt.SetEventObject(button);
                        handler->ProcessEvent(evt);
                    }
                }
                break;
            }
        }
    }
    else
    {
        m_status_book->ChangeSelection(0);
        wxString strlang = wxGetApp().current_language_code_safe();
        if (m_isNetMode)
        {
            //y30
            wxString url = wxString::Format("file://%s/web/qidi/link_missing_connection.html", from_u8(resources_dir()));
            if (strlang != "")
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
//y76
#if QDT_RELEASE_TO_PUBLIC
        {
            bool is_get_net_devices = false;
            wxString msg;
            bool is_link = wxGetApp().is_link_connect();
            if (is_link)
            {
                QIDINetwork m_qidinetwork;
                std::string name = m_qidinetwork.user_info(msg);
                is_get_net_devices = m_qidinetwork.get_device_list(msg);
            }
            else
            {
                is_get_net_devices = MakerHttpHandle::getInstance().get_maker_device_list();
                MakerHttpHandle::getInstance().setSSEHandle([this](const std::string& event, const std::string& data) {
                    this->onSSEMessageHandle(event, data);
                    });
            }
            if (is_get_net_devices)
            {
                this->UpdateState();
                this->SetPresetChanged(true);
            }
        }

        std::thread([this](){
            while(m_isloginin){
                bool is_get_net_devices = false;
                wxString msg;
                bool is_link = wxGetApp().is_link_connect();
                if(is_link)
                {
                    QIDINetwork m_qidinetwork;
                    std::string name = m_qidinetwork.user_info(msg);
                    is_get_net_devices = m_qidinetwork.get_device_list(msg);
                }
                else
                {
                    is_get_net_devices = MakerHttpHandle::getInstance().get_maker_device_list();
                }
                
                if (is_get_net_devices) 
                {
                    std::vector<NetDevice> currentDevices = wxGetApp().get_devices();
                    int currentDeviceCount = currentDevices.size();
                    static int s_lastDeviceCount = 0;
                    
                    if (currentDeviceCount != s_lastDeviceCount) 
                    {
                        s_lastDeviceCount = currentDeviceCount;
                        
                        GUI::wxGetApp().CallAfter([this]() {
                            this->UpdateState();
                            this->SetPresetChanged(true);
                            });
                    }
                }

                std::this_thread::sleep_for(std::chrono::seconds(10));
            }
        }).detach();
#endif
    } 
#if QDT_RELEASE_TO_PUBLIC
    else {
        std::vector<NetDevice> devices;
        wxGetApp().set_devices(devices);
        if (!wxGetApp().is_link_connect())
        {
            MakerHttpHandle::getInstance().closeSSEClient();
        }
#endif
        // y28
        if(webisNetMode == isNetWeb)
            webisNetMode = isDisconnect;
        SetPresetChanged(true);
        UpdateState();
    }
}


PrinterWebView::~PrinterWebView()
{
    m_device_manager->stopAllConnection();
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

    StateColor mac_btn_bg(std::pair<wxColour, int>(wxColour(230, 237, 255), StateColor::Pressed),
                    std::pair<wxColour, int>(wxColour(230, 247, 255), StateColor::Hovered),
                    std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    //y50
    wxString machine_icon_path = wxString(Slic3r::resources_dir() + "/" + "profiles" + "/" + "thumbnail" + "/" + Machine_Name + ".png", wxConvUTF8);

    DeviceButton *machine_button = new DeviceButton(leftScrolledWindow, fullname, machine_icon_path, wxBU_LEFT, wxSize(20, 20), device_name, ip, apikey);
    machine_button->SetBackgroundColor(mac_btn_bg);
    machine_button->SetForegroundColour(wxColor(119, 119, 119));
    //machine_button->SetBorderColor(wxColour(57, 51, 55));
    machine_button->SetCanFocus(false);
    machine_button->SetCornerRadius(0);

    std::thread([this, device_name, ip, machine_type, machine_button]() {
        std::string t_device_id = m_device_manager->addDevice(
            into_u8(device_name), 
            into_u8(ip), 
            /* dev_url */ into_u8(ip),
            into_u8(machine_type)
        );
        
        if (!t_device_id.empty()) {
            std::lock_guard<std::mutex> lock(m_ui_map_mutex);
            m_device_id_to_button[t_device_id] = machine_button;
        } else {
            std::cout << "Add device failed: " << into_u8(device_name) << std::endl;
        }
    }).detach(); 

    //y76
    machine_button->Bind(wxEVT_BUTTON, [this, ip, machine_button](wxCommandEvent &event) {
        clearStatusPanelData();
        cancelAllDevButtonSelect();
        m_device_manager->unSelected();
        machine_button->SetIsSelected(true);
        for (auto it = m_device_id_to_button.begin(); it != m_device_id_to_button.end(); ++it) {
            if (it->second == machine_button) {
                m_cur_deviceId = it->first;
            }
        }
        this->webisNetMode = isLocalWeb;
        UpdateState();
        m_status_book->ChangeSelection(0);
        allsizer->Layout();
        FormatUrl(into_u8(ip));

        wxGetApp().app_config->set("last_selected_machine", into_u8(machine_button->getIPLabel()));
        wxGetApp().app_config->set_bool("last_sel_machine_is_net", false);
        wxGetApp().app_config->set_bool("is_support_mqtt", false);
    });
    devicesizer->Add(machine_button);
    devicesizer->Layout();
    Layout();
    m_buttons.push_back(machine_button);
 }

void PrinterWebView::updateDeviceConnectType(const std::string& device_id, const std::string& device_ip){
    std::lock_guard<std::mutex> lock(m_ui_map_mutex);
    auto it = m_device_id_to_button.find(device_id);
    if (it != m_device_id_to_button.end()) {
        DeviceButton* machine_button = it->second;
        if (machine_button) {
            //y76
            machine_button->Bind(wxEVT_BUTTON, [this, device_ip, machine_button, device_id](wxCommandEvent &event) {
                clearStatusPanelData();
                cancelAllDevButtonSelect();
                machine_button->SetIsSelected(true);
                m_device_manager->setSelected(device_id);
                m_device_manager->reconnectDevice(device_id);

                for (auto it = m_device_id_to_button.begin(); it != m_device_id_to_button.end(); ++it) {
                    if (it->second == machine_button) {
                        m_cur_deviceId = it->first;
                    }
                }
                this->webisNetMode = isLocalWeb;
                UpdateState();
                m_status_book->ChangeSelection(1);
                allsizer->Layout();

                if (wxGetApp().mainframe) {
                    wxGetApp().mainframe->is_webview = false;
                }
                m_ip = device_ip;
                wxGetApp().app_config->set("machine_list_net", "0");
                wxGetApp().app_config->set("last_selected_machine", into_u8(machine_button->getIPLabel()));
                wxGetApp().app_config->set_bool("last_sel_machine_is_net", false);
                wxGetApp().app_config->set_bool("is_support_mqtt", true);
            });
        }
    }
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
 void PrinterWebView::AddNetButton(const NetDevice device)
 {
     std::set<std::string> qidi_printers;
     // y50
     const auto enabled_vendors = wxGetApp().app_config->vendors();
     for (const auto vendor : enabled_vendors) {
         std::map<std::string, std::set<std::string>> model_map = vendor.second;
         for (auto model_name : model_map) {
             qidi_printers.emplace(model_name.first);
         }
     }

     wxString    Machine_Name;
     wxString    device_name;
     std::string t_device_type;
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
                 t_device_type = machine_vendor;
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
                     t_device_type = machine_vendor;
                     break;
                 }
             }
         }
     }
     
     if (Machine_Name.empty())
     {
         Machine_Name = Machine_Name.Format("%s%s", "my_printer", "_thumbnail");
     }

    StateColor mac_btn_bg(std::pair<wxColour, int>(wxColour(230, 245, 255), StateColor::Pressed),
                    std::pair<wxColour, int>(wxColour(230, 247, 255), StateColor::Hovered),
                    std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

     //y50
     wxString machine_icon_path = wxString(Slic3r::resources_dir() + "/" + "profiles" + "/" + "thumbnail" + "/" + Machine_Name + ".png", wxConvUTF8);

     DeviceButton *machine_button = new DeviceButton(leftScrolledWindow, device.device_name, machine_icon_path, wxBU_LEFT, wxSize(20, 20),

                                                     device_name, device.local_ip);
     machine_button->SetBackgroundColor(mac_btn_bg);
     machine_button->SetForegroundColour(wxColor(119, 119, 119));
     machine_button->SetFont(Label::Body_16);
     //machine_button->SetBorderColor(wxColour(57, 51, 55));
     machine_button->SetCanFocus(false); 
     machine_button->SetCornerRadius(0);

     
     machine_button->Bind(wxEVT_BUTTON, [this, device, machine_button](wxCommandEvent& event) {
        if (machine_button == nullptr) {
            return;
        }
        // cj_1
        cancelAllDevButtonSelect();
        clearStatusPanelData();
        machine_button->SetIsSelected(true);


        if (wxGetApp().is_link_connect()) {
            m_status_book->ChangeSelection(0);
            m_device_manager->unSelected();
            allsizer->Layout();
            FormatNetUrl(device.link_url, device.local_ip, device.isSpecialMachine);
        }
        else {
            m_status_book->ChangeSelection(1);
            m_device_manager->setSelected(device.mac_address);
            m_cur_deviceId = device.mac_address;
            allsizer->Layout();
            if (wxGetApp().mainframe) {
                wxGetApp().mainframe->is_webview = false;
            }
            m_ip = device.local_ip;
            wxGetApp().app_config->set("machine_list_net", "1");
        }

        this->webisNetMode = isNetWeb;
        UpdateState();

        wxGetApp().app_config->set("last_selected_machine", into_u8(machine_button->getIPLabel()));
        wxGetApp().app_config->set_bool("last_sel_machine_is_net", true);
        });

    devicesizer->Add(machine_button);
     devicesizer->Layout();
     m_net_buttons.push_back(machine_button);
 	 auto qdsDevice = std::make_shared<QDSDevice>(device.mac_address, device.device_name, "", "", t_device_type);
     qdsDevice->m_frp_url = device.url;// +"/webcam/?action=snapshot";
     qdsDevice->m_ip = device.local_ip;
     qdsDevice->updateFilamentConfig();
     m_device_manager->addDevice(qdsDevice);
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
     HideAllDeviceButtons();
     ShowDeviceButtons(m_net_buttons);
     leftScrolledWindow->Layout();
     Refresh();
 }
 void PrinterWebView::ShowLocalPrinterButton()
 {
 
     HideAllDeviceButtons();
     ShowDeviceButtons(m_buttons);

     leftScrolledWindow->Layout();
     Refresh();
 }
 void PrinterWebView::SetButtons(std::vector<DeviceButton *> buttons) { m_buttons = buttons; }

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
    bool is_link = wxGetApp().is_link_connect();
    if(is_link){
        wxString    msg;
        QIDINetwork m_qidinetwork;
        m_qidinetwork.get_device_list(msg);
    }
    else {
       MakerHttpHandle::getInstance().get_maker_device_list();
    }
    m_net_devices = wxGetApp().get_devices();
    for (const auto &device : m_net_devices) {
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

                if (wxGetApp().is_link_connect()){
                    auto devices = wxGetApp().get_devices();
                    for (const auto &device : devices) {
                        if (device.local_ip == (button->getIPLabel())) {
                            wxString    msg;
                            QIDINetwork m_qidinetwork;
                            m_qidinetwork.unbind(msg, device.id);
                            m_qidinetwork.get_device_list(msg);
                        }
                    }
                } else {
					HttpData httpData;
					json bodyJson;
					bodyJson["serialNumber"] = m_cur_deviceId;
                    bodyJson["source"] = "QIDIStudio";
				    httpData.body = bodyJson.dump();
					httpData.env = m_env;
					httpData.target = DEVICE;
					wxEventType curEventType = event.GetEventType();
                    httpData.taskPath = "/unbind";

					bool isSucceed = false;
					std::string resultBody = MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);

					if (!isSucceed) {
						std::cout << "http error" << isSucceed << std::endl;
						
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

void PrinterWebView::onStatusPanelTask(wxCommandEvent& event)
{
    //y76
    if(webisNetMode == isLocalWeb){
        wxEventType curEventType = event.GetEventType();
        std::string megscript = m_localEventToTaskPath[curEventType];
        if (curEventType == EVTSET_EXTRUDER_TEMPERATURE ||
            curEventType == EVTSET_HEATERBED_TEMPERATURE ||
            curEventType == EVTSET_CHAMBER_TEMPERATURE ||
            curEventType == EVTSET_CASE_LIGHT ||
            curEventType == EVTSET_X_AXIS ||
            curEventType == EVTSET_Y_AXIS ||
            curEventType == EVTSET_Z_AXIS ||
            curEventType == EVTSET_INSERT_READ ||
			curEventType == EVTSET_BOOT_READ ||
            curEventType == EVTSET_AUTO_FILAMENT) {
				
            int value = event.GetInt();
            std::string placeholder = "%d";
            size_t pos = megscript.find(placeholder);
            if (pos != std::string::npos) {
                megscript.replace(pos, placeholder.length(), std::to_string(value));
            } else {
                megscript += std::to_string(value);
            }
            
        } else if (curEventType == EVTSET_RETURN_SAFEHOME) {
            ;
        }
        else if (
            curEventType == EVTSET_COOLLINGFAN_SPEED
			|| curEventType == EVTSET_CHAMBERFAN_SPEED
			|| curEventType == EVTSET_AUXILIARYFAN_SPEED
            ) {
			
            wxString megscriptWxStr(megscript);
            megscript = wxString::Format(megscriptWxStr,float(event.GetInt()) / float(100.00)).ToStdString();
        }
        else {
            megscript += std::to_string(event.GetInt());
        }
        m_device_manager->sendCommand(m_cur_deviceId, megscript);

        if(event.GetString() == "type"){
            int typeIndex = event.GetInt();
            std::vector<std::string> types{ "pause","resume","cancel" };
            if (typeIndex > 2) {
                typeIndex = 0;
            }
            std::string action_type = types[typeIndex];
            m_device_manager->sendActionCommand(m_cur_deviceId, action_type);
        }

    } 
#if QDT_RELEASE_TO_PUBLIC
    else {
        HttpData httpData;
        json bodyJson;
        bodyJson["serialNumber"] = m_cur_deviceId;
        if (event.GetString() == "value") {
            bodyJson["value"] = event.GetInt();
        }
        else if (event.GetString() == "enable") {
            bodyJson["enable"] = bool(event.GetInt());
        }
        else if (event.GetString() == "type") {// 0 :pause 1: resume 2:cancel
            int typeIndex = event.GetInt();
            std::vector<std::string> types{ "pause","resume","cancel" };
            if (typeIndex > 2) {
                typeIndex = 0;
            }
            bodyJson["type"] = types[typeIndex];
        }
        else if(!event.GetString().empty()){

        }

        if (event.GetString().empty()) {
            httpData.body = "{}";
        }
        else {
            httpData.body = bodyJson.dump();
        }


        httpData.env = m_env;
        httpData.target = PRINTERTYPE;

        wxEventType curEventType = event.GetEventType();
        if (m_eventToTaskPath.find(curEventType) != m_eventToTaskPath.end()) {
            httpData.taskPath = m_eventToTaskPath[curEventType];
        }

        bool isSucceed = false;
        std::string resultBody = MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);
        
        if (!isSucceed) {
            std::cout << "http error" << isSucceed << std::endl;
            return;
        }
    }
#endif
}

//cj_1
void PrinterWebView::onSetBoxTask(wxCommandEvent& event)
{

    //cj_1
	if (webisNetMode == isLocalWeb) {
        wxEventType curEventType = event.GetEventType();
		std::string megscript;
		if (curEventType == EVT_SET_COLOR) {
			megscript = "SAVE_VARIABLE VARIABLE=color_slot" + std::to_string(event.GetInt())
				+ " VALUE=\"" + event.GetString().ToStdString() + "\"";
		}
        if (curEventType == EVTSET_FILAMENT_VENDOR) {
			megscript = "SAVE_VARIABLE VARIABLE=vendor_slot" + std::to_string(event.GetInt())
				+ " VALUE=\"" + event.GetString().ToStdString() + "\"";
        }
		if (curEventType == EVTSET_FILAMENT_TYPE) {
			megscript = "SAVE_VARIABLE VARIABLE=filament_slot" + std::to_string(event.GetInt())
				+ " VALUE=\"" + event.GetString().ToStdString() + "\"";
		}
		if (curEventType == EVTSET_FILAMENT_LOAD) {
			megscript = "E_LOAD slot=" + std::to_string(event.GetInt());
		}
		if (curEventType == EVTSET_FILAMENT_UNLOAD) {
			megscript = "E_UNLOAD slot=" + std::to_string(event.GetInt());
		}

		m_device_manager->sendCommand(m_cur_deviceId, megscript);
		return;
	}

#if QDT_RELEASE_TO_PUBLIC
	HttpData httpData;
	json bodyJson;
	bodyJson["serialNumber"] = m_cur_deviceId;
	bodyJson["slotIndex"] = event.GetInt();
    long index = -1;
    event.GetString().ToLong(&index);
	bodyJson["idx"] = index;
    httpData.body = bodyJson.dump();
	httpData.env = m_env;
	httpData.target = PRINTERTYPE;
	wxEventType curEventType = event.GetEventType();
	if (m_boxEventToTaskPath.find(curEventType) != m_boxEventToTaskPath.end()) {
		httpData.taskPath = m_boxEventToTaskPath[curEventType];
	}
	bool isSucceed = false;
	std::string resultBody = MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);

	if (!isSucceed) {
		std::cout << "http error" << isSucceed << std::endl;
		return;
	}
#endif
}

void PrinterWebView::onRefreshRfid(wxCommandEvent& event)
{
	long canId = 0;
	event.GetString().ToLong(&canId);
	int slotIndex = event.GetInt() * 4 + canId;


	if (webisNetMode == isLocalWeb) {
		std::string megscript;
		megscript = "RFID_READ SLOT=slot" + std::to_string(slotIndex);

		m_device_manager->sendCommand(m_cur_deviceId, megscript);
		return;
	}

#if QDT_RELEASE_TO_PUBLIC
    //RFID_READ SLOT=slot3
	HttpData httpData;
	json bodyJson;
	bodyJson["serialNumber"] = m_cur_deviceId;
	bodyJson["slotIndex"] = slotIndex;
    httpData.body = bodyJson.dump();
	httpData.env = m_env;
	httpData.target = PRINTERTYPE;
    httpData.taskPath = "/set/filament/rfid";
	
	bool isSucceed = false;
	std::string resultBody = MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);
    
	if (!isSucceed) {
		std::cout << "http error" << isSucceed << std::endl;
		return;
	}

    auto device = m_device_manager->getDevice(m_cur_deviceId);
    return;
#endif


}

void PrinterWebView::OnScroll(wxScrollWinEvent& event)
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
    if (wxGetApp().mainframe) {
         wxGetApp().mainframe->is_webview = true;
         wxGetApp().mainframe->is_net_url = false;
         wxGetApp().mainframe->printer_view_ip = "";
         wxGetApp().mainframe->printer_view_url = m_web;
     }
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
        wxString button_ip = button->getIPLabel();
         if (button_ip.Lower().starts_with("http"))
             button_ip.Remove(0, 7);
        if (button_ip.Lower().ends_with("10088"))
            button_ip.Remove(button_ip.length() - 6);
         if (button_ip == m_ip)
             button->SetIsSelected(true);
         else
             button->SetIsSelected(false);
     }
     if (wxGetApp().mainframe) {
         wxGetApp().mainframe->is_webview = true;
         wxGetApp().mainframe->is_net_url = false;
         wxGetApp().mainframe->printer_view_ip = m_ip;
         wxGetApp().mainframe->printer_view_url = m_web;
     }
     UpdateState();
 }
 void PrinterWebView::load_net_url(wxString& url, wxString& ip)
 {
     if (m_browser == nullptr || m_web == url)
        return;
    // y13
    m_web = url;
    m_ip = ip;
    m_browser->LoadURL(m_web);
    //y28
    webisNetMode = isNetWeb;
//     for (DeviceButton *button : m_buttons) {
//         button->SetIsSelected(false);
//     }

    for (DeviceButton *button : m_net_buttons) {
        wxString button_ip = button->getIPLabel();
         if (button_ip.Lower().starts_with("http"))
             button_ip.Remove(0, 7);
        if (button_ip.Lower().ends_with("10088"))
            button_ip.Remove(button_ip.length() - 6);
//         if (m_ip == button_ip)
//             button->SetIsSelected(true);
//         else
//             button->SetIsSelected(false);
    }
    if (wxGetApp().mainframe) {
        wxGetApp().mainframe->is_webview = true;
        wxGetApp().mainframe->is_net_url = true;
        wxGetApp().mainframe->printer_view_ip = m_ip;
        wxGetApp().mainframe->printer_view_url = m_web;
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
     wxString host = from_u8(formattedHost);
     wxString ip = from_u8(local_ip);
     load_net_url(host, ip);
 }

 void PrinterWebView::FormatUrl(std::string link_url) 
 {
    //y52
    wxString m_link_url = from_u8(link_url);
    wxString url;
    
    if(m_link_url.find(":") == wxString::npos)
        url = wxString::Format("%s:10088", m_link_url);
    else
        url = m_link_url;

    if (!url.Lower().starts_with("http"))
        url = wxString::Format("http://%s", url);

    load_url(url);
 }

 std::string extractBetweenMarkers(const std::string& path) {

	 size_t startPos = path.find("/gcodes");
	 if (startPos == std::string::npos) {
		 return "";
	 }

	 startPos += 7;

	 size_t endPos = path.find(".3mf", startPos);
	 if (endPos == std::string::npos) {
		 return "";
	 }

	 return path.substr(startPos, endPos - startPos);
 }

 void PrinterWebView::onSSEMessageHandle(const std::string& event, const std::string& data)
 {
	 try
	 {
		 json msgJson = json::parse(data);

		 if (!msgJson.contains("data") && !msgJson["data"].is_object()
			 && !msgJson.contains("serialNumber") && !msgJson["serialNumber"].is_string()
			 )
		 {
		 
			 return;
		 }
		 std::string device_id = msgJson["serialNumber"];
		 auto device = m_device_manager->getDevice(device_id);
		 if (device == nullptr) {
			 return;
		 }
         string dataStr = msgJson["data"].get<std::string>();
		 json dataJson = json::parse(msgJson["data"].get<std::string>());
		 json status;

		 if (dataJson.contains("result") && dataJson["result"].contains("status")) {
			 status = dataJson["result"]["status"];
		 }
		 else if (dataJson.contains("status")) {
			 status = dataJson["status"];
		 }

		 {
             std::string oldPrintFileName = device->m_print_filename;
			 device->updateByJsonData(status);
			 device->last_update = std::chrono::steady_clock::now();
             //cj_1 
             if (oldPrintFileName != device->m_print_filename) {
				 
				std::string url = device->m_frp_url + "/api/qidiclient/files/list";
				Slic3r::Http httpPost = Slic3r::Http::get(url);
				std::string resultBody;
				bool su = false;
				httpPost.timeout_max(5)
					.header("accept", "application/json")
					.header("Content-Type", "application/json")
					.on_complete(
						[&resultBody, &su](std::string body, unsigned status) {
							
							resultBody = body;
							su = true;
						}
					)
					.on_error(
						[this](std::string body, std::string error, unsigned status) {

						}
						).perform_sync();

				try {
					json bodyJson = json::parse(resultBody);
					if (!bodyJson.is_object()) {
						return ;
					}
					if (!bodyJson.contains("result")) {
						return ;
					}
					json resultJson = bodyJson["result"];
					if (resultJson.is_array()) {
						for (json printfFileData : resultJson) {
							if (!printfFileData.is_object()) {
								continue;
							}

							//filepath
							if (printfFileData.contains("filepath") && printfFileData["filepath"].is_string()) {
								if (printfFileData["filepath"].get<std::string>().find("/.cache/") != std::string::npos) {
									continue;
								}
                            }
                            else {
                                continue;
                            }

							if (printfFileData.contains("filename") && printfFileData["filename"].is_string()) {
                                std::string jsonFileName = from_u8(printfFileData["filename"].get<std::string>()).ToStdString();
                                
								if (jsonFileName != device->m_print_filename) {
									continue;
								}
											 
                            }
                            else {
                                continue;
                            }

							if (printfFileData.contains("show_filament_weight") && printfFileData["show_filament_weight"].is_string())
							{
                                device->m_filament_weight = printfFileData["show_filament_weight"].get<std::string>();
							}

							if (printfFileData.contains("show_print_time") && printfFileData["show_print_time"].is_string())
							{
                                device->m_print_total_time = printfFileData["show_print_time"].get<std::string>();
							}

                            std::string plateIndex = "1";
							if (printfFileData.contains("plates") && printfFileData["plates"].is_array())
							{
                                if (printfFileData["plates"].size() > 0 && printfFileData["plates"][0].contains("plate_index")
                                    && printfFileData["plates"][0]["plate_index"].is_string())
                                {
                                    plateIndex = printfFileData["plates"][0]["plate_index"].get<std::string>();
                                }
							}

                            std::string pngUrl = "";
                            pngUrl = device->m_frp_url + "/server/files/gcodes/.thumbs";
                            pngUrl = pngUrl + from_u8( extractBetweenMarkers(printfFileData["filepath"].get<std::string>())).ToStdString();
                            pngUrl = "http://" +  pngUrl + "/plate_"+ plateIndex +".png";
                            device->m_print_png_url = pngUrl;

						}
					}
				}
				catch (...) {

				}


				 

             }
		 }

		 if (m_cur_deviceId == device_id && device->is_update) {

			 GUI::wxGetApp().CallAfter([this, device_id]() {
				 this->updateDeviceParameter(device_id);
				 });

			 device->is_update = false;

		 }

	 }
	 catch (...)
	 {

	 }
 }

//y74
void PrinterWebView::InitDeviceManager(){
    m_device_manager = wxGetApp().qdsdevmanager;

    m_device_manager->setConnectionEventCallback([this](const std::string& device_id, std::string new_status){
        GUI::wxGetApp().CallAfter([this, device_id, new_status](){
            this->updateDeviceButton(device_id, new_status);
        });
    });

    m_device_manager->setParameterUpdateCallback([this](const std::string& device_id) {
        GUI::wxGetApp().CallAfter([this, device_id]() {
            this->updateDeviceParameter(device_id);
        });
    });

    m_device_manager->setDeviceConnectTypeUpdateCallback([this](const std::string& device_id, const std::string& device_ip){
        GUI::wxGetApp().CallAfter([this, device_id, device_ip](){
            this->updateDeviceConnectType(device_id, device_ip);
        });
    });

    m_device_manager->setDeleteDeviceIDCallback([this](const std::string& device_id){
        std::lock_guard<std::mutex> lock(m_ui_map_mutex);
        m_device_id_to_button.erase(device_id);
    });

    // m_device_manager->setFileInfoUpdateCallback([this](const std::string& device_id)){
    //     GUI::wxGetApp().CallAfter([this, device_id]() {
    //         this->updateDeviceParameter(device_id);
    //     });
    // }
}


void PrinterWebView::initEventToTaskPath()
{
	m_eventToTaskPath = {
		{EVTSET_EXTRUESION, "/set/extrusion"},
		{EVTSET_BACK, "/set/back"},
		{EVTSET_COOLER_SWITCH, "/set/cooler/switch"},
		{EVTSET_COOLER_ENABLE, "/set/cooler/enable"},
		{EVTSET_LEVELING_ENABLE, "/set/leveling/enable"},
		{EVTSET_AMS_ENABLE, "/set/ams/enable"},
		{EVTSET_COOLLINGFAN_SPEED, "/set/coolingFan/speed"},
		{EVTSET_CHAMBERFAN_SPEED, "/set/chamberFan/speed"},
		{EVTSET_AUXILIARYFAN_SPEED, "/set/auxiliaryFan/speed"},
		{EVTSET_CASE_LIGHT, "/set/case/light"},
		{EVTSET_BEEPER_SWITHC, "/set/beeper/switch"},
		{EVTSET_EXTRUDER_TEMPERATURE, "/set/extruder/temperature"},
		{EVTSET_HEATERBED_TEMPERATURE, "/set/heaterBed/temperature"},
		{EVTSET_CHAMBER_TEMPERATURE, "/set/chamber/temperature"},
		{EVTSET_RETURN_SAFEHOME, "/set/return/safeHome"},
		{EVTSET_X_AXIS, "/set/x/axis"},
		{EVTSET_Y_AXIS, "/set/y/axis"},
		{EVTSET_Z_AXIS, "/set/z/axis"},
		{EVTSET_PRINT_CONTROL, "/set/print/control"},
		{EVTSET_INSERT_READ, "/ams/insert/filament/read/enable"},
		{EVTSET_BOOT_READ, "/ams/boot/read/enable"},
		{EVTSET_AUTO_FILAMENT, "/ams/auto/filament/enable"}
	};

    //cj_1
    m_boxEventToTaskPath = { 
		{EVT_SET_COLOR,"/set/filament/color"},
		{EVTSET_FILAMENT_TYPE, "/set/filament/type"},
		{EVTSET_FILAMENT_VENDOR, "/set/filament/vendor"},
		{EVTSET_FILAMENT_LOAD, "/set/filament/load"},
		{EVTSET_FILAMENT_UNLOAD, "/set/filament/unload"}
    };

    //y76
    m_localEventToTaskPath = {
        {EVTSET_EXTRUDER_TEMPERATURE, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=%d"},
        {EVTSET_HEATERBED_TEMPERATURE, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=%d"},
        {EVTSET_CHAMBER_TEMPERATURE, "SET_HEATER_TEMPERATURE HEATER=chamber TARGET=%d"},
        {EVTSET_CASE_LIGHT, "SET_PIN PIN=caselight VALUE=%d"},
        {EVTSET_X_AXIS, "G91\nG1 X%d F7800\nG90"},
        {EVTSET_Y_AXIS, "G91\nG1 Y%d F7800\nG90"},
        {EVTSET_Z_AXIS, "G91\nG1 Z%d F600\nG90"},
        {EVTSET_RETURN_SAFEHOME, "G28" },
	    {EVTSET_INSERT_READ, "SAVE_VARIABLE VARIABLE=auto_read_rfid VALUE=\"%d\""},
		{EVTSET_BOOT_READ, "SAVE_VARIABLE VARIABLE=auto_init_detect VALUE=\"%d\""},
		{EVTSET_AUTO_FILAMENT, "SAVE_VARIABLE VARIABLE=auto_reload_detect VALUE=\"%d\""},
		{EVTSET_COOLLINGFAN_SPEED, "SET_FAN_SPEED FAN=cooling_fan SPEED=%.2f"},
		{EVTSET_AUXILIARYFAN_SPEED, "SET_FAN_SPEED FAN=auxiliary_cooling_fan SPEED=%.2f"},
		{EVTSET_CHAMBERFAN_SPEED, "SET_FAN_SPEED FAN=chamber_circulation_fan SPEED=%.2f"}

    };

#if QDT_RELEASE_TO_PUBLIC
    std::string region = wxGetApp().app_config->get("region");
    if (region == "China") {
        m_env = PRODUCTIONENV;
    }
    else {
        m_env = FOREIGNENV;
    }
#endif
}

void PrinterWebView::bindTaskHandle()
{
    if (t_status_page == nullptr) {
        return;
    }

	for (auto& pair : m_eventToTaskPath) {
        const wxEventTypeTag< wxCommandEvent >eventType = pair.first;
        t_status_page->Bind(eventType, &PrinterWebView::onStatusPanelTask, this);

	}
}

// cj_1
void PrinterWebView::HideDeviceButtons(std::vector<DeviceButton*>& buttons)
{
    ShowDeviceButtons(buttons, false);
}

void PrinterWebView::HideAllDeviceButtons()
{
    HideDeviceButtons(m_buttons);
    HideDeviceButtons(m_net_buttons);
}

void PrinterWebView::cancelAllDevButtonSelect()
{
    for (DeviceButton* button : m_buttons) {
        button->SetIsSelected(false);
    }

	for (DeviceButton* button : m_net_buttons) {
		button->SetIsSelected(false);
	}
    
}

// cj_1
void PrinterWebView::clearStatusPanelData()
{
    t_status_page->update_temp_data("_", "_", "_");
	t_status_page->update_temp_target("_", "_", "_");
    t_status_page->pause_camera();
	t_status_page->update_camera_url("");
    t_status_page->set_default();
    t_status_page->update_thumbnail("");
}

//y76
void PrinterWebView::pauseCamera(){
    t_status_page->pause_camera();
}

void PrinterWebView::ShowDeviceButtons(std::vector<DeviceButton*>& buttons, bool isShow /*= true*/)
{
	if (buttons.empty()) {
		BOOST_LOG_TRIVIAL(info) << " empty";
	}
	else {
		for (DeviceButton* button : buttons) {
            if (isShow) {
                button->Show();
            }
            else {
                button->Hide();
            }
		}
	}
}

//y74
void PrinterWebView::updateDeviceButton(const std::string& device_id, std::string new_status){
    DeviceButton* t_button = nullptr;
    std::string t_printer_progress;
    {
        std::lock_guard<std::mutex> lock(m_ui_map_mutex);
        auto it = m_device_id_to_button.find(device_id);
        if(it != m_device_id_to_button.end()){
            t_button = it->second;
            if (new_status == "printing") {
                t_printer_progress = " (" + m_device_manager->getDevice(device_id)->m_print_progress + "%) ";
            }
        }
    }

    if(t_button != nullptr){
        t_button->SetStateText(new_status);
        if(!t_printer_progress.empty())
            t_button->SetProgressText(t_printer_progress);
    }

}


//cj_1
std::string extractEndNumbers(const std::string& str) {
	size_t endPos = str.find_last_not_of("0123456789");

	if (endPos == std::string::npos) {
		return str;
	}
	else if (endPos == str.length() - 1) {
		return "";
	}
	else {
		return str.substr(endPos + 1);
	}
}
void PrinterWebView::updateDeviceParameter(const std::string& device_id) {
    DeviceButton* t_button = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_ui_map_mutex);
        auto it = m_device_id_to_button.find(device_id);
        if (it != m_device_id_to_button.end()) {
            t_button = it->second;
        }
        if (m_cur_deviceId == device_id) {
            t_status_page->update_temp_data(
                m_device_manager->getDeviceTempNozzle(device_id),
                m_device_manager->getDeviceTempBed(device_id),
                m_device_manager->getDeviceTempChamber(device_id)
            );

			auto device = m_device_manager->getDevice(device_id);
            if (device == nullptr) {
                return;
            }
            t_status_page->update_temp_target(device->m_target_extruder, device->m_target_bed, device->m_target_chamber);
            
            t_status_page->update_temp_ctrl(device);

			int duration = 1;
			if (!device->m_print_duration.empty()) {
				duration = std::stoi(device->m_print_duration);
			}
			float progress = device->m_print_progress_float;
			int totalTime = 0;
			int remainTime = 0;
			if (progress > 0 && progress < 1 && duration>0) {
				totalTime = (double)duration / device->m_print_progress_float;
				remainTime = totalTime - duration;
			}
			string layer = _L("Layer: N/A").ToStdString();
			if (device->m_print_total_layer != 0) {
				layer = "Layer: " + std::to_string(device->m_print_cur_layer) + "/" + std::to_string(device->m_print_total_layer);
			}

			std::string totalTimeStr = device->m_print_total_time;
            std::string weight = device->m_filament_weight;
			t_status_page->update_progress(device->m_print_filename, layer, totalTimeStr, weight, remainTime, progress);
			t_status_page->update_print_status(device->m_status);
            t_status_page->update_camera_url(device->m_frp_url + "/webcam/?action=snapshot");
            t_status_page->update_thumbnail(device->m_print_png_url);
            t_status_page->update_light_status(device->m_case_light);

			/*FAN_COOLING_0_AIRDOOR     = 1,
	        FAN_REMOTE_COOLING_0_IDX  = 2,
	        FAN_CHAMBER_0_IDX         = 3,*/

            t_status_page->update_fan_speed(AIR_FUN::FAN_COOLING_0_AIRDOOR, device->m_cooling_fan_speed * 10.0);
			t_status_page->update_fan_speed(AIR_FUN::FAN_REMOTE_COOLING_0_IDX, device->m_auxiliary_fan_speed * 10.0);
			t_status_page->update_fan_speed(AIR_FUN::FAN_CHAMBER_0_IDX, device->m_chamber_fan_speed * 10.0);

            if(device->box_is_update){
				vector< Slic3r::GUI::Caninfo> cans(17);
                for (int i = 0; i < 17; ++i) {
                    cans[i].can_id = std::to_string(i);
                    if (device->m_boxData[i].hasMaterial) {
                        cans[i].material_colour = wxColour(device->m_boxData[i].colorHexCode);
                        cans[i].material_name = device->m_boxData[i].name;
                        cans[i].material_state = AMSCanType::AMS_CAN_TYPE_VIRTUAL;
                        cans[i].ctype = device->m_boxData[i].vendor == "Generic" ? 0 : 1;
                    }
                    else {
                        cans[i].material_colour = *wxWHITE;
                        cans[i].material_state = AMSCanType::AMS_CAN_TYPE_EMPTY;
                    }
				}

				Slic3r::GUI::AMSinfo amsExt; {
					amsExt.ams_id = "11";
					amsExt.nozzle_id = 0;
					amsExt.ams_type = AMSModel::EXT_AMS;
					amsExt.current_step = AMSPassRoadSTEP::AMS_ROAD_STEP_NONE;
					amsExt.cans = std::vector<Slic3r::GUI::Caninfo>{ cans[16] };
				}
				std::vector<AMSinfo> ext_info;
				ext_info = std::vector<AMSinfo>{ amsExt };


				std::vector<AMSinfo> boxS;
				for (int i = 0; i < device->m_box_count; ++i) {
					AMSinfo ams;
					ams.ams_id = std::to_string(i);
					ams.nozzle_id = 0;
                    ams.humidity_raw = device->m_boxHumidity[i];
                    ams.current_temperature = device->m_boxTemperature[i];
					ams.ams_type = AMSModel::N3F_AMS;
					ams.current_step = AMSPassRoadSTEP::AMS_ROAD_STEP_NONE;
					for (int j = i * 4; j < (i + 1) * 4; ++j) {
					    ams.cans.push_back(cans[j]);
					}
					boxS.push_back(ams);
				}

                t_status_page->update_boxs(boxS, ext_info);
                if(webisNetMode == isLocalWeb)
                    t_status_page->set_filament_config(device->m_general_filamentConfig);
                else
                    t_status_page->set_filament_config(device->m_filamentConfig);

                
                std::string slotNumSyncStr = extractEndNumbers(device->m_cur_slot);
                if (slotNumSyncStr != "") {
                    t_status_page->update_cur_slot(std::stoi(slotNumSyncStr));
                }
                t_status_page->update_AMSSettingData(device->m_auto_read_rfid, device->m_init_detect, device->m_auto_reload_detect);
                
                //cj_1
                t_status_page->update_homed_axes(device->m_home_axes);
                
                PresetBundle *preset_bundle = wxGetApp().preset_bundle;
                if(preset_bundle){
                    std::string cur_preset_name = wxGetApp().get_tab(Preset::TYPE_PRINTER)->get_presets()->get_edited_preset().name;
                    if(device->box_filament_is_update && cur_preset_name.find(device->m_type) != std::string::npos){
                        wxGetApp().qdsdevmanager->upBoxInfoToBoxMsg(device);
                        device->box_filament_is_update = false;
                    }
                }
                device->box_is_update = false;
            }
        }
    }
}

void PrinterWebView::init_select_machine(){
    std::string last_select_machine = wxGetApp().app_config->get("last_selected_machine");
    if(last_select_machine.empty())
        return;
    
    bool is_net = wxGetApp().app_config->get_bool("last_sel_machine_is_net");

    DeviceButton* selected_button {nullptr};
    if(is_net){
        for (DeviceButton* button : m_net_buttons){
            wxString button_name = button->getIPLabel(); 
            if(into_u8(button_name) == last_select_machine){
                selected_button = button;
                break;
            }
        }
    } else {
        for (DeviceButton *button : m_buttons){
            wxString button_name = button->getIPLabel(); 
            if(into_u8(button_name) == last_select_machine){
                selected_button = button;
                break;
            }
        }
    }

    if (selected_button) {
        if (is_net) {
            wxEvtHandler* handler = selected_button->GetEventHandler();
            if (handler)
            {
                wxCommandEvent evt(wxEVT_BUTTON, selected_button->GetId());
                evt.SetEventObject(selected_button);
                handler->ProcessEvent(evt);
            }
        }
        else {
            bool is_support_mqtt = wxGetApp().app_config->get_bool("is_support_mqtt");
            if (is_support_mqtt) {
                clearStatusPanelData();
                cancelAllDevButtonSelect();
                selected_button->SetIsSelected(true);
                for (auto it = m_device_id_to_button.begin(); it != m_device_id_to_button.end(); ++it) {
                    if (it->second == selected_button) {
                        m_cur_deviceId = it->first;
                    }
                }
                m_device_manager->setSelected(m_cur_deviceId);
                m_device_manager->reconnectDevice(m_cur_deviceId);
                this->webisNetMode = isLocalWeb;
                UpdateState();
                m_status_book->ChangeSelection(1);
                allsizer->Layout();
                if (wxGetApp().mainframe) {
                    wxGetApp().mainframe->is_webview = false;
                }
                m_ip = selected_button->getIPLabel();
                wxGetApp().app_config->set("machine_list_net", "0");
            }
            else {
                clearStatusPanelData();
                cancelAllDevButtonSelect();
                m_device_manager->unSelected();
                selected_button->SetIsSelected(true);
                for (auto it = m_device_id_to_button.begin(); it != m_device_id_to_button.end(); ++it) {
                    if (it->second == selected_button) {
                        m_cur_deviceId = it->first;
                    }
                }
                this->webisNetMode = isLocalWeb;
                UpdateState();
                m_status_book->ChangeSelection(0);
                allsizer->Layout();
                FormatUrl(into_u8(last_select_machine));
            }
        }

    }

}

} // GUI
} // Slic3r
