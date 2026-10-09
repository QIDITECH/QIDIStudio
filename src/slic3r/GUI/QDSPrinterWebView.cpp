#include "QDSPrinterWebView.hpp"

#include <mutex>

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r_version.h"

// cj_1
#include <wx/wx.h>
#include <wx/timer.h>
#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <wx/simplebook.h>
#include <wx/animate.h>

#include <slic3r/GUI/Widgets/WebView.hpp>

#include "PhysicalPrinterDialog.hpp"
#include "DeviceErrorDialog.hpp"
//B45
#include <wx/regex.h>
#include <boost/regex.hpp>
#include <boost/filesystem.hpp>
#include <wx/graphics.h>
//B55
#include "../Utils/PrintHost.hpp"

#include <string>
#include <vector>
#include <algorithm>
#include <set>
#include <map>
#include <cstdlib>
#include <unordered_map>
#include <iostream>

#include <iostream>
#include <chrono>
#include <nlohmann/json.hpp>

#include "nlohmann/json.hpp"
#include <curl/curl.h>

#include "Tab.hpp"
//cj_2
#include "StatusPanel.hpp"
#include "AMSMaterialsSetting.hpp" // FilamentInfoPayload / EVTSET_FILAMENT_INFO
#include "Widgets/TimelapseUiEvents.hpp"
#include "Widgets/TimelapseFileList.hpp"
#include <wx/filename.h>
//cj_2
#include "QDSDeviceManager.hpp"
//cj_2
#include "Widgets/ModelFileListView.hpp"
#include "libslic3r/Utils.hpp"
#include "Widgets/TimelapseFileList.hpp"
#include "DownloadManager.hpp"
#include <wx/url.h>
#include <wx/ffile.h>
#include <wx/file.h>
#include <wx/filename.h>

#include "GUI.hpp"
#include "ReleaseNote.hpp"



//cj_2
#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#include "../QIDI/P2PManager.hpp"
#endif

#include "../QIDI/QIDIDeviceApi.hpp"
#include "../QIDI/QIDIFileManager.hpp"
#include "BaseTransparentDPIFrame.hpp"
namespace pt = boost::property_tree;

namespace {
//cj_4 Legacy cloud devices: normalize link_url into a Moonraker request base.
std::string trim_ws(std::string s)
{
    while (!s.empty() && (static_cast<unsigned char>(s.back()) <= ' '))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && static_cast<unsigned char>(s[i]) <= ' ')
        ++i;
    return s.substr(i);
}
//cj_4
// Relative path under download_dir (may contain subdirs); keep rules in sync with DeviceModelList local check.
bool local_download_target_exists(const std::string& download_dir, const wxString& file_base_name)
{
    if (download_dir.empty() || file_base_name.empty())
        return false;
    wxString rel(file_base_name);
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
    try {
        boost::filesystem::path root = boost::filesystem::absolute(boost::filesystem::path(download_dir));
        boost::filesystem::path combined = root / std::string(rel.ToUTF8().data());
        combined = combined.lexically_normal();
        root = root.lexically_normal();
        std::string rs = root.generic_string();
        std::string cs = combined.generic_string();
        if (cs.size() < rs.size())
            return false;
#ifdef _WIN32
        std::string rsl = rs, csl = cs;
        for (char& c : rsl)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        for (char& c : csl)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (csl.compare(0, rsl.size(), rsl) != 0)
            return false;
        if (csl.size() > rsl.size()) {
            const char b = csl[rsl.size()];
            if (b != '/' && b != '\\')
                return false;
        }
#else
        if (cs.compare(0, rs.size(), rs) != 0)
            return false;
        if (cs.size() > rs.size() && cs[rs.size()] != '/')
            return false;
#endif
        return boost::filesystem::is_regular_file(combined);
    } catch (const std::exception&) {
        return false;
    }
}

//cj_4
// Remove file under Preferences download_path; path rules must match local_download_target_exists.
bool remove_local_download_for_storage_path(const wxString& storage_path)
{
    std::string download_dir = wxGetApp().app_config->get("download_path");
    if (!local_download_target_exists(download_dir, storage_path))
        return false;
    wxString rel(storage_path);
    rel.Replace("\\", "/");
    while (!rel.empty() && rel[0] == '/')
        rel = rel.Mid(1);
    if (rel.empty())
        return false;
    try {
        boost::filesystem::path root = boost::filesystem::absolute(boost::filesystem::path(download_dir));
        boost::filesystem::path combined = root / std::string(rel.ToUTF8().data());
        combined = combined.lexically_normal();
        root = root.lexically_normal();
        std::string rs = root.generic_string();
        std::string cs = combined.generic_string();
        if (cs.size() < rs.size())
            return false;
#ifdef _WIN32
        std::string rsl = rs, csl = cs;
        for (char& c : rsl)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        for (char& c : csl)
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (csl.compare(0, rsl.size(), rsl) != 0)
            return false;
        if (csl.size() > rsl.size()) {
            const char b = csl[rsl.size()];
            if (b != '/' && b != '\\')
                return false;
        }
#else
        if (cs.compare(0, rs.size(), rs) != 0)
            return false;
        if (cs.size() > rs.size() && cs[rs.size()] != '/')
            return false;
#endif
        if (!boost::filesystem::is_regular_file(combined))
            return false;
        return boost::filesystem::remove(combined);
    } catch (const std::exception&) {
        return false;
    }
}

//cj_4 Model/timelapse download notices: SecondaryCheckDialog (app UI), not wxMessageBox.
static void show_printer_webview_download_notice(wxWindow* parent, const wxString& title, const wxString& message)
{
	wxWindow* dlg_parent = parent;
	if (!dlg_parent)
		dlg_parent = wxGetApp().mainframe;
	auto* dlg = new SecondaryCheckDialog(dlg_parent, wxID_ANY, title, SecondaryCheckDialog::ButtonStyle::ONLY_CONFIRM);
	dlg->m_button_ok->SetLabel(_L("OK"));
	dlg->update_text(message);
	dlg->set_message_area_width(600);
	dlg->Bind(EVT_SECONDARY_CHECK_CONFIRM, [dlg](wxCommandEvent&) { dlg->Destroy(); });
	dlg->on_show();
	dlg->Raise();
}

//cj_5
// Parse display weight text to grams for model file ordering.
double parse_display_weight_grams(const std::string& text)
{
    std::string s = trim_ws(text);
    char* end = nullptr;
    const double value = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) {
        return 0.0;
    }

    std::string unit = trim_ws(end != nullptr ? std::string(end) : std::string());
    for (char& c : unit) {
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }

    if (unit.find("kg") != std::string::npos) {
        return value * 1000.0;
    }
    if (unit.find("mg") != std::string::npos) {
        return value / 1000.0;
    }
    return value;
}
} // namespace

namespace Slic3r {
    namespace GUI {

wxDEFINE_EVENT(EVT_PRINTER_TASK_RESULT, wxCommandEvent);

MonitorConnectionPhase QDSPrinterWebView::GetConnectionPhase() const
{
    switch (webisNetMode) {
    case isLocalWeb:
        return MonitorConnectionPhase::LocalPrinter;
    case isNetWeb:
        return MonitorConnectionPhase::CloudPrinter;
    case isDisconnect:
    default:
        return MonitorConnectionPhase::Disconnected;
    }
}

void QDSPrinterWebView::SetConnectionPhase(MonitorConnectionPhase phase)
{
    switch (phase) {
    case MonitorConnectionPhase::LocalPrinter:
        webisNetMode = isLocalWeb;
        break;
    case MonitorConnectionPhase::CloudPrinter:
        webisNetMode = isNetWeb;
        break;
    case MonitorConnectionPhase::Disconnected:
    default:
        webisNetMode = isDisconnect;
        break;
    }
}

//cj_5
wxString QDSPrinterWebView::BuildDisconnectUrl() const
{
    wxString strlang = wxGetApp().current_language_code_safe();
    wxString url;
    url = wxString::Format("file://%s/web/qidi/missing_connection.html", from_u8(resources_dir()));
    if (strlang != "") {
        url = wxString::Format("file://%s/web/qidi/missing_connection.html?lang=%s", from_u8(resources_dir()), strlang);
    }
    return url;
}

//cj_5
wxString QDSPrinterWebView::BuildLocalUrl(const std::string& link_url) const
{
    wxString m_link_url = from_u8(link_url);
    wxString url;

    url = m_link_url;

    if (!url.Lower().starts_with("http")) {
        url = wxString::Format("http://%s", url);
    }

    return url;
}

//cj_5
void QDSPrinterWebView::LoadDisconnectPageOnly()
{
    m_web = BuildDisconnectUrl();
    m_ip = "";
    if (m_browser) {
        m_browser->LoadURL(m_web);
    }

    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->is_webview = true;
        wxGetApp().mainframe->is_net_url = false;
    }
}

//cj_5
bool QDSPrinterWebView::LoadLocalUrlOnly(wxString& url)
{
    if (m_browser == nullptr) {
        return false;
    }

    m_web = url;
    m_browser->LoadURL(url);

    if (url.Lower().starts_with("http")) {
        url.Remove(0, 7);
    }
    //y84
    int pos = url.Find(':');
    if (pos != wxNOT_FOUND) {
        url = url.SubString(0, pos - 1);
    }
    m_ip = url;
    for (DeviceButton* button : m_net_buttons) {
        button->SetIsSelected(false);
    }

    for (DeviceButton* button : m_buttons) {
        wxString button_ip = button->getIPLabel();
        if (button_ip.Lower().starts_with("http")) {
            button_ip.Remove(0, 7);
        }
        //y84
        int pos = button_ip.Find(':');
        if (pos != wxNOT_FOUND) {
            button_ip = button_ip.SubString(0, pos - 1);
        }
        if (button_ip == m_ip) {
            button->SetIsSelected(true);
        }
        else {
            button->SetIsSelected(false);
        }
    }

    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->is_webview = true;
        wxGetApp().mainframe->is_net_url = false;
        wxGetApp().mainframe->printer_view_ip = m_ip;
        wxGetApp().mainframe->printer_view_url = m_web;
    }

    return true;
}

//cj_5
bool QDSPrinterWebView::LoadNetUrlOnly(wxString& url, wxString& ip)
{
    if (m_browser == nullptr) {
        return false;
    }

    m_web = url;
    m_ip = ip;
    m_browser->LoadURL(m_web);

    for (DeviceButton* button : m_buttons) {
        button->SetIsSelected(false);
    }

    for (DeviceButton* button : m_net_buttons) {
        wxString button_ip = button->getIPLabel();
        if (button_ip.Lower().starts_with("http")) {
            button_ip.Remove(0, 7);
        }
        //y84
        int pos = button_ip.Find(':');
        if (pos != wxNOT_FOUND) {
            button_ip = button_ip.SubString(0, pos - 1);
        }
        if (m_ip == button_ip) {
            button->SetIsSelected(true);
        }
        else {
            button->SetIsSelected(false);
        }
    }

    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->is_webview = true;
        wxGetApp().mainframe->is_net_url = true;
        wxGetApp().mainframe->printer_view_ip = m_ip;
        wxGetApp().mainframe->printer_view_url = m_web;
    }

    return true;
}

//cj_5
void QDSPrinterWebView::TransitionToDisconnected(const DisconnectTransitionOptions& options)
{
    if (options.clear_button_selection) {
        cancelAllDevButtonSelect();
    }
    if (options.clear_status_panel) {
        clearStatusPanelData();
    }
    if (options.clear_device_selection && m_device_manager) {
        m_device_manager->unSelected();
    }
    if (options.clear_current_target) {
        m_cur_deviceId.clear();
        m_ip.clear();
        m_web.Clear();
    }
    if (options.clear_persisted_selection) {
        wxGetApp().app_config->set("last_selected_machine", "");
        wxGetApp().app_config->set_bool("last_sel_machine_is_net", false);
        wxGetApp().app_config->set("machine_list_net", "0");
    }
    if (options.blank_browser && m_browser) {
        WebView::LoadUrl(m_browser, "about:blank");
        m_web.Clear();
    }
    if ((options.blank_browser || options.load_placeholder) && m_status_book) {
        m_status_book->ChangeSelection(0);
    }
    if (options.reset_mainframe_context && wxGetApp().mainframe) {
        wxGetApp().mainframe->is_net_url = false;
        wxGetApp().mainframe->printer_view_ip = "";
        wxGetApp().mainframe->printer_view_url = "";
        if (options.set_mainframe_not_webview) {
            wxGetApp().mainframe->is_webview = false;
        }
    }

    SetConnectionPhase(MonitorConnectionPhase::Disconnected);
    if (options.load_placeholder) {
        LoadDisconnectPageOnly();
    }
    UpdateState();
}

void QDSPrinterWebView::TransitionToLocalDevice(const std::string& device_id, DeviceButton* machine_button, const wxString& ip)
{
    if (machine_button == nullptr) {
        return;
    }

#if QDT_RELEASE_TO_PUBLIC
    if (wxGetApp().app_config->get_bool("last_sel_machine_is_net")) {
        stopCloudStatusStream();
    }

    //y83
    if (P2PManager::instance().isConnected())
        P2PManager::instance().disconnect();
#endif

    cancelAllDevButtonSelect();
    machine_button->SetIsSelected(true);

    bool expert_mode = true;
    //y84
    {
        auto mode_it = m_device_id_to_expert_mode.find(device_id);
        if (mode_it != m_device_id_to_expert_mode.end()) {
            expert_mode = mode_it->second;
        }
    }
    m_cur_deviceId = device_id;

    //cj_5
    ApplyStatusContext(m_cur_deviceId, MonitorConnectionPhase::LocalPrinter);
    UpdateState();

    if (expert_mode) {
        m_device_manager->setSelected(m_cur_deviceId);
        m_device_manager->reconnectDevice(m_cur_deviceId);
        m_status_book->ChangeSelection(0);
        allsizer->Layout();
        FormatUrl(into_u8(ip));
    }
    else {
        m_device_manager->setSelected(m_cur_deviceId);
        m_device_manager->reconnectDevice(m_cur_deviceId);
        m_device_manager->getFileInfo(m_cur_deviceId);

        auto device = m_device_manager->getDevice(m_cur_deviceId);
        if (device != nullptr) {
            device->box_is_update = true;
            //y84
            if (device->m_firmware_version.empty()) {
                m_device_manager->getDeviceInfo(m_cur_deviceId);
            }
        }

        m_status_book->ChangeSelection(1);
        LoadDisconnectPageOnly();
        allsizer->Layout();
        if (wxGetApp().mainframe != nullptr) {
            wxGetApp().mainframe->is_webview = false;
        }
        m_ip = ip;
    }

    SetConnectionPhase(MonitorConnectionPhase::LocalPrinter);
    wxGetApp().app_config->set("machine_list_net", "0");
    wxGetApp().app_config->set("last_selected_machine", into_u8(machine_button->getIPLabel()));
    wxGetApp().app_config->set_bool("last_sel_machine_is_net", false);
    wxGetApp().plater()->update_machine_sync_status();
}

#if QDT_RELEASE_TO_PUBLIC
void QDSPrinterWebView::TransitionToCloudDevice(const NetDevice& device, DeviceButton* machine_button)
{
    if (machine_button == nullptr) {
        return;
    }

    showLoadingOverlay();


    cancelAllDevButtonSelect();
    machine_button->SetIsSelected(true);
    //cj_5
    ApplyStatusContext(device.serial_number, MonitorConnectionPhase::CloudPrinter);


    //y83
    std::shared_ptr<QDSDevice> dev = m_device_manager->getDevice(device.serial_number);

    //y84: fetch filament config only after the user selects (clicks) this device,
    // not at device/button initialization time.
    // if (dev) {
    //     dev->updateFilamentConfig();
    // }

    // y84
    stopCloudStatusStream();
    startMqttDeviceStream(device.serial_number);

    dev->active_p2p = false;
    std::string user_id_ = wxGetApp().app_config->get("preset_folder");
    std::string target = "Bearer ";
    std::string t_user_token = wxGetApp().app_config->get("user_token");
    size_t pos = t_user_token.find(target);
    if (pos != std::string::npos) {
        t_user_token.erase(pos, target.length());
    }

    std::string p2p_clientId = user_id_ + "_" + t_user_token;

    //y83
    P2PManager::ConnectParams params;
    params.client_id   = p2p_clientId;
    params.server      = device.p2p_server;
    params.relay_list  = device.p2p_relay_list;
    params.device_id   = device.p2p_license;
    params.user_token  = t_user_token;

    std::string serial = device.serial_number;
    std::thread([this, dev, params, serial]() {
        bool connected = P2PManager::instance().ensureConnection(
            params, [dev](bool c) { dev->active_p2p = c; });
        dev->active_p2p = connected;
        m_device_manager->getFileInfo(serial);
    }).detach();

    new std::thread([this, dev]() {
        HttpData httpData;
        json bodyJson;
        bodyJson["serialNumber"] = dev->m_serial_number;
        bodyJson["commandId"] = make_filament_info_command_id(dev->m_serial_number);
        httpData.body = bodyJson.dump();
        httpData.env = m_env;
        httpData.target = PRINTERTYPE;

        //y78
        httpData.taskPath = QIDIMakerUrlBuilder::MakerTaskPath::kDatabaseConfigAll;

        bool isSucceed = false;
        std::string resultBody = MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);
        if (!isSucceed) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "http error" << isSucceed << std::endl;
        }
    });


    m_status_book->ChangeSelection(1);
    LoadDisconnectPageOnly();
    m_device_manager->setSelected(device.serial_number);
    m_cur_deviceId = device.serial_number;
    allsizer->Layout();
    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->is_webview = false;
    }
    m_ip = device.local_ip;
    wxGetApp().plater()->update_machine_sync_status();

    SetConnectionPhase(MonitorConnectionPhase::CloudPrinter);
    UpdateState();

    wxGetApp().app_config->set("last_selected_machine", into_u8(machine_button->getIPLabel()));
    wxGetApp().app_config->set_bool("last_sel_machine_is_net", true);
    wxGetApp().app_config->set("machine_list_net", "1");
    

    // 获取设备错误/通知信息
    new std::thread([this, serial_number = device.serial_number]() {
        HttpData httpData;
        json bodyJson;
        bodyJson["serialNumber"] = serial_number;
        httpData.body = bodyJson.dump();
        httpData.env = m_env;
        httpData.target = PRINTERTYPE;
        httpData.taskPath = QIDIMakerUrlBuilder::MakerTaskPath::kNotifyAll;

        bool isSucceed = false;
        std::string resultBody = MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);
        if (!isSucceed) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                << " get/notify/all http error" << std::endl;
            return;
        }

        try
        {
            json jsonBody = json::parse(resultBody);
            json jsonResult = jsonBody["data"];
            std::shared_ptr<QDSDevice> tempDevice = m_device_manager->getDevice(serial_number);
            tempDevice->updateAllErrorData(jsonResult);
        }
        catch (...)
        {
        	
        
        }

    });

    std::shared_ptr<QDSDevice> tempDevice = m_device_manager->getDevice(device.serial_number);
    if (tempDevice != nullptr) {
        tempDevice->box_is_update = true;
    }
}

//cj_5
void QDSPrinterWebView::TransitionToNetDeviceViaLocal(const NetDevice& net_device,
                                                     const LocalDiscoveredDevice& local_device,
                                                     DeviceButton* machine_button)
{
    if (machine_button == nullptr) {
        return;
    }

    // Close cloud SSE client if it was active from a previous net device
    if (wxGetApp().app_config->get_bool("last_sel_machine_is_net")) {
        stopCloudStatusStream();
    }

    // Select this button, deselect others
    cancelAllDevButtonSelect();
    machine_button->SetIsSelected(true);

    // Device ID is the cloud serial_number — consistent with AddNetButton
    const std::string device_id = net_device.serial_number;
    m_cur_deviceId = device_id;

    

    // Update the existing QDSDevice with local connection info, but keep is_net_device = true
	const std::string local_ip = local_device.ip;
	std::shared_ptr<QDSDevice> device = m_device_manager->getDevice(device_id);
    
    if (device) {
        device->m_url = "ws://" + local_ip + ":7125/websocket";
        device->m_ip  = local_ip;
        device->m_frp_url = "http://" + local_ip;
        // Keep is_net_device = true — the button stays in the net section.
        //y85
        device->is_local_transitioned = true;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__
            << " Updated net device " << device_id
            << " to use local WebSocket at " << device->m_url << std::endl;
    }

    //cj_5
    auto netDevices = wxGetApp().get_devices();
    for (int i = 0; i < netDevices.size(); ++i) {
        if (netDevices[i].id == net_device.id && netDevices[i].local_ip == net_device.local_ip) {
            netDevices[i].url = device->m_frp_url;
            netDevices[i].is_local_transitioned = true;
        }
    }
    wxGetApp().set_devices(netDevices);
    // Apply status context as LocalPrinter (WebSocket-based communication)
    ApplyStatusContext(device_id, MonitorConnectionPhase::LocalPrinter);
    UpdateState();

    m_device_manager->setSelected(device_id);
    m_device_manager->reconnectDevice(device_id);
    m_device_manager->getFileInfo(device_id);

    if (device) {
        device->box_is_update = true;
    }

    m_status_book->ChangeSelection(1);
    LoadDisconnectPageOnly();
    allsizer->Layout();
    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->is_webview = false;
    }
    m_ip = wxString::FromUTF8(local_ip);

    // Set connection phase to LocalPrinter (WebSocket active)
    SetConnectionPhase(MonitorConnectionPhase::LocalPrinter);
    UpdateState();

    // Persist selection: keep net section visible and marked as net
    wxGetApp().app_config->set("last_selected_machine", into_u8(machine_button->getIPLabel()));
    wxGetApp().app_config->set_bool("last_sel_machine_is_net", true);
    wxGetApp().app_config->set("machine_list_net", "1");
    wxGetApp().plater()->update_machine_sync_status();
}
#endif

        //cj_2
class LoadingOverlayWithGif : public wxFrame
{
private:
    wxWindow* m_target_window { nullptr };
    wxPanel* m_root_panel { nullptr };
    //cj_4
    wxFrame* m_center_frame { nullptr };
    wxPanel* m_center_panel { nullptr };
    wxAnimationCtrl* m_animation_ctrl { nullptr };

    wxStaticText* m_text { nullptr };
    wxTimer* m_show_delay_timer { nullptr };
    wxTimer* m_hide_delay_timer { nullptr };
    wxTimer* m_timeout_timer { nullptr };
    std::chrono::steady_clock::time_point m_visible_since {};
    bool m_pending_show { false };

    static constexpr int kShowDelayMs = 10;
    static constexpr int kMinVisibleMs = 300;
    static constexpr int kTimeoutMs = 20000;

public:
    explicit LoadingOverlayWithGif(wxWindow* owner)
        : wxFrame(nullptr,
                  wxID_ANY,
                  wxEmptyString,
                  wxDefaultPosition,
                  wxDefaultSize,
                  wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE | wxFRAME_TOOL_WINDOW)
    {
        //cj_4
        m_target_window = wxGetTopLevelParent(owner);

        SetBackgroundColour(wxColour(0, 0, 0));
        //cj_4
        SetTransparent(180);

        //cj_4
        m_show_delay_timer = new wxTimer(this, wxWindow::NewControlId());
        m_hide_delay_timer = new wxTimer(this, wxWindow::NewControlId());
        m_timeout_timer = new wxTimer(this, wxWindow::NewControlId());
        Bind(wxEVT_TIMER, &LoadingOverlayWithGif::onShowDelayTimer, this, m_show_delay_timer->GetId());
        Bind(wxEVT_TIMER, &LoadingOverlayWithGif::onHideDelayTimer, this, m_hide_delay_timer->GetId());
        Bind(wxEVT_TIMER, &LoadingOverlayWithGif::onTimeoutTimer, this, m_timeout_timer->GetId());

        //cj_4
        m_root_panel = new wxPanel(this);
        m_root_panel->SetBackgroundColour(wxColour(0, 0, 0));
        auto* root_sizer = new wxBoxSizer(wxVERTICAL);
        root_sizer->Add(1, 1, 1, wxEXPAND, 0);
        m_root_panel->SetSizer(root_sizer);

        //cj_4
        m_center_frame = new wxFrame(nullptr,
            wxID_ANY,
            wxEmptyString,
            wxDefaultPosition,
            wxSize(300, 100),
            wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE | wxFRAME_TOOL_WINDOW);
        m_center_frame->SetBackgroundColour(*wxWHITE);
        //cj_4
        {
            wxBitmap shape_bmp(300, 100, 32);
            wxMemoryDC mem_dc(shape_bmp);
            mem_dc.SetBackground(*wxBLACK_BRUSH);
            mem_dc.Clear();
            mem_dc.SetBrush(*wxWHITE_BRUSH);
            mem_dc.SetPen(*wxTRANSPARENT_PEN);
            mem_dc.DrawRoundedRectangle(0, 0, 300, 100, 14);
            mem_dc.SelectObject(wxNullBitmap);
            wxRegion region(shape_bmp, *wxBLACK);
            m_center_frame->SetShape(region);
        }


        m_center_panel = new wxPanel(m_center_frame);
        m_center_panel->SetBackgroundColour(*wxWHITE);
		m_center_panel->SetMinSize(wxSize(300, 100));
		m_center_panel->SetMaxSize(wxSize(300, 100));
        //cj_4
        m_center_panel->SetSize(wxSize(300, 100));

        auto* center_sizer = new wxBoxSizer(wxHORIZONTAL);
        //cj_4
        center_sizer->AddStretchSpacer(1);

        m_animation_ctrl = new wxAnimationCtrl(m_center_panel, wxID_ANY, wxNullAnimation, wxDefaultPosition, wxDefaultSize, wxAC_DEFAULT_STYLE);

        auto gif_path = Slic3r::var("wait1.gif");// "D:\\Backup\\Downloads\\wait1.gif";
        if (m_animation_ctrl->LoadFile(gif_path)) {
            const wxSize gif_size = m_animation_ctrl->GetAnimation().GetSize();
            //cj_4
            const wxSize target_gif_size(gif_size.GetWidth() + 10, gif_size.GetHeight() + 10);
            m_animation_ctrl->SetMinSize(target_gif_size);
            m_animation_ctrl->SetSize(target_gif_size);
            center_sizer->Add(m_animation_ctrl, 0, wxALIGN_CENTER_VERTICAL, 0);
        }


        m_text = new wxStaticText(m_center_panel, wxID_ANY, _L("Loading"));
        //cj_4
        wxFont loading_font = m_text->GetFont();
        //cj_4
        loading_font.SetPointSize(13);
        loading_font.SetWeight(wxFONTWEIGHT_NORMAL);
        m_text->SetFont(loading_font);

        m_text->SetForegroundColour(*wxBLACK);

        center_sizer->Add(m_text, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
        //cj_4
        center_sizer->AddStretchSpacer(1);

        //cj_4
        auto* center_outer_sizer = new wxBoxSizer(wxVERTICAL);
        center_outer_sizer->AddStretchSpacer(1);
        center_outer_sizer->Add(center_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
        center_outer_sizer->AddStretchSpacer(1);
        m_center_panel->SetSizer(center_outer_sizer);
        //cj_4
        m_center_panel->Layout();


        auto* center_frame_sizer = new wxBoxSizer(wxVERTICAL);
        center_frame_sizer->Add(m_center_panel, 1, wxEXPAND, 0);
        m_center_frame->SetSizer(center_frame_sizer);
        //cj_4
        m_center_frame->Layout();



        //cj_4
        Bind(wxEVT_LEFT_DOWN, &LoadingOverlayWithGif::onMouseConsume, this);
        Bind(wxEVT_LEFT_UP, &LoadingOverlayWithGif::onMouseConsume, this);
        Bind(wxEVT_RIGHT_DOWN, &LoadingOverlayWithGif::onMouseConsume, this);
        Bind(wxEVT_RIGHT_UP, &LoadingOverlayWithGif::onMouseConsume, this);
        Bind(wxEVT_MOTION, &LoadingOverlayWithGif::onMouseConsume, this);
        Bind(wxEVT_MOUSEWHEEL, &LoadingOverlayWithGif::onMouseWheelConsume, this);
        Bind(wxEVT_CHAR_HOOK, &LoadingOverlayWithGif::onCharHook, this);

        //cj_4
        if (m_center_frame) {
            m_center_frame->Bind(wxEVT_LEFT_DOWN, &LoadingOverlayWithGif::onMouseConsume, this);
            m_center_frame->Bind(wxEVT_LEFT_UP, &LoadingOverlayWithGif::onMouseConsume, this);
            m_center_frame->Bind(wxEVT_RIGHT_DOWN, &LoadingOverlayWithGif::onMouseConsume, this);
            m_center_frame->Bind(wxEVT_RIGHT_UP, &LoadingOverlayWithGif::onMouseConsume, this);
            m_center_frame->Bind(wxEVT_MOTION, &LoadingOverlayWithGif::onMouseConsume, this);
            m_center_frame->Bind(wxEVT_MOUSEWHEEL, &LoadingOverlayWithGif::onMouseWheelConsume, this);
            m_center_frame->Bind(wxEVT_CHAR_HOOK, &LoadingOverlayWithGif::onCharHook, this);
        }


        if (m_target_window) {
            m_target_window->Bind(wxEVT_MOVE, &LoadingOverlayWithGif::onTargetMove, this);
            m_target_window->Bind(wxEVT_SIZE, &LoadingOverlayWithGif::onTargetSize, this);
        }

        Hide();
        if (m_center_frame) {
            m_center_frame->Hide();
        }
    }


    ~LoadingOverlayWithGif() override
    {
        if (m_target_window) {
            m_target_window->Unbind(wxEVT_MOVE, &LoadingOverlayWithGif::onTargetMove, this);
            m_target_window->Unbind(wxEVT_SIZE, &LoadingOverlayWithGif::onTargetSize, this);
        }

        //cj_4
        if (m_center_frame) {
            m_center_frame->Destroy();
            m_center_frame = nullptr;
            m_center_panel = nullptr;
        }
        
        if (m_show_delay_timer) {

            m_show_delay_timer->Stop();
            delete m_show_delay_timer;
            m_show_delay_timer = nullptr;
        }
        if (m_hide_delay_timer) {
            m_hide_delay_timer->Stop();
            delete m_hide_delay_timer;
            m_hide_delay_timer = nullptr;
        }
        if (m_timeout_timer) {
            m_timeout_timer->Stop();
            delete m_timeout_timer;
            m_timeout_timer = nullptr;
        }
    }

    void RequestShow()
    {
        m_pending_show = true;
        if (m_hide_delay_timer) {
            m_hide_delay_timer->Stop();
        }
        if (IsShown()) {
            startTimeout();
            return;
        }

        if (m_show_delay_timer) {
            m_show_delay_timer->StartOnce(kShowDelayMs);
        }
    }

    void RequestHide()
    {
        m_pending_show = false;
        if (m_show_delay_timer) {
            m_show_delay_timer->Stop();
        }
        if (!IsShown()) {
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const int elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now - m_visible_since).count());
        const int delay_ms = std::max(0, kMinVisibleMs - elapsed_ms);
        if (delay_ms > 0 && m_hide_delay_timer) {
            m_hide_delay_timer->StartOnce(delay_ms);
            return;
        }

        doHide();
    }

private:
    void syncToTarget()
    {
        if (!m_target_window) {
            return;
        }

        const wxRect target_rect = m_target_window->GetScreenRect();
        SetSize(target_rect);
        if (m_root_panel) {
            m_root_panel->SetSize(GetClientSize());
            m_root_panel->Layout();
        }

        //cj_4
        if (m_center_frame) {
            const wxSize center_size = m_center_frame->GetSize();
            const int center_x = target_rect.x + (target_rect.width - center_size.x) / 2;
            const int center_y = target_rect.y + (target_rect.height - center_size.y) / 2;
            m_center_frame->SetPosition(wxPoint(center_x, center_y));
        }
    }

    void doShow()
    {
        syncToTarget();
        Show();
        Raise();
        if (m_center_frame) {
            m_center_frame->Show();
            m_center_frame->Raise();
        }
        if (m_animation_ctrl && m_animation_ctrl->GetAnimation().IsOk()) {
            m_animation_ctrl->Play();
        }
        m_visible_since = std::chrono::steady_clock::now();
        startTimeout();
    }


    void doHide()
    {
        if (m_timeout_timer) {
            m_timeout_timer->Stop();
        }
        if (m_animation_ctrl) {
            m_animation_ctrl->Stop();
        }
        if (m_center_frame) {
            m_center_frame->Hide();
        }
        Hide();
    }

    void startTimeout()
    {
        if (m_timeout_timer) {
            m_timeout_timer->StartOnce(kTimeoutMs);
        }
    }

    void onShowDelayTimer(wxTimerEvent& event)
    {
        boost::ignore_unused(event);
        if (!m_pending_show) {
            return;
        }

        //y84
        int sel = wxGetApp().mainframe->m_tabpanel->GetSelection();
        bool isCurPanel = (sel == MainFrame::tpMonitor);
        if (isCurPanel)
            doShow();
    }

    void onHideDelayTimer(wxTimerEvent& event)
    {
        boost::ignore_unused(event);
        if (m_pending_show) {
            return;
        }
        doHide();
    }

    void onTimeoutTimer(wxTimerEvent& event)
    {
        boost::ignore_unused(event);
        m_pending_show = false;
        doHide();
    }

    void onTargetMove(wxMoveEvent& event)
    {
        syncToTarget();
        event.Skip();
    }

    void onTargetSize(wxSizeEvent& event)
    {
        syncToTarget();
        event.Skip();
    }

    void onMouseConsume(wxMouseEvent& event)
    {
        boost::ignore_unused(event);
    }

    void onMouseWheelConsume(wxMouseEvent& event)
    {
        boost::ignore_unused(event);
    }

    void onCharHook(wxKeyEvent& event)
    {
        boost::ignore_unused(event);
    }
};
    
// //B45
QDSPrinterWebView::QDSPrinterWebView(wxWindow* parent) : 
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
    SetBackgroundColour(wxColour(255, 255, 255));
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

	// TimedDisappearance hides itself after the timer expires.
	// toast->call_start_gradual_disappearance() can also start it manually.

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
    m_task_dispatcher = std::make_unique<PrinterTaskDispatcher>(m_device_manager);
    //cj_4
    m_progress_watchdog_timer = new wxTimer(this, wxWindow::NewControlId());
    Bind(wxEVT_TIMER, &QDSPrinterWebView::onProgressWatchdogTimer, this, m_progress_watchdog_timer->GetId());
    resetProgressWatchdogHeartbeat();
    m_progress_watchdog_timer->Start(1000);
    //y83 Bounded-rate coalesced status refresh (UI thread)
    m_status_refresh_timer = new wxTimer(this, wxWindow::NewControlId());
    Bind(wxEVT_TIMER, &QDSPrinterWebView::onStatusRefreshTimer, this, m_status_refresh_timer->GetId());
    m_status_refresh_timer->Start(kStatusRefreshIntervalMs);

    initEventToTaskPath();


    // B45
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &QDSPrinterWebView::OnScriptMessage, this);
    Bind(EVT_PRINTER_TASK_RESULT, &QDSPrinterWebView::onTaskDispatchResult, this);

    Bind(wxEVT_CLOSE_WINDOW, &QDSPrinterWebView::OnClose, this);

    bool m_isloginin = (wxGetApp().app_config->get("user_token") != "");
    SetLoginStatus(m_isloginin);

    // cj_1
    t_status_panel->Bind(EVTSET_FILAMENT_INFO, &QDSPrinterWebView::onSetBoxTask, this);
    t_status_panel->Bind(EVTSET_FILAMENT_LOAD, &QDSPrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_UNLOAD, &QDSPrinterWebView::onSetBoxTask, this);
    //cj_4
    t_status_panel->Bind(EVTSET_FILAMENT_EJECT, &QDSPrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVT_AMS_REFRESH_RFID, &QDSPrinterWebView::onRefreshRfid, this);
    //cj_4
    t_status_panel->Bind(EVT_TIMELAPSE_DELETE_UI, &QDSPrinterWebView::onTimelapseDeleteUi, this);
    t_status_panel->Bind(EVT_MODEL_FILE_LIST_COMMAND, &QDSPrinterWebView::onModelFileListCommand, this);
    //cj_4
    t_status_panel->Bind(EVTSET_DOWNLOAD_TIMELAPSE_FILE, &QDSPrinterWebView::downloadTimelapseFile, this);
    //cj_4
    t_status_panel->Bind(EVT_TIMELAPSE_PLAY_FILE, &QDSPrinterWebView::playTimelapseFile, this);
    t_status_panel->Bind(EVT_TIMELAPSE_REVEAL_FILE, &QDSPrinterWebView::revealTimelapseFile, this);
    t_status_panel->Bind(EVT_TIMELAPSE_DOWNLOAD_ONE, &QDSPrinterWebView::downloadTimelapseOne, this);
    bindTaskHandle();
    CallAfter([this]() {
        init_select_machine();
    });
    
    //cj_4
    m_printer_view_bootstrap = false;
}

//cj_4
void QDSPrinterWebView::showLoadingOverlay()
{


    //cj_4 Skip overlay during init_select_machine session restore.
    if (m_printer_view_bootstrap) {
        return;
    }



    if (!m_loading_overlay) {
        m_loading_overlay = std::make_unique<LoadingOverlayWithGif>(this);
    }
    
    m_loading_overlay->RequestShow();
}

//cj_4
void QDSPrinterWebView::hideLoadingOverlay()
{
    if (!m_loading_overlay) {
        return;
    }
    m_loading_overlay->RequestHide();
}

//cj_4
void QDSPrinterWebView::stopLegacyStatusPolling()
{
    m_stop_legacy_status_polling = true;
    if (m_legacy_status_thread.joinable()) {
        m_legacy_status_thread.join();
    }
}

//cj_4
void QDSPrinterWebView::resetProgressWatchdogHeartbeat()
{
    m_last_progress_heartbeat = std::chrono::steady_clock::now();
    m_last_progress_signature.clear();
    m_watchdog_camera_active = false;
}

//cj_4
void QDSPrinterWebView::onProgressWatchdogTimer(wxTimerEvent& event)
{
    boost::ignore_unused(event);

    if (t_status_page == nullptr || m_cur_deviceId.empty()) {
        m_watchdog_camera_active = false;
        return;
    }

    const bool is_monitoring = t_status_page->is_camera_monitoring();
    if (!is_monitoring) {
        m_watchdog_camera_active = false;
        return;
    }

    if (!m_watchdog_camera_active) {
        m_watchdog_camera_active = true;
        m_last_progress_heartbeat = std::chrono::steady_clock::now();
        return;
    }

    auto device = m_device_manager->getDevice(m_cur_deviceId);
    if (device == nullptr ) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto idle_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - m_last_progress_heartbeat).count();
    if (idle_seconds >= 300) {
        BOOST_LOG_TRIVIAL(info) << "QDSPrinterWebView auto close camera: progress not updated for 5 minutes";
        t_status_page->pause_camera(true);
        m_watchdog_camera_active = false;
        m_last_progress_heartbeat = now;
    }
}


wxBoxSizer* QDSPrinterWebView::init_menu_bar(wxPanel* Panel)

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
    
    add_button->Bind(wxEVT_BUTTON, &QDSPrinterWebView::OnAddButtonClick, this);

    delete_button = new DeviceButton(Panel, "delete_machine_list_able", wxBU_LEFT);
    delete_button->SetBackgroundColor(add_btn_bg);
    delete_button->SetCanFocus(false);
    delete_button->SetCornerRadius(4);
    
    buttonsizer->Add(delete_button, 0, wxALIGN_CENTER_VERTICAL);
    
    buttonsizer->AddSpacer(14);
    
    delete_button->Bind(wxEVT_BUTTON, &QDSPrinterWebView::OnDeleteButtonClick, this);

    edit_button = new DeviceButton(Panel, "edit_machine_list_able", wxBU_LEFT);
    edit_button->SetBackgroundColor(add_btn_bg);
    edit_button->SetCanFocus(false);
    edit_button->SetCornerRadius(4);
    
    buttonsizer->Add(edit_button, 0, wxALIGN_CENTER_VERTICAL);
    
    buttonsizer->AddSpacer(14);
    
    edit_button->Bind(wxEVT_BUTTON, &QDSPrinterWebView::OnEditButtonClick, this);

    refresh_button = new DeviceButton(Panel, "refresh_machine_list_able", wxBU_LEFT);
    refresh_button->SetBackgroundColor(add_btn_bg);
    refresh_button->SetCanFocus(false);
    refresh_button->SetCornerRadius(4);
    
    buttonsizer->Add(refresh_button, 0, wxALIGN_CENTER_VERTICAL);
    
    buttonsizer->AddSpacer(16);
    
    refresh_button->Bind(wxEVT_BUTTON, &QDSPrinterWebView::OnRefreshButtonClick, this);

    buttonsizer->Layout();
    return buttonsizer;
}

void QDSPrinterWebView::init_scroll_window(wxPanel* Panel) {
    // Vertical scroll only; width is constrained by the columns and EXPAND layout.
    leftScrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    leftScrolledWindow->SetBackgroundColour(wxColour(255, 255, 255));
    leftScrolledWindow->SetSizer(devicesizer);
    leftScrolledWindow->SetScrollRate(10, 10);
    leftScrolledWindow->EnableScrolling(false, true);
    leftScrolledWindow->SetMinSize(wxSize(220, -1));
    leftScrolledWindow->FitInside();
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_TOP, &QDSPrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_BOTTOM, &QDSPrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEUP, &QDSPrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_LINEDOWN, &QDSPrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEUP, &QDSPrinterWebView::OnScroll, this);
    leftScrolledWindow->Bind(wxEVT_SCROLLWIN_PAGEDOWN, &QDSPrinterWebView::OnScroll, this);


}

 void QDSPrinterWebView::SetPresetChanged(bool status) {
     if (status) {
        clearStatusPanelData();
        DeleteButton();
        DeleteNetButton();
        leftScrolledWindow->DestroyChildren();
        devicesizer->Clear();
        m_device_manager->stopAllConnection();
        //cj_4
        {
            std::lock_guard<std::mutex> lock(m_ui_map_mutex);
            m_device_id_to_button.clear();
            m_device_id_to_expert_mode.clear();
            m_device_id_to_config.clear();
        }
        m_machine.clear();

        m_exit_host.clear();
        m_netDeviceExpand = nullptr;
        m_localDeviceExpand = nullptr;

        //cj_2
		StateColor trans_bg(
			std::pair<wxColour, int>(wxColour(200, 200, 200), StateColor::Pressed),
			std::pair<wxColour, int>(wxColour(233, 233, 233), StateColor::Hovered),
			std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
        );

        PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
        PhysicalPrinterCollection& ph_printers = preset_bundle.physical_printers;



        if(!ph_printers.empty()){
            wxBoxSizer* label_boxsizer = new wxBoxSizer(wxHORIZONTAL);
            wxStaticText* LocalDevicesLabel = new wxStaticText(leftScrolledWindow, wxID_ANY, ("Local"));
            LocalDevicesLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
            LocalDevicesLabel->SetForegroundColour(wxColour(74, 74, 74));

            //cj_2
            wxStaticBitmap* localBitmap = new wxStaticBitmap(leftScrolledWindow, wxID_ANY, create_scaled_bitmap("localList", leftScrolledWindow, 13));
            localBitmap->Hide();
            m_localDeviceExpand = new DeviceButton(leftScrolledWindow, "fold", wxBU_LEFT);
            m_localIsExpand = true;
            m_localDeviceExpand->SetBackgroundColor(trans_bg);
            m_localDeviceExpand->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
                //y84
                boost::ignore_unused(event);
                const bool expand_local = !m_localIsExpand;
                applyLocalSectionExpand(expand_local);
                if (expand_local) {
                    applyNetSectionExpand(false);
                }
                leftScrolledWindow->Layout();
            });
            m_localDeviceExpand->SetCanFocus(false);
            
            label_boxsizer->AddSpacer(10);
			label_boxsizer->Add(LocalDevicesLabel, wxSizerFlags(0).CenterVertical());
            label_boxsizer->AddStretchSpacer(1);
			label_boxsizer->Add(m_localDeviceExpand, wxSizerFlags(0).CenterVertical());
            label_boxsizer->AddSpacer(10);
            devicesizer->Add(label_boxsizer, 0, wxEXPAND);

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
                //y84
                std::string model_id = wxGetApp().preset_bundle->printers.get_edited_preset().get_printer_type_from_preset_name(wxGetApp().preset_bundle, preset_name);
                bool isQIDI_printer = false;
                if (qidi_printers.find(preset_name) != qidi_printers.end())
                    isQIDI_printer = true;

                std::string full_name = it->get_full_name(preset_name);
                if (!isQIDI_printer)
                    preset_name = "my_printer";
                const DynamicPrintConfig* cfg_t = &(it->config);

                const auto        opt = cfg_t->option<ConfigOptionEnum<PrintHostType>>("host_type");
                auto       host_type = opt != nullptr ? opt->value : htOctoPrint;
                //cj_4_cursor
                bool expert_mode = true;
                if (const auto* expert_opt = cfg_t->option<ConfigOptionBool>("expert_mode")) {
                    expert_mode = expert_opt->value;
                }

                // y13
                bool is_selected = false;

                if (!select_machine_name.empty())
                    if (select_machine_name == it->get_short_name(full_name))
                    {
                        is_selected = true;
                        select_machine_name = "";
                    }


                boost::ignore_unused(host_type);
                AddButton(from_u8(it->get_short_name(full_name)), host, preset_name, from_u8(full_name), model_id, is_selected,
                    expert_mode, apikey);
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
            NetDevicesLabel = new wxStaticText(leftScrolledWindow, wxID_ANY, "QIDI Maker");
            NetDevicesLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
            NetDevicesLabel->SetForegroundColour(wxColour(74, 74, 74));

            //cj_2
			wxStaticBitmap* netBitmap = new wxStaticBitmap(leftScrolledWindow, wxID_ANY, create_scaled_bitmap("netList", leftScrolledWindow, 13));
            netBitmap->Hide();
			m_netDeviceExpand = new DeviceButton(leftScrolledWindow, "fold", wxBU_LEFT);
            m_netDeviceExpand->SetBackgroundColor(trans_bg);
            m_netIsExpand = true;
            
            m_netDeviceExpand->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
                //y84
                boost::ignore_unused(event);
                const bool expand_net = !m_netIsExpand;
				if (!expand_net) {
                    applyNetSectionExpand(false);
                    bool selected_net_button = false;
					for (DeviceButton* button : m_net_buttons) {
                        if(button->GetIsSelected()){
                            selected_net_button = true;
                        }
					}
                    if(!selected_net_button) {
#if QDT_RELEASE_TO_PUBLIC
                        stopCloudStatusStream();
#endif
                    }
				}
				else {
                    removeDeviceButtonMapEntriesForButtons(m_net_buttons);
					for (DeviceButton* button : m_net_buttons) {
						delete button;
					}
                    m_net_buttons.clear();
#if QDT_RELEASE_TO_PUBLIC
                    //cj_5 Refresh local device cache so net-button clicks can match LAN devices
                    m_device_manager->refreshLocalDevices(false, {});
                    MakerHttpHandle::getInstance().get_maker_device_list();
                    //y84
                    if(!wxGetApp().get_devices().empty())
                        startCloudStatusStream();

					m_net_devices = wxGetApp().get_devices();
					for (const auto& device : m_net_devices) {
						AddNetButton(device);
					}
                    
#endif

//y84
                    applyNetSectionExpand(true);
                    applyLocalSectionExpand(false);
				}

                // 依据持久化的上次选中恢复线上按钮选中态
                wxString curMachineIP =  wxGetApp().app_config->get("last_selected_machine");
				for (DeviceButton* button : m_net_buttons) {
                    if (curMachineIP == into_u8(button->getIPLabel())) {
                        button->SetIsSelected(true);
                    }
				}
                leftScrolledWindow->Layout();
				});
            m_netDeviceExpand->SetCanFocus(false);

            label_boxsizer_online->AddSpacer(10);
            label_boxsizer_online->Add(NetDevicesLabel, wxSizerFlags(0).CenterVertical());
            label_boxsizer_online->AddStretchSpacer(1);
			label_boxsizer_online->Add(m_netDeviceExpand, wxSizerFlags(0).CenterVertical());
			label_boxsizer_online->AddSpacer(10);

            devicesizer->Add(label_boxsizer_online, 0, wxEXPAND);
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
    const MonitorConnectionPhase connection_phase = GetConnectionPhase();
    if (connection_phase == MonitorConnectionPhase::CloudPrinter) {
        for (DeviceButton* button : m_net_buttons) {
            wxString button_ip = button->getIPLabel();
            if (button_ip.Lower().starts_with("http"))
                button_ip.Remove(0, 7);
            //y84
            int pos = button_ip.Find(':');
            if (pos != wxNOT_FOUND) {
                button_ip = button_ip.SubString(0, pos - 1);
            }
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
    else if (connection_phase == MonitorConnectionPhase::LocalPrinter) {
        for (DeviceButton* button : m_buttons) {
            //y79
            if (button->getIPLabel() == m_ip) {
                wxEvtHandler* handler = button->GetEventHandler();
                if (handler)
                {
                    wxCommandEvent evt(wxEVT_BUTTON, button->GetId());
                    evt.SetEventObject(button);
                    handler->ProcessEvent(evt);
                }
                break;
            }
        }
    }
    else
    {
        DisconnectTransitionOptions options;
        options.load_placeholder = true;
        TransitionToDisconnected(options);
    }
    if (status) {
        syncDeviceSectionExpandFromLastSelection();
    }
    UpdateState();
    UpdateLayout();
 }
void QDSPrinterWebView::SetLoginStatus(bool status) {
    //y77
    m_isloginin = false;


    m_isloginin = status;
    if (m_isloginin) {
//y76
#if QDT_RELEASE_TO_PUBLIC
        {
            bool is_get_net_devices = false;
            wxString msg;

            is_get_net_devices = MakerHttpHandle::getInstance().get_maker_device_list();

            if (is_get_net_devices)
            {
                this->UpdateState();
                this->SetPresetChanged(true);
            }
        }

#endif
    } 
    else {
#if QDT_RELEASE_TO_PUBLIC
        std::vector<NetDevice> devices;
        wxGetApp().set_devices(devices);

        // y84
        stopCloudStatusStream();
        MQTTManager::instance().disconnect();
#endif
        const bool was_online_selected =
            (GetConnectionPhase() == MonitorConnectionPhase::CloudPrinter) ||
            wxGetApp().app_config->get_bool("last_sel_machine_is_net");
        if (was_online_selected) {
            DisconnectTransitionOptions options;
            options.clear_button_selection = true;
            options.clear_status_panel = true;
            options.clear_device_selection = true;
            options.clear_current_target = true;
            options.clear_persisted_selection = true;
            options.reset_mainframe_context = true;
            options.set_mainframe_not_webview = true;
            options.blank_browser = true;
            options.load_placeholder = false;
            TransitionToDisconnected(options);
        }
        else if (GetConnectionPhase() == MonitorConnectionPhase::CloudPrinter) {
            DisconnectTransitionOptions options;
            options.load_placeholder = false;
            TransitionToDisconnected(options);
        }
        SetPresetChanged(true);
        UpdateState();
    }
}


QDSPrinterWebView::~QDSPrinterWebView()
{
    //cj_4 Mark as destroying FIRST so any in-flight MQTT / CallAfter can bail out.
    m_isDestroying = true;

    //y83
#if QDT_RELEASE_TO_PUBLIC
    stopCloudStatusStream();

    if (P2PManager::instance().isConnected())
        P2PManager::instance().shutdown();
#endif

    if (m_progress_watchdog_timer) {
        m_progress_watchdog_timer->Stop();
        Unbind(wxEVT_TIMER, &QDSPrinterWebView::onProgressWatchdogTimer, this, m_progress_watchdog_timer->GetId());
        delete m_progress_watchdog_timer;
        m_progress_watchdog_timer = nullptr;
    }
    //y83
    if (m_status_refresh_timer) {
        m_status_refresh_timer->Stop();
        Unbind(wxEVT_TIMER, &QDSPrinterWebView::onStatusRefreshTimer, this, m_status_refresh_timer->GetId());
        delete m_status_refresh_timer;
        m_status_refresh_timer = nullptr;
    }
    //cj_6
    if (m_thumb_flush_timer) {
        m_thumb_flush_timer->Stop();
        Unbind(wxEVT_TIMER, &QDSPrinterWebView::onThumbFlushTimer, this, m_thumb_flush_timer->GetId());
        delete m_thumb_flush_timer;
        m_thumb_flush_timer = nullptr;
    }
    if (m_device_error_dlg) {
        m_device_error_dlg->Destroy();
        m_device_error_dlg = nullptr;
    }

    //cj_4 Stop polling before unbinding callbacks so stopAllConnection cannot CallAfter a destroyed object.
    stopLegacyStatusPolling();
    if (m_device_manager) {
        m_device_manager->setConnectionEventCallback({});
        m_device_manager->setParameterUpdateCallback({});
        m_device_manager->setDeleteDeviceIDCallback({});
        m_device_manager->setFileInfoUpdateCallback({});
        //y84
        m_device_manager->setFileThumbnailReadyCallback({});
        m_device_manager->stopAllConnection();
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";

    SetEvtHandlerEnabled(false);

    //y77
    m_isloginin = false;

    //cj_4
    hideLoadingOverlay();
    m_loading_overlay.reset();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";

    //y84
    m_lifetime.reset();
}

// // B55
 void QDSPrinterWebView::AddButton(const wxString &    device_name,
                                const wxString &    ip,
                                const wxString &    machine_type,
                                const wxString &    fullname,
                                const std::string&  model_id,
                                bool                isSelected,
                                //cj_4_cursor
                                bool                expert_mode,
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
    //cj_4
    machine_button->SetStateText("offline");

    //y83
    wxString ip_without_colon = ip;
    if (ip_without_colon.Lower().ends_with("7125"))
        ip_without_colon.Remove(ip_without_colon.length() - 5);

    // Register device information with the manager; addDevice starts the connection.
    std::string t_device_id = m_device_manager->addDevice(
        into_u8(device_name),
        into_u8(ip),
        /* dev_url */ into_u8(ip_without_colon),
        into_u8(machine_type),
        model_id
    );

     if (!t_device_id.empty()) {
        std::lock_guard<std::mutex> lock(m_ui_map_mutex);
        m_device_id_to_button[t_device_id] = machine_button;
        m_device_id_to_expert_mode[t_device_id] = expert_mode;
        DynamicPrintConfig printer_cfg;
        printer_cfg.set_key_value("print_host", new ConfigOptionString(into_u8(ip)));
        printer_cfg.set_key_value("printhost_apikey", new ConfigOptionString(into_u8(apikey)));
        printer_cfg.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htOctoPrint));
        m_device_id_to_config[t_device_id] = printer_cfg;
    }
    else {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Add device failed: " << into_u8(device_name) << std::endl;
    }

    //y76
    machine_button->Bind(wxEVT_BUTTON, [this, t_device_id, machine_button, ip](wxCommandEvent &event) {
        boost::ignore_unused(event);
        TransitionToLocalDevice(t_device_id, machine_button, ip);
    });
    devicesizer->Add(machine_button, 0, wxEXPAND);
    devicesizer->Layout();
    Layout();
    m_buttons.push_back(machine_button);
}

// y22
 std::string QDSPrinterWebView::NormalizeVendor(const std::string& str) 
 {
     std::string normalized;
     for (char c : str) {
         if (std::isalnum(c)) {
             normalized += std::tolower(c);
         }
     }
     return normalized;
 }

//cj_4
bool QDSPrinterWebView::select_device_by_id(const std::string& device_id)
{
    if (device_id.empty()) return false;

    DeviceButton* target_button = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_ui_map_mutex);
        auto it = m_device_id_to_button.find(device_id);
        if (it != m_device_id_to_button.end()) target_button = it->second;
    }

    if (target_button == nullptr) return false;

    wxEvtHandler* handler = target_button->GetEventHandler();
    if (handler == nullptr) return false;

    wxCommandEvent evt(wxEVT_BUTTON, target_button->GetId());
    evt.SetEventObject(target_button);
    return handler->ProcessEvent(evt);
}

 #if QDT_RELEASE_TO_PUBLIC
 void QDSPrinterWebView::AddNetButton(const NetDevice device)
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
         device_name = from_u8(device.device_name);
         std::string extracted = device.machine_type;
         for (std::string machine_vendor : qidi_printers)
         {
             if (NormalizeVendor(machine_vendor).find(NormalizeVendor(extracted)) != std::string::npos)
             {
                 Machine_Name = Machine_Name.Format("%s%s", machine_vendor, "_thumbnail");
                 t_device_type = machine_vendor;
                 break;
             }
         }
     }
     else
     {
         device_name = from_u8(device.device_name);
         std::size_t found = device.device_name.find('@');
         if (found != std::string::npos)
         {
             std::string extracted = device.device_name.substr(found + 1);
             for (std::string machine_vendor : qidi_printers)
             {
                 if (NormalizeVendor(machine_vendor).find(NormalizeVendor(extracted)) != std::string::npos)
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

     DeviceButton *machine_button = new DeviceButton(leftScrolledWindow, device_name, machine_icon_path, wxBU_LEFT, wxSize(20, 20),

                                                     device_name, device.local_ip);
     machine_button->SetBackgroundColor(mac_btn_bg);
     machine_button->SetForegroundColour(wxColor(119, 119, 119));
     machine_button->SetFont(Label::Body_16);
     //machine_button->SetBorderColor(wxColour(57, 51, 55));
     machine_button->SetCanFocus(false); 
     machine_button->SetCornerRadius(0);


      machine_button->Bind(wxEVT_BUTTON, [this, device, machine_button](wxCommandEvent& event) {
        boost::ignore_unused(event);
		//cj_5 Try local WebSocket first if the same device is on LAN (via SSDP).
		LocalDiscoveredDevice local_dev;
		auto* qds = wxGetApp().qdsdevmanager;
		if (qds && !device.local_ip.empty()
		    && qds->findSSDPDeviceByIP(device.local_ip, local_dev)) {
			TransitionToNetDeviceViaLocal(device, local_dev, machine_button);
		} else {
			TransitionToCloudDevice(device, machine_button);
		}
        });

     devicesizer->Add(machine_button, 0, wxEXPAND);
     devicesizer->Layout();
     m_net_buttons.push_back(machine_button);
     {
         std::lock_guard<std::mutex> lock(m_ui_map_mutex);
         m_device_id_to_button[device.serial_number] = machine_button;
     }
 	 auto qdsDevice = std::make_shared<QDSDevice>(device.serial_number, device.device_name, "", "", t_device_type, device.model_id, device.firmware_version);
     qdsDevice->m_frp_url = device.url;// +"/webcam/?action=snapshot";
     qdsDevice->m_ip = device.local_ip;
     qdsDevice->m_serial_number = device.serial_number;
     qdsDevice->m_mac_address = device.mac_address;
     //y78
     qdsDevice->is_net_device = true;
     qdsDevice->p2p_enable = true;
     //y83
     qdsDevice->p2p_license = device.p2p_license;
     qdsDevice->p2p_server = device.p2p_server;
     qdsDevice->p2p_relay_list = device.p2p_relay_list;
     qdsDevice->m_machine_name = device.device_name;
     qdsDevice->m_status = device.is_online ? "standy" : "offline";

     machine_button->SetStateText(qdsDevice->m_status);
     m_device_manager->addDevice(qdsDevice);

     //y84
     std::thread get_config_thread([this, qdsDevice]() {
         HttpData httpData;
         json bodyJson;
         bodyJson["serialNumber"] = qdsDevice->m_id;
         httpData.body = bodyJson.dump();
#if QDT_RELEASE_TO_PUBLIC
         std::string region = wxGetApp().app_config->get("region");
         if (region == "China") {
             httpData.env = PRODUCTIONENV;
         }
         else {
             httpData.env = FOREIGNENV;
         }
#endif
         httpData.target = DEVICE;

         //y78
         httpData.taskPath = QIDIMakerUrlBuilder::MakerTaskPath::kConfigList;

         bool isSucceed = false;
         std::string resultBody = MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);
         if (!isSucceed) {
             BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "http error" << isSucceed << std::endl;
         }

         try {
             json resultJson = json::parse(resultBody);
             if(resultJson.contains("data"))
                qdsDevice->update_device_config(resultJson["data"]);
         }
         catch (...) {
         }
         });
     get_config_thread.detach();
}
 #endif

 void QDSPrinterWebView::RefreshButton()
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
 void QDSPrinterWebView::UnSelectedButton()
 {
     if (m_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         for (DeviceButton *button : m_buttons) {
             button->SetIsSelected(false);
         }
     }
 }
 void QDSPrinterWebView::DeleteButton()
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
 void QDSPrinterWebView::DeleteNetButton()
 {
     if (m_net_buttons.empty()) {
         BOOST_LOG_TRIVIAL(info) << " empty";
     } else {
         removeDeviceButtonMapEntriesForButtons(m_net_buttons);
         for (DeviceButton *button : m_net_buttons) {
             delete button;
         }
         m_net_buttons.clear();
     }
 }

//cj_4
void QDSPrinterWebView::removeDeviceButtonMapEntriesForButtons(const std::vector<DeviceButton*>& buttons)
{
    if (buttons.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_ui_map_mutex);
    for (auto it = m_device_id_to_button.begin(); it != m_device_id_to_button.end();) {
        if (std::find(buttons.begin(), buttons.end(), it->second) != buttons.end()) {
            it = m_device_id_to_button.erase(it);
        } else {
            ++it;
        }
    }
}
 void QDSPrinterWebView::ShowNetPrinterButton()
 {
     HideAllDeviceButtons();
     ShowDeviceButtons(m_net_buttons);
     leftScrolledWindow->Layout();
     Refresh();
 }
 void QDSPrinterWebView::ShowLocalPrinterButton()
 {
 
     HideAllDeviceButtons();
     ShowDeviceButtons(m_buttons);

     leftScrolledWindow->Layout();
     Refresh();
 }
 void QDSPrinterWebView::SetButtons(std::vector<DeviceButton *> buttons) { m_buttons = buttons; }

void QDSPrinterWebView::OnRefreshButtonClick(wxCommandEvent &event)
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
    if (m_isloginin) {
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
 void QDSPrinterWebView::OnAddButtonClick(wxCommandEvent &event)
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
void QDSPrinterWebView::OnDeleteButtonClick(wxCommandEvent &event)
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

                DisconnectTransitionOptions options;
                options.clear_device_selection = true;
                options.clear_current_target = true;
                options.clear_persisted_selection = true;
                options.reset_mainframe_context = true;
                options.load_placeholder = true;
                TransitionToDisconnected(options);
                SetPresetChanged(true);

                //cj_4 clear sidebar sync badges after deleting selected device
                wxGetApp().plater()->update_machine_sync_status();

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

                if (m_task_dispatcher != nullptr) {
                    PrinterTask task;
                    task.type = PrinterTaskType::UnbindDevice;
                    task.transport = PrinterTaskTransport::Cloud;
                    task.device_id = m_cur_deviceId;
                    PrinterTaskResult result = m_task_dispatcher->dispatch(task, m_env, DEVICE);
                    emitTaskDispatchResult(task.type, result);
                }

#endif         
                DisconnectTransitionOptions options;
                options.clear_device_selection = true;
                options.clear_current_target = true;
                options.clear_persisted_selection = true;
                options.reset_mainframe_context = true;
                options.load_placeholder = true;
                TransitionToDisconnected(options);
                SetPresetChanged(true);

                //cj_4 clear sidebar sync badges after deleting selected device
                wxGetApp().plater()->update_machine_sync_status();

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
 void QDSPrinterWebView::OnEditButtonClick(wxCommandEvent &event)
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
 // void QDSPrinterWebView::SendRecentList(int images)
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
 void QDSPrinterWebView::OnScriptMessage(wxWebViewEvent &evt)
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
 void QDSPrinterWebView::UpdateLayout()
 {
     wxSize size   = devicesizer->GetSize();
     int    height = size.GetHeight();
     int    client_w = leftScrolledWindow->GetClientSize().GetWidth();
     if (client_w < 1) {
         client_w = leftScrolledWindow->GetSize().GetWidth();
     }
     // Keep virtual width aligned with the visible area to avoid horizontal scrolling.
     leftScrolledWindow->SetVirtualSize(wxSize(std::max(client_w, 1), std::max(height, 1)));
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
 void QDSPrinterWebView::OnScrollup(wxScrollWinEvent &event)
 {
     height -= 5;
     leftScrolledWindow->Scroll(0, height);
     UpdateLayout();
     event.Skip();
 }
void QDSPrinterWebView::OnScrolldown(wxScrollWinEvent &event)
{
    height += 5;
    leftScrolledWindow->Scroll(0, height);
    UpdateLayout();
    event.Skip();
}

void QDSPrinterWebView::emitTaskDispatchResult(PrinterTaskType task_type, const PrinterTaskResult& result)
{
    wxCommandEvent result_event(EVT_PRINTER_TASK_RESULT);
    result_event.SetInt(static_cast<int>(result.code));
    result_event.SetExtraLong(static_cast<long>(task_type));
    result_event.SetString(wxString::FromUTF8(result.message.c_str()));
    wxPostEvent(this, result_event);
}

void QDSPrinterWebView::onTaskDispatchResult(wxCommandEvent& event)
{
    if (event.GetInt() == static_cast<int>(PrinterTaskErrorCode::None)) {
        return;
    }

    BOOST_LOG_TRIVIAL(error) << "task dispatch failed, taskType="
        << event.GetExtraLong() << ", code=" << event.GetInt()
        << ", msg=" << event.GetString().ToStdString();
}

void QDSPrinterWebView::onStatusPanelTask(wxCommandEvent& event)
{
   if (m_task_dispatcher == nullptr)
        return;

    PrinterTask task;
    task.type = PrinterTaskType::StatusPanel;
    task.device_id = m_cur_deviceId;
    task.event_type = event.GetEventType();
    task.int_value = event.GetInt();
    task.string_key = event.GetString().ToStdString();

    if (GetConnectionPhase() == MonitorConnectionPhase::LocalPrinter) {
        task.transport = PrinterTaskTransport::Local;
        PrinterTaskResult result = m_task_dispatcher->dispatch(task);
        emitTaskDispatchResult(task.type, result);
        return;
    }

#if QDT_RELEASE_TO_PUBLIC
    task.transport = PrinterTaskTransport::Cloud;
    PrinterTaskResult result = m_task_dispatcher->dispatch(task, m_env, PRINTERTYPE);
    emitTaskDispatchResult(task.type, result);
#endif
}


//cj_1
void QDSPrinterWebView::onSetBoxTask(wxCommandEvent& event)
{
    if (m_task_dispatcher == nullptr)
        return;

    PrinterTask task;
    task.type = PrinterTaskType::SetBox;
    task.device_id = m_cur_deviceId;
    task.event_type = event.GetEventType();
    task.slot_index = event.GetInt();

    // y84
    if (task.event_type == EVTSET_FILAMENT_INFO) {
        // Producer always attaches a FilamentInfoPayload for this event.
        if (FilamentInfoPayload* p = static_cast<FilamentInfoPayload*>(event.GetClientObject())) {
            task.filament_color = p->color;
            task.filament_vendor = p->vendor;
            task.filament_type = p->type;
        }
    }

    if (GetConnectionPhase() == MonitorConnectionPhase::LocalPrinter) {
        task.transport = PrinterTaskTransport::Local;
        PrinterTaskResult result = m_task_dispatcher->dispatch(task);
        emitTaskDispatchResult(task.type, result);
        return;
    }

#if QDT_RELEASE_TO_PUBLIC
    task.transport = PrinterTaskTransport::Cloud;
    PrinterTaskResult result = m_task_dispatcher->dispatch(task, m_env, PRINTERTYPE);
    emitTaskDispatchResult(task.type, result);
#endif
}


void QDSPrinterWebView::onRefreshRfid(wxCommandEvent& event)
{
    if (m_task_dispatcher == nullptr)
        return;

    long canId = 0;
    event.GetString().ToLong(&canId);

    PrinterTask task;
    task.type = PrinterTaskType::RefreshRfid;
    task.device_id = m_cur_deviceId;
    task.slot_index = static_cast<int>(canId);

    if (GetConnectionPhase() == MonitorConnectionPhase::LocalPrinter) {
        task.transport = PrinterTaskTransport::Local;
        PrinterTaskResult result = m_task_dispatcher->dispatch(task);
        emitTaskDispatchResult(task.type, result);
        return;
    }

#if QDT_RELEASE_TO_PUBLIC
    task.transport = PrinterTaskTransport::Cloud;
    PrinterTaskResult result = m_task_dispatcher->dispatch(task, m_env, PRINTERTYPE);
    emitTaskDispatchResult(task.type, result);
#endif
}



//cj_4
void QDSPrinterWebView::onModelFileListCommand(wxCommandEvent& event)
{
    const auto cmd = static_cast<ModelFileListCommandType>(event.GetInt());
    const wxString path = event.GetString();
    switch (cmd) {
    case ModelFileListCommandType::Print:
        PrintStatusModelFile(path);
        break;
    case ModelFileListCommandType::Download:
        downloadSinglePrinterFile(path);
        break;
    case ModelFileListCommandType::Delete:
        deleteSinglePrinterFile(path);
        break;
    case ModelFileListCommandType::RevealLocal:
        revealDownloadedPrinterFile(path);
        break;
    default:
        break;
    }
}

//y83
bool QDSPrinterWebView::downloadSinglePrinterFileViaP2P(
    const wxString& wx_printer_file_path,
    const std::shared_ptr<QDSDevice>& device,
    const std::string& localPath,
    const std::string& fileName)
{
#if QDT_RELEASE_TO_PUBLIC
    // Launch download thread to avoid blocking UI
    std::thread([this, wx_printer_file_path, localPath, fileName]() {
        ExecuteP2PDownload(wx_printer_file_path, localPath, fileName);
        }).detach();

    return true;
#else
    return false;
#endif
}

//y83
void QDSPrinterWebView::ExecuteP2PDownload(
    const wxString& wx_printer_file_path,
    const std::string& localPath,
    const std::string& fileName)
{
#if QDT_RELEASE_TO_PUBLIC
    auto p2p_dev = wxGetApp().qdsdevmanager->getSelectedDevice();
    QIDIFileManager qdsfmsg(p2p_dev);

    if(!P2PManager::instance().isConnected())
        return;

    //y84
    P2PManager::FileTransferOptions opt;
    opt.progress = [this, fileName](int64_t bytes, int64_t total) {
        if (bytes <= 0)
            return;
        if (total > 0)
            SetStatusModelFileDownloadProgress(
                fileName, std::min(1.0f, float(double(bytes) / double(total))));
        else
            SetStatusModelFileDownloadProgress(fileName, -1.f);
    };

    auto result = qdsfmsg.downloadFile(into_u8(wx_printer_file_path), localPath, opt);
    if (!result.ok) {
        BOOST_LOG_TRIVIAL(error) << "P2P download failed for " << wx_printer_file_path;
        EndStatusModelFileDownload(fileName, true);
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "P2P download completed: " << fileName
        << " (" << result.bytes << " bytes, "
        << result.chunks << " chunks)";
    EndStatusModelFileDownload(fileName, false);
    RefreshStatusModelFileLocalState();
#endif
}

//cj_4
void QDSPrinterWebView::downloadSinglePrinterFile(const wxString& wx_storage_path)
{
    //y83
    std::string t_storage_path = wx_storage_path.ToUTF8().data();
    auto json_storage_path = json::parse(t_storage_path);

    std::string fileName = json_storage_path["storage_path"].get<std::string>();
    std::string printer_file_path = json_storage_path["printer_file_path"].get<std::string>();

    std::string downloadPath = wxGetApp().app_config->get("download_path");
    if (downloadPath.empty()) {
        show_printer_webview_download_notice(
            GetStatusDialogParent(),
            _L("Download Failed"),
            _L("Please set the download path in Preferences first."));
        return;
    }

    if (local_download_target_exists(downloadPath, from_u8(fileName)))
        return;

    boost::filesystem::create_directories(boost::filesystem::path(downloadPath));

    std::shared_ptr<QDSDevice> device = m_device_manager->getDevice(m_cur_deviceId);
    if (!device)
        return;

    std::string       localPath = downloadPath + "/" + fileName;

    // P2P download first (force enabled; replace `true` with real condition later)
    auto obj = wxGetApp().qdsdevmanager->getSelectedDevice();
    if (obj->p2p_enable) {
        if (downloadSinglePrinterFileViaP2P(from_u8(printer_file_path), device, localPath, fileName)) {
            // P2P download started (async) – show indeterminate progress
            wxString wx_task_name = from_u8(fileName);
            BeginStatusModelFileDownload(wx_task_name, "");
            return;
        }
    }

    std::string       encodedName = UrlEncodeForFilename(fileName);
    std::string       urlStr      = device->m_frp_url + "/server/files/gcodes/" + encodedName + "?date=" +
        std::to_string(wxGetUTCTimeMillis().GetValue());

    const wxString wx_path_copy = from_u8(fileName);

    const std::string task_id = DownloadManager::getInstance().downloadFile(
        urlStr,
        localPath,
        fileName,
        [this, wx_path_copy, fileName](FileDownloadProgress progress) {
            if (progress.state == FileDownloadProgress::State::Completed) {
                BOOST_LOG_TRIVIAL(info) << "Downloaded: " << fileName;
                EndStatusModelFileDownload(wx_path_copy, false);
            } else if (progress.state == FileDownloadProgress::State::Failed) {
                show_printer_webview_download_notice(
                    GetStatusDialogParent(),
                    _L("Download Error"),
                    wxString::Format(_L("Download failed: %s\n%s"),
                        from_u8(fileName),
                        from_u8(progress.error_msg)));
                EndStatusModelFileDownload(wx_path_copy, true);
            }
            RefreshStatusModelFileLocalState();
        },
        [this, wx_path_copy](FileDownloadProgress progress) {
            if (progress.state != FileDownloadProgress::State::Downloading) {
                return;
            }
            float f = progress.progress;
            if (f < 0.f && progress.bytes_total > 0) {
                f = static_cast<float>(static_cast<double>(progress.bytes_downloaded) /
                    static_cast<double>(std::max<int64_t>(progress.bytes_total, 1)));
            }
            if (f < 0.f && progress.bytes_downloaded > 0) {
                f = std::min(0.92f,
                    static_cast<float>(progress.bytes_downloaded) / (48.f * 1024.f * 1024.f));
            }
            if (f < 0.f) {
                f = 0.f;
            }
            SetStatusModelFileDownloadProgress(wx_path_copy, std::min(1.f, f));
        });
    if (!task_id.empty())
        BeginStatusModelFileDownload(wx_path_copy, task_id);
}

//cj_4
void QDSPrinterWebView::deleteSinglePrinterFile(const wxString& storage_path)
{
    if (t_status_page == nullptr)
        return;
    wxWindow* dlg_parent = GetStatusDialogParent();
    std::string downloadPath = wxGetApp().app_config->get("download_path");
    const bool local_copy_exists = !downloadPath.empty() && local_download_target_exists(downloadPath, storage_path);

    if (local_copy_exists) {
        //cj_3
        auto* dlg = new SecondaryCheckDialog(dlg_parent, wxID_ANY, _L("Delete file"),
            SecondaryCheckDialog::ButtonStyle::DELETE_LOCAL_AND_BOTH_AND_CANCEL_NO_PRINTER_ONLY, wxDefaultPosition,
            wxDefaultSize, wxCLOSE_BOX | wxCAPTION, false, true);
        dlg->m_button_retry->SetLabel(_L("Delete local copy"));
        dlg->m_button_ok->SetLabel(_L("Delete on printer and locally"));
        dlg->m_button_cancel->SetLabel(_L("Cancel"));
        dlg->update_text(wxString::Format(_L("Are you sure you want to delete this file?\n\n%s"), storage_path));
        dlg->set_message_area_width(600);
        dlg->rescale();
        dlg->m_button_retry->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this, storage_path, dlg](wxCommandEvent&) {
            wxGetApp().CallAfter([this, storage_path, dlg]() {
                const bool ok = remove_local_download_for_storage_path(storage_path);
                dlg->Destroy();
                if (t_status_page) {
                    if (ok)
                        RefreshStatusModelFileLocalState();
                    else
                        show_printer_webview_download_notice(
                            GetStatusDialogParent(),
                            _L("Delete local copy"),
                            _L("Could not delete the local file."));
                }
            });
            dlg->on_hide();
        });
        dlg->m_button_ok->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this, storage_path, dlg](wxCommandEvent&) {
            wxGetApp().CallAfter([this, storage_path, dlg]() {
                run_delete_printer_file_task(storage_path, true);
                dlg->Destroy();
            });
            dlg->on_hide();
        });
        dlg->m_button_cancel->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [dlg](wxCommandEvent&) {
            dlg->on_hide();
            dlg->Destroy();
        });
        dlg->on_show();
        dlg->Raise();
        return;
    }

    //cj_4
    auto* dlg = new SecondaryCheckDialog(dlg_parent, wxID_ANY, _L("Delete file"),
        SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_CANCEL, wxDefaultPosition, wxDefaultSize, wxCLOSE_BOX | wxCAPTION,
        false, true);
    dlg->m_button_ok->SetLabel(_L("Delete"));
    dlg->m_button_cancel->SetLabel(_L("Cancel"));
    dlg->update_text(wxString::Format(_L("Are you sure you want to delete this file?\n\n%s"), storage_path));
    dlg->set_message_area_width(600);
    dlg->m_button_ok->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this, storage_path, dlg](wxCommandEvent&) {
        wxGetApp().CallAfter([this, storage_path, dlg]() {
            run_delete_printer_file_task(storage_path, false);
            dlg->Destroy();
        });
        dlg->on_hide();
    });
    dlg->m_button_cancel->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [dlg](wxCommandEvent&) {
        dlg->on_hide();
        dlg->Destroy();
    });
    dlg->on_show();
    dlg->Raise();
}

//cj_4
void QDSPrinterWebView::run_delete_printer_file_task(const wxString& storage_path, bool also_remove_local_copy)
{
    if (m_task_dispatcher == nullptr || t_status_page == nullptr)
        return;

    PrinterTask task;
    task.type      = PrinterTaskType::DeletePrinterFiles;
    task.device_id = m_cur_deviceId;

    if (wxGetApp().app_config->get_bool("last_sel_machine_is_net")) {
#if QDT_RELEASE_TO_PUBLIC
        task.transport = PrinterTaskTransport::Cloud;
        task.file_paths.push_back("gcodes/" + std::string(storage_path.ToUTF8()));
        PrinterTaskResult result = m_task_dispatcher->dispatch(task, m_env, PRINTERTYPE);
        emitTaskDispatchResult(task.type, result);
        if (result.success && result.code == PrinterTaskErrorCode::None) {
            if (also_remove_local_copy)
                (void)remove_local_download_for_storage_path(storage_path);
            RemoveStatusModelFileRow(storage_path);
        } else {
            const wxString msg = result.message.empty() ? _L("Delete failed.") : wxString::FromUTF8(result.message);
            show_printer_webview_download_notice(
                GetStatusDialogParent(), _L("Delete failed"), msg);
        }
#else
        boost::ignore_unused(storage_path);
        boost::ignore_unused(also_remove_local_copy);
#endif
    } else {
        task.transport = PrinterTaskTransport::Local;
        //cj_4
        // Moonraker paths are UTF-8; use ToUTF8() like the cloud branch. decode_path(storage_path.data())
        // mis-treats wxString as ACP and corrupts CJK (mojibake like "æµ‹è¯•" for UTF-8 Chinese).
        task.file_paths.push_back("gcodes/" + std::string(storage_path.ToUTF8()));
        PrinterTaskResult result = m_task_dispatcher->dispatch(task);
        emitTaskDispatchResult(task.type, result);
        if (result.success && result.code == PrinterTaskErrorCode::None) {
            if (also_remove_local_copy)
                (void)remove_local_download_for_storage_path(storage_path);
            RemoveStatusModelFileRow(storage_path);
        } else {
            const wxString msg = result.message.empty()
                ? _L("Could not send delete command to the printer. Check the connection and try again.")
                : wxString::FromUTF8(result.message);
            show_printer_webview_download_notice(
                GetStatusDialogParent(), _L("Delete failed"), msg);
        }
    }
}

//cj_4
void QDSPrinterWebView::revealDownloadedPrinterFile(const wxString& storage_path)
{
    std::string downloadPath = wxGetApp().app_config->get("download_path");
    if (downloadPath.empty()) {
        show_printer_webview_download_notice(
            GetStatusDialogParent(),
            _L("Open Folder"),
            _L("Please set the download path in Preferences first."));
        return;
    }
    if (!local_download_target_exists(downloadPath, storage_path)) {
        show_printer_webview_download_notice(
            GetStatusDialogParent(),
            _L("Open Folder"),
            _L("The file is not present in the download folder yet."));
        return;
    }

    wxString rel(storage_path);
    rel.Replace("\\", "/");
    while (!rel.empty() && rel[0] == '/')
        rel = rel.Mid(1);
    wxFileName root(wxString::FromUTF8(downloadPath), wxEmptyString);
    root.MakeAbsolute();
    wxString fullPath = root.GetPathWithSep() + rel;
    wxFileName target(fullPath);
    target.Normalize();
    if (!target.FileExists()) {
        show_printer_webview_download_notice(
            GetStatusDialogParent(),
            _L("Open Folder"),
            _L("Could not find the file on disk."));
        return;
    }

#ifdef __WXMSW__
    wxString native = target.GetFullPath();
    native.Replace("/", "\\");
    wxString explorer = wxString::Format("explorer.exe /select,\"%s\"", native);
    wxExecute(explorer, wxEXEC_ASYNC);
#elif defined(__WXOSX__)
    wxExecute(wxString::Format("open -R \"%s\"", target.GetFullPath()), wxEXEC_ASYNC);
#else
    wxLaunchDefaultApplication(target.GetPath());
#endif
}

//cj_3
void QDSPrinterWebView::run_delete_timelapse_files(const std::vector<TimelapseFileItem*>& items, bool also_remove_local_copy)
{
    if (m_task_dispatcher == nullptr || t_status_page == nullptr || items.empty())
        return;

    PrinterTask task;
    task.type      = PrinterTaskType::DeletePrinterFiles;
    task.device_id = m_cur_deviceId;
    for (TimelapseFileItem* item : items) {
        task.file_paths.push_back("timelapse/" + std::string(item->GetName().ToUTF8()));
    }

    if (wxGetApp().app_config->get_bool("last_sel_machine_is_net")) {
#if QDT_RELEASE_TO_PUBLIC
        task.transport = PrinterTaskTransport::Cloud;
        PrinterTaskResult result = m_task_dispatcher->dispatch(task, m_env, PRINTERTYPE);
        emitTaskDispatchResult(task.type, result);
        if (result.success && result.code == PrinterTaskErrorCode::None) {
            if (also_remove_local_copy) {
                for (TimelapseFileItem* item : items)
                    (void)remove_local_download_for_storage_path(item->GetName());
            }
            RemoveStatusTimelapseRows(items);
        } else {
            const wxString msg = result.message.empty() ? _L("Delete failed.") : wxString::FromUTF8(result.message);
            show_printer_webview_download_notice(GetStatusDialogParent(), _L("Delete failed"), msg);
        }
#else
        boost::ignore_unused(also_remove_local_copy);
#endif
    } else {
        task.transport = PrinterTaskTransport::Local;
        PrinterTaskResult result = m_task_dispatcher->dispatch(task);
        emitTaskDispatchResult(task.type, result);
        if (result.success && result.code == PrinterTaskErrorCode::None) {
            if (also_remove_local_copy) {
                for (TimelapseFileItem* item : items)
                    (void)remove_local_download_for_storage_path(item->GetName());
            }
            RemoveStatusTimelapseRows(items);
        } else {
            const wxString msg = result.message.empty()
                ? _L("Could not send delete command to the printer. Check the connection and try again.")
                : wxString::FromUTF8(result.message);
            show_printer_webview_download_notice(GetStatusDialogParent(), _L("Delete failed"), msg);
        }
    }
}

//cj_3
void QDSPrinterWebView::onTimelapseDeleteUi(wxCommandEvent& event)
{
    if (!t_status_page || m_task_dispatcher == nullptr)
        return;

    std::vector<TimelapseFileItem*> items;
    const wxString                   hint = event.GetString();
    if (!hint.empty()) {
        TimelapseFileItem* hit = FindStatusTimelapseItem(hint);
        if (!hit)
            return;
        items.push_back(hit);
    } else {
        items = GetSelectedStatusTimelapseItems();
    }
    if (items.empty())
        return;

    std::string                    downloadPath = wxGetApp().app_config->get("download_path");
    bool                           any_local    = false;
    if (!downloadPath.empty()) {
        for (TimelapseFileItem* it : items) {
            if (local_download_target_exists(downloadPath, it->GetName())) {
                any_local = true;
                break;
            }
        }
    }

    wxWindow* dlg_parent = GetStatusDialogParent();

    //cj_3
    wxString confirm_text;
    const size_t selected_count = items.size();
    if (selected_count == 1) {
        confirm_text = wxString::Format(_L("Are you sure you want to delete this file?\n\n%s"), items[0]->GetName());
    } else {
        wxString preview_names;
        const size_t preview_count = std::min<size_t>(3, selected_count);
        for (size_t i = 0; i < preview_count; ++i) {
            preview_names += items[i]->GetName();
            if (i + 1 < preview_count)
                preview_names += "\n";
        }
        if (selected_count > preview_count)
            preview_names += "\n...";
        confirm_text =
            wxString::Format(_L("Are you sure you want to delete %d files?\n\n%s"), static_cast<int>(selected_count), preview_names);
    }

    if (any_local) {
        //cj_3
        auto* dlg = new SecondaryCheckDialog(dlg_parent, wxID_ANY, _L("Delete file"),
            SecondaryCheckDialog::ButtonStyle::DELETE_LOCAL_AND_BOTH_AND_CANCEL_NO_PRINTER_ONLY, wxDefaultPosition,
            wxDefaultSize, wxCLOSE_BOX | wxCAPTION, false, true);
        dlg->m_button_retry->SetLabel(_L("Delete local copy"));
        dlg->m_button_ok->SetLabel(_L("Delete on printer and locally"));
        dlg->m_button_cancel->SetLabel(_L("Cancel"));
        dlg->update_text(confirm_text);
        dlg->set_message_area_width(600);
        dlg->rescale();
        dlg->m_button_retry->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this, items, dlg](wxCommandEvent&) {
            wxGetApp().CallAfter([this, items, dlg]() {
                const std::string dp = wxGetApp().app_config->get("download_path");
                bool              ok = true;
                for (TimelapseFileItem* it : items) {
                    if (local_download_target_exists(dp, it->GetName())) {
                        if (!remove_local_download_for_storage_path(it->GetName()))
                            ok = false;
                    }
                }
                dlg->Destroy();
                if (t_status_page) {
                    if (ok)
                        RefreshStatusTimelapseLocalState();
                    else
                        show_printer_webview_download_notice(
                            GetStatusDialogParent(),
                            _L("Delete local copy"),
                            _L("Could not delete the local file."));
                }
            });
            dlg->on_hide();
        });
        dlg->m_button_ok->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this, items, dlg](wxCommandEvent&) {
            wxGetApp().CallAfter([this, items, dlg]() {
                run_delete_timelapse_files(items, true);
                dlg->Destroy();
            });
            dlg->on_hide();
        });
        dlg->m_button_cancel->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [dlg](wxCommandEvent&) {
            dlg->on_hide();
            dlg->Destroy();
        });
        dlg->on_show();
        dlg->Raise();
        return;
    }

    auto* dlg = new SecondaryCheckDialog(dlg_parent, wxID_ANY, _L("Delete file"),
        SecondaryCheckDialog::ButtonStyle::CONFIRM_AND_CANCEL, wxDefaultPosition, wxDefaultSize, wxCLOSE_BOX | wxCAPTION,
        false, true);
    dlg->m_button_ok->SetLabel(_L("Delete"));
    dlg->m_button_cancel->SetLabel(_L("Cancel"));
    dlg->update_text(confirm_text);
    dlg->set_message_area_width(600);
    dlg->m_button_ok->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this, items, dlg](wxCommandEvent&) {
        wxGetApp().CallAfter([this, items, dlg]() {
            run_delete_timelapse_files(items, false);
            dlg->Destroy();
        });
        dlg->on_hide();
    });
    dlg->m_button_cancel->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [dlg](wxCommandEvent&) {
        dlg->on_hide();
        dlg->Destroy();
    });
    dlg->on_show();
    dlg->Raise();
}

//cj_4
void QDSPrinterWebView::downloadTimelapseOne(wxCommandEvent& event)
{
	const wxString name = event.GetString();
	if (!t_status_page || name.empty())
		return;
	TimelapseFileItem* it = FindStatusTimelapseItem(name);
	if (!it)
		return;
	std::vector<TimelapseFileItem*> one = { it };
	downloadTimelapseItems(one);
}

//y83
bool QDSPrinterWebView::downloadTimelapseFileViaP2P(
    TimelapseFileItem* item,
    const std::shared_ptr<QDSDevice>& device,
    const std::string& downloadPath)
{
#if QDT_RELEASE_TO_PUBLIC

    wxString wxFileName = item->GetName();
    std::string fileName = into_u8(wxFileName);
    std::string localPath = downloadPath + "/" + fileName;
    std::string filePath = "/home/qidi/printer_data/timelapse/" + fileName;

    auto timelapse_dev = wxGetApp().qdsdevmanager->getSelectedDevice();
    std::thread([this, item, wxFileName, localPath, fileName, filePath, timelapse_dev]() {
        // Initialize UI: show download indicator
        CallAfter([item]() { item->beginFileDownload(""); });

        QIDIFileManager qdsfmsg(timelapse_dev);
        if (!P2PManager::instance().isConnected()) {
            return;
        }

        //y84
        P2PManager::FileTransferOptions opt;
        opt.filter_by_transfer_id = true;
        opt.verify_end_size       = true;
        opt.trace_chunks          = true;
        opt.progress = [item](int64_t bytes, int64_t total) {
            if (bytes > 0 && total > 0) {
                float pct = std::min(1.0f, float(double(bytes) / double(total)));
                item->CallAfter([item, pct]() { item->setDownloadProgressFraction(pct); });
            }
        };

        auto result = qdsfmsg.downloadFile(filePath, localPath, opt);
        if (result.ok)
            BOOST_LOG_TRIVIAL(info) << "P2P timelapse download completed: " << fileName;
        else
            BOOST_LOG_TRIVIAL(error) << "P2P timelapse download failed: " << filePath;

        CallAfter([item]() { item->setDownloadProgressFraction(-1.f); });
    }).detach();

    return true;
#else
    return false;
#endif
}

//cj_4
void QDSPrinterWebView::playTimelapseFile(wxCommandEvent& event)
{
	const wxString name = event.GetString();
	std::string downloadPath = wxGetApp().app_config->get("download_path");
	if (downloadPath.empty()) {
		show_printer_webview_download_notice(
			GetStatusDialogParent(),
			_L("Play"),
			_L("Please set the download path in Preferences first."));
		return;
	}
	boost::filesystem::create_directories(boost::filesystem::path(downloadPath));
	if (local_download_target_exists(downloadPath, name)) {
		wxFileName fn(wxString::FromUTF8(downloadPath), name);
		fn.Normalize();
		if (fn.FileExists())
			wxLaunchDefaultApplication(fn.GetFullPath());
		return;
	}
	if (!t_status_page)
		return;
	TimelapseFileItem* item = FindStatusTimelapseItem(name);
	std::shared_ptr<QDSDevice> device = m_device_manager->getDevice(m_cur_deviceId);
	if (!item || !device)
		return;
	const std::string path = downloadPath;
	std::string fileName = name.ToUTF8().data();
	std::string localPath = path + "/" + fileName;
	std::string encodedName = UrlEncodeForFilename(fileName);
	std::string urlStr = device->m_frp_url + "/server/files/timelapse/" + encodedName
		+ "?date=" + std::to_string(wxGetUTCTimeMillis().GetValue());
	const wxString openPath = wxString::FromUTF8(localPath);
	const std::string task_id = DownloadManager::getInstance().downloadFile(
		urlStr,
		localPath,
		fileName,
		[this, item, fileName, openPath](FileDownloadProgress progress) {
			item->setDownloadProgressFraction(-1.f);
			if (progress.state == FileDownloadProgress::State::Completed) {
				wxLaunchDefaultApplication(openPath);
			} else if (progress.state == FileDownloadProgress::State::Failed) {
				show_printer_webview_download_notice(
					GetStatusDialogParent(),
					_L("Download Error"),
					wxString::Format(_L("Download failed: %s\n%s"),
						wxString::FromUTF8(fileName),
						wxString::FromUTF8(progress.error_msg)));
			}
		},
		[item](FileDownloadProgress progress) {
			if (progress.state != FileDownloadProgress::State::Downloading) {
				return;
			}
			float f = progress.progress;
			if (f < 0.f && progress.bytes_total > 0) {
				f = static_cast<float>(static_cast<double>(progress.bytes_downloaded)
					/ static_cast<double>(std::max<int64_t>(progress.bytes_total, 1)));
			}
			if (f < 0.f && progress.bytes_downloaded > 0) {
				f = std::min(0.92f,
					static_cast<float>(progress.bytes_downloaded) / (48.f * 1024.f * 1024.f));
			}
			if (f < 0.f) {
				f = 0.f;
			}
			item->setDownloadProgressFraction(std::min(1.f, f));
		});
	if (!task_id.empty())
		item->beginFileDownload(task_id);
}

//cj_4
void QDSPrinterWebView::revealTimelapseFile(wxCommandEvent& event)
{
	revealDownloadedPrinterFile(event.GetString());
}

//cj_4
void QDSPrinterWebView::downloadTimelapseItems(const std::vector<TimelapseFileItem*>& items)
{
	std::string downloadPath = wxGetApp().app_config->get("download_path");
	if (downloadPath.empty()) {
		show_printer_webview_download_notice(
			GetStatusDialogParent(),
			_L("Download Failed"),
			_L("Please set the download path in Preferences first."));
		return;
	}

	boost::filesystem::create_directories(boost::filesystem::path(downloadPath));

	std::shared_ptr<QDSDevice> device = m_device_manager->getDevice(m_cur_deviceId);
	if (!device)
		return;

	wxString skipped_existing;
	std::vector<TimelapseFileItem*> items_to_fetch;
	items_to_fetch.reserve(items.size());
	for (TimelapseFileItem* item : items) {
		if (local_download_target_exists(downloadPath, item->GetName())) {
			if (!skipped_existing.empty())
				skipped_existing += "\n";
			skipped_existing += item->GetName();
			continue;
		}
		items_to_fetch.push_back(item);
	}

	const std::shared_ptr<QDSDevice> dev = device;
	const std::string path = downloadPath;
	auto run_timelapse_downloads = [this, dev, path, items_to_fetch]() {
		for (TimelapseFileItem* item : items_to_fetch) {
			// Try P2P download first
            if(dev->p2p_enable){
                if (downloadTimelapseFileViaP2P(item, dev, path)) {
                    // P2P download started (async)
                    continue;
                }
            }

			std::string fileName = item->GetName().ToUTF8().data();
			std::string localPath = path + "/" + fileName;
			std::string encodedName = UrlEncodeForFilename(fileName);
			std::string urlStr = dev->m_frp_url
				+ "/server/files/timelapse/"
				+ encodedName
				+ "?date=" + std::to_string(wxGetUTCTimeMillis().GetValue());

			//cj_4
			const std::string task_id = DownloadManager::getInstance().downloadFile(
				urlStr,
				localPath,
				fileName,
				[this, item, fileName](FileDownloadProgress progress) {
					item->setDownloadProgressFraction(-1.f);
					if (progress.state == FileDownloadProgress::State::Completed) {
						BOOST_LOG_TRIVIAL(info) << "Downloaded timelapse: " << fileName;
					}
					else if (progress.state == FileDownloadProgress::State::Failed) {
						show_printer_webview_download_notice(
							GetStatusDialogParent(),
							_L("Download Error"),
							wxString::Format(_L("Download failed: %s\n%s"),
								wxString::FromUTF8(fileName),
								wxString::FromUTF8(progress.error_msg)));
					}
				},
				[item](FileDownloadProgress progress) {
					if (progress.state != FileDownloadProgress::State::Downloading) {
						return;
					}
					float f = progress.progress;
					if (f < 0.f && progress.bytes_total > 0) {
						f = static_cast<float>(static_cast<double>(progress.bytes_downloaded)
							/ static_cast<double>(std::max<int64_t>(progress.bytes_total, 1)));
					}
					if (f < 0.f && progress.bytes_downloaded > 0) {
						f = std::min(0.92f,
							static_cast<float>(progress.bytes_downloaded) / (48.f * 1024.f * 1024.f));
					}
					if (f < 0.f) {
						f = 0.f;
					}
					item->setDownloadProgressFraction(std::min(1.f, f));
				}
			);
			if (!task_id.empty())
				item->beginFileDownload(task_id);
		}
	};

	//cj_4
	if (!skipped_existing.empty()) {
		const wxString msg = wxString::Format(
			_L("The following file(s) already exist in the directory:\n\n%s"),
			skipped_existing);
		wxWindow* dlg_parent = GetStatusDialogParent();
		auto* dlg = new SecondaryCheckDialog(dlg_parent, wxID_ANY, _L("Download"),
			SecondaryCheckDialog::ButtonStyle::ONLY_CONFIRM);
		dlg->m_button_ok->SetLabel(_L("OK"));
		dlg->update_text(msg);
		//cj_4
		dlg->set_message_area_width(600);
		dlg->Bind(EVT_SECONDARY_CHECK_CONFIRM, [dlg, run_timelapse_downloads](wxCommandEvent&) {
			//cj_4
			wxGetApp().CallAfter([dlg, run_timelapse_downloads]() {
				run_timelapse_downloads();
				dlg->Destroy();
			});
		});
		dlg->on_show();
		dlg->Raise();
	} else {
		run_timelapse_downloads();
	}
}

//cj_4
void QDSPrinterWebView::downloadTimelapseFile(wxCommandEvent& event)
{
	boost::ignore_unused(event);
	if (!t_status_page)
		return;
	downloadTimelapseItems(GetSelectedStatusTimelapseItems());
}

void QDSPrinterWebView::OnScroll(wxScrollWinEvent& event)
 {
     UpdateLayout();
     event.Skip();
 }

//y77
 void QDSPrinterWebView::load_disconnect_url()
 {

    //cj_5
    // Preserve the selected device navigation state while the tab temporarily unloads the webview.
    wxString saved_ip = m_ip;
    wxString saved_web = m_web;
    bool saved_is_webview = false;
    bool saved_is_net_url = false;
    wxString saved_printer_view_ip;
    wxString saved_printer_view_url;
    if (wxGetApp().mainframe != nullptr) {
        saved_is_webview = wxGetApp().mainframe->is_webview;
        saved_is_net_url = wxGetApp().mainframe->is_net_url;
        saved_printer_view_ip = wxGetApp().mainframe->printer_view_ip;
        saved_printer_view_url = wxGetApp().mainframe->printer_view_url;
    }

    LoadDisconnectPageOnly();

    //cj_5
    m_ip = saved_ip;
    m_web = saved_web;
    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->is_webview = saved_is_webview;
        wxGetApp().mainframe->is_net_url = saved_is_net_url;
        wxGetApp().mainframe->printer_view_ip = saved_printer_view_ip;
        wxGetApp().mainframe->printer_view_url = saved_printer_view_url;
    }
  }

 void QDSPrinterWebView::load_url(wxString &url)
 {
     //cj_5
     if (LoadLocalUrlOnly(url)) {
         SetConnectionPhase(MonitorConnectionPhase::LocalPrinter);
         UpdateState();
     }
  }
 void QDSPrinterWebView::load_net_url(wxString& url, wxString& ip)
 {
    //cj_5
    if (LoadNetUrlOnly(url, ip)) {
        SetConnectionPhase(MonitorConnectionPhase::CloudPrinter);
        UpdateState();
    }
  }
 void QDSPrinterWebView::UpdateState()
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
 void QDSPrinterWebView::OnClose(wxCloseEvent &evt) { this->Hide(); }
 void QDSPrinterWebView::RunScript(const wxString &javascript)
 {
     // Remember the script we run in any case, so the next time the user opens
     // the "Run Script" dialog box, it is shown there for convenient updating.

     WebView::RunScript(m_browser, javascript);
 }

 void QDSPrinterWebView::FormatUrl(std::string link_url) 
 {
    //cj_5
    wxString url = BuildLocalUrl(link_url);
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

 #if QDT_RELEASE_TO_PUBLIC
void QDSPrinterWebView::startCloudStatusStream()
{
    //y84
    if (!MQTTManager::instance().is_connected())
        wxGetApp().fetch_and_connect_mqtt_license();
    const std::string user_id = wxGetApp().app_config->get("preset_folder");
    if (!user_id.empty())
        MQTTManager::instance().subscribe_user_topic(user_id);

    if (m_mqtt_subscription.active())
        return;
    // y84。
    std::weak_ptr<int> weak_life = m_lifetime;
    m_mqtt_subscription = MQTTManager::instance().subscribe(
        [this, weak_life](const std::string& topic, const std::string& payload) {
            if (weak_life.expired())
                return;
            onMqttMessageHandle(topic, payload);
        });
}

//y84
void QDSPrinterWebView::stopCloudStatusStream()
{
    stopMqttDeviceStream();
    m_mqtt_subscription.unsubscribe();
}

void QDSPrinterWebView::startMqttDeviceStream(const std::string& serial)
{
    if (serial.empty())
        return;

    startCloudStatusStream();

    if (!MQTTManager::instance().is_healthy()) {
        BOOST_LOG_TRIVIAL(info) << "MQTT link unhealthy on selecting maker device, "
                                   "closing stale link and reconnecting with fresh license";
        MQTTManager::instance().disconnect();
        m_mqtt_current_topic.clear();
        wxGetApp().fetch_and_connect_mqtt_license();
        const std::string user_id = wxGetApp().app_config->get("preset_folder");
        if (!user_id.empty())
            MQTTManager::instance().subscribe_user_topic(user_id);
    }

    MQTTManager::instance().focus_device(serial);
}

void QDSPrinterWebView::stopMqttDeviceStream()
{
    //y84 停止设备焦点续期
    MQTTManager::instance().unfocus_device();

    if (m_mqtt_current_topic.empty())
        return;
    MQTTManager::instance().unsubscribe_topic(m_mqtt_current_topic);
    m_mqtt_current_topic.clear();
}

void QDSPrinterWebView::onMqttMessageHandle(const std::string& topic, const std::string& payload)
{
    if (m_isDestroying)
        return;

    std::string event;
    json normalized;
    try {
        json j = json::parse(payload);
        if (j.contains("event") && j["event"].is_string())
            event = j["event"].get<std::string>();

        normalized = j;
        if (j.contains("data") && j["data"].is_object()) {
            json data_obj = j["data"];
            normalized["data"] = data_obj.dump();
        }
    } catch (...) {
        normalized = nullptr;
    }

    if (normalized.is_null())
        onCloudDeviceMessage(event, payload);
    else
        onCloudDeviceMessage(event, normalized.dump());
}
//y84

void QDSPrinterWebView::onCloudDeviceMessage(const std::string& event, const std::string& data)
{
	 //cj_4 Bail out if we're being destroyed — SSE thread may still deliver messages
	 // while ~QDSPrinterWebView() is shutting down connections.
	 if (m_isDestroying) return;

	 //y84
	 std::weak_ptr<int> weak_life = m_lifetime;

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

//y84
        auto dev = m_device_manager->getDevice(device_id);

        if(event == "summary" || event == "jobState"){
            if(msgJson.contains("data")){
                string dataStr = msgJson["data"].get<std::string>();
                json   dataJson;
                dataJson = json::parse(dataStr);

                if (dataJson.contains("online")) {
                    bool is_online_ = dataJson["online"].get<bool>();
                    if (dev) {
                        if (is_online_ != dev->is_online()) {
                            if (is_online_)
                                m_device_manager->updateDeviceStatus(device_id, "online");
                            else
                                m_device_manager->updateDeviceStatus(device_id, "offline");
                        }
                    }
                }

                if(dataJson.contains("workState")){
                    std::string dev_state = dataJson["workState"].get<std::string>();
                    m_device_manager->updateDeviceStatus(device_id, dev_state);
                }
            }
            return;
        }

        if(event == "unbind_from_3dp"){
            //y84
            wxGetApp().CallAfter([this, device_id, weak_life]() {
                if (weak_life.expired() || m_isDestroying)
                    return;
                wxEvtHandler* handler = refresh_button->GetEventHandler();
                if (handler){
                    wxCommandEvent evt(wxEVT_BUTTON, refresh_button->GetId());
                    evt.SetEventObject(refresh_button);
                    handler->ProcessEvent(evt);
                }
            });
        }
//y84

		string dataStr = msgJson["data"].get<std::string>();
		json   dataJson;
		try {
			dataJson = json::parse(dataStr);
		} catch (...) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "mqtt data parse fail: " << dataStr << std::endl;
		}

		json status;

		if (dataJson.contains("method") && dataJson.at("method").is_string() && dataJson.at("method").get<std::string>() == "notify_agent_event" && dataJson.at("params").is_array()) {
			json result = dataJson.at("params").at(0);
            auto device = m_device_manager->getDevice(device_id);
			device->updateErrorDataForNotiry(result);
		}

		if (dataJson.contains("result") && dataJson["result"].contains("status")) {
			status = dataJson["result"]["status"];
		} else if (dataJson.contains("status")) {
			status = dataJson["status"];
		}

		//y84
		if (event == "ack" && dataJson.contains("result") && dataJson["result"].is_object()) {
			const json& cfg = dataJson["result"];
			auto device = m_device_manager->getDevice(device_id);
			if (device) {
				if (cfg.contains("printing.polar_cooler") && cfg["printing.polar_cooler"].is_string()) {
					device->m_enable_polar_cooler = (cfg["printing.polar_cooler"].get<std::string>() == "1");
				}
				if (cfg.contains("nozzle.diameter")) {
					const json& nd = cfg["nozzle.diameter"];
					std::vector<float> nozzle_diameter_temp;
					if (nd.is_string()) {
						try { nozzle_diameter_temp.push_back(std::stof(nd.get<std::string>())); } catch (...) {}
					} else if (nd.is_array()) {
						for (const auto& item : nd) {
							if (item.is_string()) {
								try { nozzle_diameter_temp.push_back(std::stof(item.get<std::string>())); } catch (...) {}
							}
						}
					}
					if (!nozzle_diameter_temp.empty()) {
						device->m_nozzle_diameter = nozzle_diameter_temp;
					}
				}
			}
		}

		//y83
		if (status.empty() || status.is_null()) {
			if (dataJson.contains("jobState") && dataJson.contains("progress")) {
				auto device = m_device_manager->getDevice(device_id);
				if (device)
					device->updatePrinterStatusData(dataJson);
			}
			requestStatusRefresh(device_id);
			return;
		}

		bool should_post = false;
		{
			std::lock_guard<std::mutex> lock(m_cloud_mutex);
			m_cloud_pending_device_id   = device_id;
            if(m_cloud_pending_status_json.empty())
			    m_cloud_pending_status_json = status.dump();
            should_post               = !m_cloud_refresh_pending;
			m_cloud_refresh_pending     = true;
		}
		if (!should_post)
			return;

        //y84
		if (weak_life.expired())
			return;

		CallAfter([this, weak_life]() {
			if (weak_life.expired() || m_isDestroying) return;

			std::string device_id;
			std::string status_json;
			{
				std::lock_guard<std::mutex> lock(m_cloud_mutex);
				if (!m_cloud_refresh_pending)
					return;
				m_cloud_refresh_pending   = false;
                device_id               = m_cloud_pending_device_id;
				status_json             = std::move(m_cloud_pending_status_json);
				m_cloud_pending_device_id.clear();
				m_cloud_pending_status_json.clear();
			}
			if (device_id.empty() || status_json.empty())
				return;

			json status;
			try { status = json::parse(status_json); } catch (...) { return; }

			auto device = m_device_manager->getDevice(device_id);
			if (device == nullptr)
				return;

			std::string oldPrintFileName = device->m_print_filename;
			device->updateByJsonData(status);
			device->last_update = std::chrono::steady_clock::now();

            //y84
			if (oldPrintFileName != device->m_print_filename) {
                //y83
                std::string fileListJson;
                auto dev_sp = std::shared_ptr<QDSDevice>(device);

                if(dev_sp->p2p_enable){

                    //y83
                    QIDIFileManager qdsfmsg(dev_sp);
                    auto reply = qdsfmsg.requestText(R"({"method":"fetch_model_list"})");
                    if (!reply.ok) {
                        BOOST_LOG_TRIVIAL(error) << "QDSDeviceManager: "
                            << (reply.timed_out ? "fetch_model_list timeout"
                                                : "failed to send fetch_model_list");
                    }
                    fileListJson = std::move(reply.text);
                }
                if(!fileListJson.empty()){
                    try {
                        json parsed = json::parse(fileListJson);
                        json arr = parsed.is_array() ? parsed : (parsed.contains("result") ? parsed["result"] : parsed);
                        if (arr.is_array()) {
                            for (const auto &file : arr) {
                                if (file.contains("filename") && file["filename"].is_string()) {
                                    std::string jsonFileName = file["filename"].get<std::string>();
                                    if (jsonFileName == dev_sp->m_print_filename){
                                        if (file.contains("show_filament_weight") && file["show_filament_weight"].is_string())
										    dev_sp->m_filament_weight = file["show_filament_weight"].get<std::string>();

                                        if(file.contains("show_print_time") && file["show_print_time"].is_string())
										    dev_sp->m_print_total_time = file["show_print_time"].get<std::string>();
                                        break;
                                    }
                                }
                            }
                        }
                    } catch (const std::exception &e) {
                        BOOST_LOG_TRIVIAL(error) << "QDSDeviceManager: parse file list failed: " << e.what();
                    }
                }
			}

			//y83 Edge-triggered like the WS path; the coalescing timer filters
			// by the currently selected device on the UI thread.
			if (device->is_update.exchange(false)) {
				requestStatusRefresh(device_id);
			}
		});

	 }
	 catch (...)
	 {

	 }
 }
 #endif

//y74
void QDSPrinterWebView::InitDeviceManager(){
    m_device_manager = wxGetApp().qdsdevmanager;

    //cj_6 thumbnail refresh coalescing timer (created before the callback is registered)
    m_thumb_flush_timer = new wxTimer(this, wxWindow::NewControlId());
    Bind(wxEVT_TIMER, &QDSPrinterWebView::onThumbFlushTimer, this, m_thumb_flush_timer->GetId());

    //y84
    std::weak_ptr<int> weak_life = m_lifetime;

    m_device_manager->setConnectionEventCallback([this, weak_life](const std::string& device_id, std::string new_status){
        if (weak_life.expired())    //cj_4
            return;
        CallAfter([this, device_id, new_status, weak_life](){
            //cj_4 Destroyed between QueueEvent() and dispatch.
            if (weak_life.expired())
                return;
            //cj_2
			if (m_isUpdating) {
				return;
			}
            //cj_4
            {
                std::lock_guard<std::mutex> lock(m_ui_map_mutex);
                auto mode_it = m_device_id_to_expert_mode.find(device_id);
                if (mode_it != m_device_id_to_expert_mode.end() && mode_it->second) {
                    return;
                }
            }
            this->updateDeviceButton(device_id, new_status);
        });
    });


    m_device_manager->setParameterUpdateCallback([this](const std::string& device_id) {
        //cj_6 Coalesce: mark the device dirty; the UI-thread timer performs the
        // actual refresh at a bounded rate instead of one CallAfter per WS message.
        requestStatusRefresh(device_id);
    });

    m_device_manager->setDeleteDeviceIDCallback([this](const std::string& device_id){
		//cj_2
		if (m_isUpdating) {
			return;
		}
        std::lock_guard<std::mutex> lock(m_ui_map_mutex);
        m_device_id_to_button.erase(device_id);
        //cj_4
        m_device_id_to_expert_mode.erase(device_id);
        m_device_id_to_config.erase(device_id);
    });

    //y84
    m_device_manager->setFileInfoUpdateCallback([this, weak_life](const std::string& device_id) {
        //y84
        if (weak_life.expired())
            return;
        CallAfter([this, device_id, weak_life]() {
            if (weak_life.expired())
                return;
            if (t_status_page == nullptr || m_cur_deviceId != device_id)
                return;
            hideLoadingOverlay();
            auto dev = m_device_manager->getDevice(device_id);
            if (dev)
                RefreshStatusFileLists(dev);
        });
    });

    if (t_status_page != nullptr) {
        t_status_page->set_file_list_tab_opened_callback([this, weak_life]() {
            if (weak_life.expired())
                return;
            if (t_status_page == nullptr || m_device_manager == nullptr || m_cur_deviceId.empty())
                return;
            auto dev = m_device_manager->getDevice(m_cur_deviceId);
            if (dev) {
                RefreshStatusFileLists(dev);
                t_status_page->sync_model_file_toolbar_after_list_download_change();
            }
        });
    }

    //y84
    m_device_manager->setFileThumbnailReadyCallback(
        [this, weak_life](const std::string& device_id,
                          bool               is_timelapse,
                          const std::string& file_name,
                          const std::vector<uint8_t>& data) {
            if (weak_life.expired())
                return;
            std::vector<uint8_t> bytes = data;
            {
                std::lock_guard<std::mutex> lock(m_pending_thumbnails_mutex);
                m_pending_thumbnails.push_back({device_id, is_timelapse, file_name, std::move(bytes)});
            }
            if (m_thumb_flush_timer)
                m_thumb_flush_timer->StartOnce(kThumbFlushIntervalMs);
        });

}


void QDSPrinterWebView::initEventToTaskPath()
{
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

void QDSPrinterWebView::bindTaskHandle()
{
    if (t_status_page == nullptr) {
        return;
    }

    t_status_page->Bind(EVTSET_EXTRUESION, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_BACK, &QDSPrinterWebView::onStatusPanelTask, this);
    //cj_4
    t_status_page->Bind(EVTSET_COOLER_SWITCH, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_LEVELING_ENABLE, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_AMS_ENABLE, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_COOLLINGFAN_SPEED, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_CHAMBERFAN_SPEED, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_AUXILIARYFAN_SPEED, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_CASE_LIGHT, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_BEEPER_SWITHC, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_EXTRUDER_TEMPERATURE, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_PRINT_SPEED, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_HEATERBED_TEMPERATURE, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_CHAMBER_TEMPERATURE, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_RETURN_SAFEHOME, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_X_AXIS, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_Y_AXIS, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_Z_AXIS, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_PRINT_CONTROL, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_INSERT_READ, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_BOOT_READ, &QDSPrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_AUTO_FILAMENT, &QDSPrinterWebView::onStatusPanelTask, this);
    
    // cj_4 Bind exclude print object event
    t_status_page->Bind(EVTSET_EXCLUDE_PRINT_OBJECT, &QDSPrinterWebView::onStatusPanelTask, this);
}

// cj_1
void QDSPrinterWebView::HideDeviceButtons(std::vector<DeviceButton*>& buttons)
{
    ShowDeviceButtons(buttons, false);
}

void QDSPrinterWebView::HideAllDeviceButtons()
{
    HideDeviceButtons(m_buttons);
    HideDeviceButtons(m_net_buttons);
}

void QDSPrinterWebView::cancelAllDevButtonSelect()
{
    for (DeviceButton* button : m_buttons) {
        button->SetIsSelected(false);
    }

	for (DeviceButton* button : m_net_buttons) {
		button->SetIsSelected(false);
	}
    
}

// cj_1
void QDSPrinterWebView::clearStatusPanelData()
{
    ResetStatusPanel();
}

//cj_5
void QDSPrinterWebView::ApplyStatusContext(const std::string& device_id, MonitorConnectionPhase phase)
{
    if (phase == MonitorConnectionPhase::Disconnected || device_id.empty()) {
        ResetStatusPanel();
        return;
    }

    ResetStatusPanel();

    //y84
    if (m_device_manager != nullptr) {
        std::shared_ptr<QDSDevice> sel_device = m_device_manager->getDevice(device_id);
        if (sel_device != nullptr)
            RefreshStatusFileLists(sel_device);
    }
}

//cj_5
void QDSPrinterWebView::ResetStatusPanel()
{
    //y84
    m_list_render_device_id.clear();
    m_rendered_model_sig.clear();
    m_rendered_timelapse_sig.clear();

    DownloadManager::getInstance().cancelAllDownloads();

    if (t_status_page == nullptr) {
        resetProgressWatchdogHeartbeat();
        return;
    }

    t_status_page->update_temp_data("_", "_", "_");
	t_status_page->update_temp_target("_", "_", "_");
    t_status_page->pause_camera();
	t_status_page->update_camera_url("");
    t_status_page->set_default();
    t_status_page->update_thumbnail("");

    t_status_page->clear_model_item();
    //cj_5 Clear old device's HMS error data on device switch
    t_status_page->update_hms_data({}, "");
    //cj_4
    resetProgressWatchdogHeartbeat();
}

//cj_5
std::string extractEndNumbers(const std::string& str);

//cj_5
void QDSPrinterWebView::ApplyDeviceDataToStatusPanel(const std::string& device_id, std::shared_ptr<QDSDevice> device)
{
    //y84
    if (m_isDestroying || wxGetApp().is_closing()) {
        return;
    }

    if (t_status_page == nullptr || device == nullptr || !device->is_online()) {
        return;
    }

    //y84
    t_status_page->update_setting_options();

    t_status_page->update_temp_data(
        m_device_manager->getDeviceTempNozzle(device_id),
        m_device_manager->getDeviceTempBed(device_id),
        m_device_manager->getDeviceTempChamber(device_id)
    );

    if (device->m_print_cur_layer != 0 && device->m_print_cur_layer != device->m_print_total_layer) {
        m_last_progress_heartbeat = std::chrono::steady_clock::now();
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
    if (progress > 0 && progress < 1 && duration > 0) {
        totalTime = static_cast<double>(duration) / device->m_print_progress_float;
        remainTime = totalTime - duration;
    }
    std::string layer = _L("Layer: N/A").ToStdString();
    if (device->m_print_total_layer != 0) {
        layer = "Layer: " + std::to_string(device->m_print_cur_layer) + "/" + std::to_string(device->m_print_total_layer);
    }

    t_status_page->update_progress(device->m_print_filename, layer, device->m_print_total_time,
        device->m_filament_weight, remainTime, progress,device->m_print_msg);
    t_status_page->update_print_status(device->m_status);
    t_status_page->update_camera_url(device->m_frp_url + "/webcam/?action=snapshot");
    t_status_page->update_thumbnail(device->m_print_png_url);
    t_status_page->update_light_status(device->m_case_light);
    t_status_page->update_fan_speed(AIR_FUN::FAN_COOLING_0_AIRDOOR, device->m_cooling_fan_speed * 10.0);
    t_status_page->update_fan_speed(AIR_FUN::FAN_REMOTE_COOLING_0_IDX, device->m_auxiliary_fan_speed * 10.0);
    t_status_page->update_fan_speed(AIR_FUN::FAN_CHAMBER_0_IDX, device->m_chamber_fan_speed * 10.0);
    if (device->m_polar_cooler_dirty_for_ui.exchange(false)) {
        t_status_page->update_polar_cooler(device->m_polar_cooler.load());
    }
    t_status_page->update_print_speed_display_for_qds(device->m_print_speed_display_percent);
    t_status_page->update_homed_axes(device->m_home_axes);
    t_status_page->update_extruder_filament(device->m_extruder_filament);
    //y84
    t_status_page->update_camera_state(device);

    if (device->box_is_update) {
        // y85: coalesce rapid box-data renders. During initial box-info loading
        // the printer pushes a burst of box/AMS status; rebuilding the whole
        // AMS control + filament combos synchronously on the UI thread for every
        // change would freeze the UI until the stream settles. Throttle the
        // heavy render to once per kBoxRenderDebounceMs; the final state is
        // always honoured because box_is_update stays true until consumed.
        auto now = std::chrono::steady_clock::now();
        if ((now - m_last_box_render_time) < std::chrono::milliseconds(kBoxRenderDebounceMs)) {
            // still streaming: defer the heavy rebuild to the next timer tick
        } else {
        std::vector<Slic3r::GUI::Caninfo> cans(17);
        for (int i = 0; i < 17; ++i) {
            cans[i].can_id = std::to_string(i);
            if (device->m_boxData[i].hasMaterial) {
                cans[i].material_colour = wxColour(device->m_boxData[i].colorHexCode);
                cans[i].material_name = device->m_boxData[i].name;
                cans[i].material_state = AMSCanType::AMS_CAN_TYPE_VIRTUAL;
                cans[i].ctype = 2;  // Single Color
                cans[i].filament_id = device->m_boxData[i].filament_id;
            }
            else {
                cans[i].material_colour = *wxWHITE;
                cans[i].material_state = AMSCanType::AMS_CAN_TYPE_EMPTY;
            }
        }

        Slic3r::GUI::AMSinfo amsExt;
        amsExt.ams_id = "11";
        amsExt.ams_type = DevAmsType::EXT_SPOOL;
        amsExt.current_step = AMSPassRoadSTEP::AMS_ROAD_STEP_NONE;
        amsExt.cans = std::vector<Slic3r::GUI::Caninfo>{ cans[16] };
        std::vector<AMSinfo> ext_info{ amsExt };

        std::vector<AMSinfo> boxS;
        for (int i = 0; i < device->m_box_count; ++i) {
            AMSinfo ams;
            ams.ams_id = std::to_string(i);
            ams.ams_humidity_percent = device->m_boxHumidity[i];
            ams.current_temperature = device->m_boxTemperature[i];
            ams.ams_type = DevAmsType::N3F;
            ams.current_step = AMSPassRoadSTEP::AMS_ROAD_STEP_NONE;
            // QDS gen-2 box marker: drives the Unload-button-hide rule in AMSControl.
            ams.identity = device->m_box_identity;
            for (int j = i * 4; j < (i + 1) * 4; ++j) {
                Slic3r::GUI::Caninfo local_can = cans[j];
                local_can.can_id = std::to_string(j - i * 4);
                ams.cans.push_back(local_can);
            }
            boxS.push_back(ams);
        }

        t_status_page->update_boxs(boxS, ext_info);
        if (device->m_is_init_filamentConfig) {
            t_status_page->set_filament_config(device->m_filamentConfig);
        }

        std::string slotNumSyncStr = extractEndNumbers(device->m_cur_slot);
        if (slotNumSyncStr != "") {
            t_status_page->update_cur_slot(std::stoi(slotNumSyncStr));
        }
        t_status_page->update_AMSSettingData(device->m_auto_read_rfid, device->m_init_detect, device->m_auto_reload_detect);

        if (device->m_is_init_filamentConfig) {
            PresetBundle* preset_bundle = wxGetApp().preset_bundle;
            if (preset_bundle) {
                std::string cur_preset_name = wxGetApp().get_tab(Preset::TYPE_PRINTER)->get_presets()->get_edited_preset().name;
                if (cur_preset_name.find(device->m_type) != std::string::npos) {
                    wxGetApp().qdsdevmanager->upBoxInfoToBoxMsg(device);
                }
            }
        }

        device->box_is_update = false;
            m_last_box_render_sig = device->m_box_signature;
            m_last_box_render_time = now;
        }
    }

    if (device->m_is_update_box_temp) {
        for (int i = 0; i < device->m_boxTemperature.size(); ++i) {
            t_status_page->update_AMS_temp(i, device->m_boxTemperature[i]);
        }
        for (int i = 0; i < device->m_boxHumidity.size(); ++i) {
            t_status_page->update_AMS_humidity(i, device->m_boxHumidity[i]);
        }
        device->m_is_update_box_temp = false;
    }
    RefreshStatusFileLists(device);


    //cj_5 Update HMS error data for the notification button.
    // Always pass current error data — update_hms_data handles device change internally.
    if (device->m_needUpdateErrorData) {
        // m_errorData 由 websocket 线程写入（push/erase/sort），UI 线程读取/删除，需加锁保护
        std::lock_guard<std::mutex> lock(device->m_errorData_mtx);
        t_status_page->update_hms_data(device->m_errorData, device->m_id);
        device->m_needUpdateErrorData = false;

        wxWindow* panel = wxGetApp().mainframe->m_tabpanel->GetCurrentPage();
        bool isCurPanel = (panel == this);
        if (device->m_errorData.size() > 0 && isCurPanel) {
            if (m_device_error_dlg) {
                m_device_error_dlg->Destroy();
                m_device_error_dlg = nullptr;
            }

            const auto& err = device->m_errorData[device->m_errorData.size() - 1];
            if(err.error_type >= 2)
                t_status_page->update_error_message(err.error_message);

            for (int i = static_cast<int>(device->m_errorData.size()) - 1; i >= 0; --i) {
                auto& err = device->m_errorData[i];
                if (!err.error_popup) {
                    continue;
                }
                m_device_error_dlg = new DeviceErrorDialog(nullptr, this);
                m_device_error_dlg->set_show_msg(from_u8(err.error_message));
                m_device_error_dlg->show_error(from_u8(err.error_code));
                break;
            }
        }
    }

    
}

//cj_5
void QDSPrinterWebView::RefreshStatusFileLists(const std::shared_ptr<QDSDevice>& device)
{
    if (t_status_page == nullptr || device == nullptr) {
        return;
    }

    //y84
    const bool device_changed = (m_list_render_device_id != device->m_id);
    if (device_changed) {
        m_rendered_model_sig.clear();
        m_rendered_timelapse_sig.clear();
        m_list_render_device_id = device->m_id;
    }

    const bool model_visible     = t_status_page->is_model_list_visible();
    const bool timelapse_visible = t_status_page->is_timelapse_list_visible();

    bool model_rebuilt = false;

    if (device->m_file_info_load_failed && model_visible) {
        device->m_file_info_load_failed = false;
        if (!device->has_model_files_loaded()) {
            t_status_page->clear_model_items_only();
            t_status_page->show_model_file_list_error();
            m_rendered_model_sig.clear();
            model_rebuilt = true;
        }
    }
    if (!model_rebuilt && model_visible) {
        const bool has_data = device->has_model_files_loaded();
        const std::string model_sig = device->model_list_signature();
        const bool need_rebuild = has_data &&
                                  (device->m_fresh_file_info.load() || device_changed ||
                                   m_rendered_model_sig != model_sig);
        device->m_fresh_file_info = false;

        if (need_rebuild) {
            std::vector<GCodeFileInfo> file_infos = device->snapshotModelFiles();

            t_status_page->clear_model_items_only();
            //cj_5
            // Show model files from heavier to lighter.
            std::stable_sort(file_infos.begin(), file_infos.end(), [](const GCodeFileInfo& lhs, const GCodeFileInfo& rhs) {
                const double lhs_weight = parse_display_weight_grams(lhs.show_filament_weight);
                const double rhs_weight = parse_display_weight_grams(rhs.show_filament_weight);
                if (lhs_weight != rhs_weight) {
                    return lhs_weight > rhs_weight;
                }
                return lhs.file_name < rhs.file_name;
            });
            //cj_5 Freeze layout during batch add to avoid O(n²) sizer recalc.
            t_status_page->freeze_model_list();
            for (const GCodeFileInfo& fileInfo : file_infos) {
                std::vector<uint8_t> cached_thumb;
                if (!fileInfo.plates.empty() && fileInfo.plates[0].thumbnailLoaded)
                    cached_thumb.assign(fileInfo.plates[0].thumbnailData.pixels.begin(),
                                        fileInfo.plates[0].thumbnailData.pixels.end());

                t_status_page->add_model_item(fileInfo.file_name, fileInfo.show_filament_weight, fileInfo.show_print_time,
                    fileInfo.show_thumb_url, fileInfo.thumbnailsSize, fileInfo.file_path, cached_thumb);
            }
            t_status_page->flush_model_batch();
            m_rendered_model_sig = model_sig;
            model_rebuilt        = true;
        }
    }
    auto render_timelapse_from_device = [&]() {
        const std::vector<TimelapseFileInfo> timelapse_infos = device->snapshotTimelapseFiles();

        t_status_page->clear_timelapse_file_list();
        //cj_5
        // Show timelapse files from newest to oldest.
        std::vector<TimelapseFileInfo> sorted = timelapse_infos;
        std::stable_sort(sorted.begin(), sorted.end(), [](const TimelapseFileInfo& lhs, const TimelapseFileInfo& rhs) {
            if (lhs.modified_time != rhs.modified_time) {
                return lhs.modified_time > rhs.modified_time;
            }
            return lhs.file_name < rhs.file_name;
        });
        t_status_page->freeze_timelapse_list();
        for (const auto& info : sorted) {
            t_status_page->add_timelapse_file_item(info);
        }
        t_status_page->flush_timelapse_batch();
    };
    if (device->m_timelapse_info_load_failed && timelapse_visible) {
        device->m_timelapse_info_load_failed = false;
        if (!device->has_timelapse_files_loaded()) {
            t_status_page->clear_timelapse_file_list();
            t_status_page->show_timelapse_file_list_error();
            m_rendered_timelapse_sig.clear();
        }
    }
    if (timelapse_visible) {
        const bool has_data = device->has_timelapse_files_loaded();
        const std::string tl_sig = device->timelapse_list_signature();
        const bool need_rebuild = has_data &&
                                  (device->m_fresh_timelapse_file_info.load() || device_changed ||
                                   m_rendered_timelapse_sig != tl_sig);
        device->m_fresh_timelapse_file_info = false;

        if (need_rebuild) {
            render_timelapse_from_device();
            m_rendered_timelapse_sig = tl_sig;
        } else if (model_rebuilt && has_data) {
            render_timelapse_from_device();
            m_rendered_timelapse_sig = tl_sig;
        }
    }
}

//cj_5
wxWindow* QDSPrinterWebView::GetStatusDialogParent() const
{
    return t_status_page ? t_status_page->GetParent() : nullptr;
}

//cj_5
void QDSPrinterWebView::PrintStatusModelFile(const wxString& storage_path)
{
    if (t_status_page != nullptr) {
        t_status_page->print_model_for_storage_path(storage_path);
    }
}

//cj_5
void QDSPrinterWebView::BeginStatusModelFileDownload(const wxString& storage_path, const std::string& task_id)
{
    if (t_status_page != nullptr) {
        t_status_page->begin_model_file_download_ui(storage_path, task_id);
    }
}

//cj_5
void QDSPrinterWebView::EndStatusModelFileDownload(const wxString& storage_path, bool failed)
{
    if (t_status_page != nullptr) {
        t_status_page->end_model_file_download_ui(storage_path, failed);
    }
}

//cj_5
void QDSPrinterWebView::SetStatusModelFileDownloadProgress(const wxString& storage_path, float progress)
{
    if (t_status_page != nullptr) {
        t_status_page->set_model_file_download_progress(storage_path, progress);
    }
}

//cj_5
void QDSPrinterWebView::RefreshStatusModelFileLocalState()
{
    if (t_status_page != nullptr) {
        t_status_page->refresh_model_file_local_exist_state();
    }
}

//cj_5
void QDSPrinterWebView::RemoveStatusModelFileRow(const wxString& storage_path)
{
    if (t_status_page != nullptr) {
        t_status_page->remove_model_row_by_storage_path(storage_path);
    }
}

//cj_5
TimelapseFileItem* QDSPrinterWebView::FindStatusTimelapseItem(const wxString& name) const
{
    return t_status_page ? t_status_page->find_timelapse_item_by_name(name) : nullptr;
}

//cj_5
std::vector<TimelapseFileItem*> QDSPrinterWebView::GetSelectedStatusTimelapseItems() const
{
    return t_status_page ? t_status_page->getTimelapseSelectItems() : std::vector<TimelapseFileItem*>{};
}

//cj_5
void QDSPrinterWebView::RefreshStatusTimelapseLocalState()
{
    if (t_status_page != nullptr) {
        t_status_page->refresh_timelapse_local_exist_state();
    }
}

//cj_5
void QDSPrinterWebView::RemoveStatusTimelapseRows(const std::vector<TimelapseFileItem*>& items)
{
    if (t_status_page != nullptr) {
        t_status_page->remove_timelapse_file_rows(items);
    }
}

//y76
void QDSPrinterWebView::pauseCamera(){
    t_status_page->pause_camera();
    //cj_4
    m_watchdog_camera_active = false;
}

void QDSPrinterWebView::ShowDeviceButtons(std::vector<DeviceButton*>& buttons, bool isShow /*= true*/)
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


//y74 y84
//cj_4 Local and cloud devices refresh the left button state through m_device_id_to_button.
void QDSPrinterWebView::updateDeviceButton(const std::string& device_id, std::string new_status){
    DeviceButton* t_button = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_ui_map_mutex);
        auto it = m_device_id_to_button.find(device_id);
        if (it != m_device_id_to_button.end()) {
            t_button = it->second;
        }
    }

    if (t_button == nullptr) {
        return;
    }

    std::string t_printer_progress;
    if (new_status == "printing") {
        if (auto dev = m_device_manager->getDevice(device_id)) {
            t_printer_progress = " (" + dev->m_print_progress + "%) ";
        }
    }

    //y84
    if (t_button->GetIsSelected()) {
        wxString status_text = t_button->GetStateText();
        if(new_status == "offline" && status_text != "offline"){
            auto dev = m_device_manager->getDevice(device_id);
            dev->m_is_init_filamentConfig = false;
            clearStatusPanelData();
            wxGetApp().CallAfter([this]() { GUI::wxGetApp().plater()->update_machine_sync_status(); });
        } else if(status_text == "offline" && new_status != "offline"){
            wxEvtHandler* handler = t_button->GetEventHandler();
            if (handler)
            {
                wxCommandEvent evt(wxEVT_BUTTON, t_button->GetId());
                evt.SetEventObject(t_button);
                handler->ProcessEvent(evt);
            }
        }
    }

    t_button->SetStateText(new_status);
    if (!t_printer_progress.empty()) {
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
void QDSPrinterWebView::updateDeviceParameter(const std::string& device_id) {
    //y84
    if (m_isDestroying || wxGetApp().is_closing()) {
        return;
    }

    DeviceButton* t_button = nullptr;
    {
        m_isUpdating = true;
        {
            std::lock_guard<std::mutex> lock(m_ui_map_mutex);
            auto it = m_device_id_to_button.find(device_id);
            if (it != m_device_id_to_button.end()) {
                t_button = it->second;
            }
        }

        if (m_cur_deviceId == device_id) {
            hideLoadingOverlay();

            auto device = m_device_manager->getDevice(device_id);
            if (device == nullptr) {
                m_isUpdating = false;
                return;
            }

            //cj_5
            ApplyDeviceDataToStatusPanel(device_id, device);
        }

        m_isUpdating = false;
    }
}

//y83
// Mark the device as needing a UI refresh. Safe to call from any thread
// (WebSocket thread, SSE callback, UI thread); the UI timer consumes it.
void QDSPrinterWebView::requestStatusRefresh(const std::string& device_id)
{
    std::lock_guard<std::mutex> lock(m_status_refresh_mutex);
    m_status_refresh_pending   = true;
    m_status_refresh_device_id = device_id;
}

//y83
// Bounded-rate coalesced refresh: at most one status refresh per timer tick,
// no matter how many notify_status_update messages arrived in between.
void QDSPrinterWebView::onStatusRefreshTimer(wxTimerEvent& event)
{
    boost::ignore_unused(event);
    //y84
    if (m_isDestroying || wxGetApp().is_closing()) {
        return;
    }

    std::string device_id;
    {
        std::lock_guard<std::mutex> lock(m_status_refresh_mutex);
        if (!m_status_refresh_pending) {
            return;
        }
        m_status_refresh_pending   = false;
        device_id                  = m_status_refresh_device_id;
        m_status_refresh_device_id.clear();
    }

    if (device_id.empty()) {
        return;
    }
    // Only the currently selected device has visible status UI; skip stale
    // pending updates that arrived for other devices.
    if (m_cur_deviceId == device_id) {
        updateDeviceParameter(device_id);
    }
}

//cj_6 Coalesced flush of buffered thumbnail updates: applies all pending thumbnails
// then refreshes each panel ONCE (was one Refresh() per thumbnail -> O(N²) freeze).
void QDSPrinterWebView::onThumbFlushTimer(wxTimerEvent& event)
{
    boost::ignore_unused(event);
    if (t_status_page == nullptr)
        return;

    std::vector<PendingThumb> batch;
    {
        std::lock_guard<std::mutex> lock(m_pending_thumbnails_mutex);
        if (m_pending_thumbnails.empty())
            return;
        batch.swap(m_pending_thumbnails);
    }

    const std::string cur_device = m_cur_deviceId;
    bool applied = false;
    for (const auto& pt : batch) {
        if (pt.device_id != cur_device) {
            BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] skip refresh: buffered device_id=" << pt.device_id
                                     << " != m_cur_deviceId=" << cur_device;
            continue;
        }
        BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] refreshing UI item: device_id=" << cur_device
                                 << " is_timelapse=" << pt.is_timelapse << " name=" << pt.file_name;
        if (pt.is_timelapse)
            t_status_page->refreshTimelapseThumbnailItem(pt.file_name, pt.bytes);
        else
            t_status_page->refreshThumbnailItem(pt.file_name, pt.bytes);
        applied = true;
    }
    //cj_6 single batched refresh for the whole batch (or nothing if all were skipped)
    if (applied)
        t_status_page->flushThumbnailUpdates();
}

//y84
void QDSPrinterWebView::applyLocalSectionExpand(bool expand)
{
    m_localIsExpand = expand;
    if (m_localDeviceExpand != nullptr) {
        m_localDeviceExpand->SetIcon(expand ? "fold" : "unfold");
    }
    for (DeviceButton* button : m_buttons) {
        button->Show(expand);
    }
}

//y84
void QDSPrinterWebView::applyNetSectionExpand(bool expand)
{
    m_netIsExpand = expand;
    if (m_netDeviceExpand != nullptr) {
        m_netDeviceExpand->SetIcon(expand ? "fold" : "unfold");
    }
    for (DeviceButton* button : m_net_buttons) {
        button->Show(expand);
    }
}

//y84
void QDSPrinterWebView::syncDeviceSectionExpandFromLastSelection()
{
    if (m_localDeviceExpand == nullptr && m_netDeviceExpand == nullptr)
        return;

    const bool is_net = wxGetApp().app_config->get_bool("last_sel_machine_is_net");

    if (m_localDeviceExpand != nullptr) {
        applyLocalSectionExpand(!is_net);
    }
    if (m_netDeviceExpand != nullptr) {
        applyNetSectionExpand(is_net);
    }

    leftScrolledWindow->Layout();
}

void QDSPrinterWebView::init_select_machine() {
    std::string last_select_machine = wxGetApp().app_config->get("last_selected_machine");
    if (last_select_machine.empty()) {
        DisconnectTransitionOptions options;
        options.load_placeholder = true;
        TransitionToDisconnected(options);
        syncDeviceSectionExpandFromLastSelection();
        return;
    }

    bool is_net = wxGetApp().app_config->get_bool("last_sel_machine_is_net");

    DeviceButton* selected_button{ nullptr };
    if (is_net) {
        for (DeviceButton* button : m_net_buttons) {
            wxString button_name = button->getIPLabel();
            if (into_u8(button_name) == last_select_machine) {
                selected_button = button;
                break;
            }
        }
    }
    else {
        for (DeviceButton* button : m_buttons) {
            wxString button_name = button->getIPLabel();
            if (into_u8(button_name) == last_select_machine) {
                selected_button = button;
                break;
            }
        }
    }

    if (selected_button) {
        //cj_4
        wxEvtHandler* handler = selected_button->GetEventHandler();

        if (handler)
        {
            wxCommandEvent evt(wxEVT_BUTTON, selected_button->GetId());
            evt.SetEventObject(selected_button);
            handler->ProcessEvent(evt);
        }
    }

    syncDeviceSectionExpandFromLastSelection();
}

} // GUI
} // Slic3r
