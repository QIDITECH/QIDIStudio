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
#include <wx/animate.h>

#include <slic3r/GUI/Widgets/WebView.hpp>

#include "PhysicalPrinterDialog.hpp"
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
#include <unordered_map>
#include <iostream>

#include <iostream>
#include <chrono>

#include "nlohmann/json.hpp"
#include <curl/curl.h>

#include "Tab.hpp"
//cj_2
#include "StatusPanel.hpp"
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
#include <wx/filename.h>

#include "GUI.hpp"
#include "ReleaseNote.hpp"



//cj_2
#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#endif
#include "BaseTransparentDPIFrame.hpp"
namespace pt = boost::property_tree;

namespace {
//cj_4
std::string trim_ws(std::string s)
{
    while (!s.empty() && (static_cast<unsigned char>(s.back()) <= ' '))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && static_cast<unsigned char>(s[i]) <= ' ')
        ++i;
    return s.substr(i);
}

std::string normalize_net_legacy_poll_base(const std::string& link_url)
{
    std::string s = trim_ws(link_url);
    if (s.empty()) {
        return {};
    }
    if (s.size() >= 7 && s.compare(0, 7, "http://") == 0) {
        return s;
    }
    if (s.size() >= 8 && s.compare(0, 8, "https://") == 0) {
        return s;
    }
    if (s.find("://") != std::string::npos) {
        return s;
    }
    return std::string("http://") + s;
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
} // namespace

namespace Slic3r {
    namespace GUI {

wxDEFINE_EVENT(EVT_PRINTER_TASK_RESULT, wxCommandEvent);

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
    //cj_4
    startLegacyStatusPolling();
    m_task_dispatcher = std::make_unique<PrinterTaskDispatcher>(m_device_manager);
    //cj_4
    m_progress_watchdog_timer = new wxTimer(this, wxWindow::NewControlId());
    Bind(wxEVT_TIMER, &PrinterWebView::onProgressWatchdogTimer, this, m_progress_watchdog_timer->GetId());
    resetProgressWatchdogHeartbeat();
    m_progress_watchdog_timer->Start(1000);

    initEventToTaskPath();


    // B45
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterWebView::OnScriptMessage, this);
    Bind(EVT_PRINTER_TASK_RESULT, &PrinterWebView::onTaskDispatchResult, this);

    Bind(wxEVT_CLOSE_WINDOW, &PrinterWebView::OnClose, this);

    bool m_isloginin = (wxGetApp().app_config->get("user_token") != "");
    SetLoginStatus(m_isloginin);

    // cj_1
	t_status_panel->Bind(EVT_SET_COLOR, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_TYPE, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_VENDOR, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_LOAD, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVTSET_FILAMENT_UNLOAD, &PrinterWebView::onSetBoxTask, this);
    //cj_4
    t_status_panel->Bind(EVTSET_FILAMENT_EJECT, &PrinterWebView::onSetBoxTask, this);
	t_status_panel->Bind(EVT_AMS_REFRESH_RFID, &PrinterWebView::onRefreshRfid, this);
    //cj_4
    t_status_panel->Bind(EVT_TIMELAPSE_DELETE_UI, &PrinterWebView::onTimelapseDeleteUi, this);
    t_status_panel->Bind(EVT_MODEL_FILE_LIST_COMMAND, &PrinterWebView::onModelFileListCommand, this);
    //cj_4
    t_status_panel->Bind(EVTSET_DOWNLOAD_TIMELAPSE_FILE, &PrinterWebView::downloadTimelapseFile, this);
    //cj_4
    t_status_panel->Bind(EVT_TIMELAPSE_PLAY_FILE, &PrinterWebView::playTimelapseFile, this);
    t_status_panel->Bind(EVT_TIMELAPSE_REVEAL_FILE, &PrinterWebView::revealTimelapseFile, this);
    t_status_panel->Bind(EVT_TIMELAPSE_DOWNLOAD_ONE, &PrinterWebView::downloadTimelapseOne, this);
    bindTaskHandle();

    init_select_machine();
    //cj_4
    m_printer_view_bootstrap = false;
}

//cj_4
void PrinterWebView::showLoadingOverlay()
{


    //cj_3
    if (m_printer_view_bootstrap) {
        return;
    }



    if (!m_loading_overlay) {
        m_loading_overlay = std::make_unique<LoadingOverlayWithGif>(this);
    }
    
    m_loading_overlay->RequestShow();
}

//cj_4
void PrinterWebView::hideLoadingOverlay()
{
    if (!m_loading_overlay) {
        return;
    }
    m_loading_overlay->RequestHide();
}

//cj_4
void PrinterWebView::startLegacyStatusPolling()
{
    stopLegacyStatusPolling();
    m_stop_legacy_status_polling = false;
    m_legacy_status_thread = std::thread([this]() {
        while (!m_stop_legacy_status_polling.load()) {
            std::unordered_map<std::string, DynamicPrintConfig> local_cfg_copy;
            {
                std::lock_guard<std::mutex> lock(m_ui_map_mutex);
                for (const auto& kv : m_device_id_to_config) {
                    local_cfg_copy[kv.first] = kv.second;
                }
            }

            const auto devices_snap = m_device_manager->snapshotDevices();

            for (const auto& entry : devices_snap) {
                if (m_stop_legacy_status_polling.load()) {
                    break;
                }

                const std::string&         device_id = entry.first;
                const std::shared_ptr<QDSDevice>& dev       = entry.second;
                if (!dev) {
                    continue;
                }

                wxString    check_status_msg;
                std::string status = "offline";
                try {
                    std::unique_ptr<PrintHost> host;
                    if (dev->is_net_device) {
                        std::string poll_host;
                        if (dev->m_net_poll_use_frp) {
                            poll_host = dev->m_frp_url;
                        } else {
                            poll_host = normalize_net_legacy_poll_base(dev->m_net_link_url);
                            if (poll_host.empty()) {
                                poll_host = dev->m_frp_url;
                            }
                        }
                        if (poll_host.empty()) {
                            continue;
                        }
                        const std::string local_ip = dev->m_ip.empty() ? std::string("127.0.0.1") : dev->m_ip;
                        host.reset(PrintHost::get_print_host_url(poll_host, local_ip));
                    } else {
                        auto cfg_it = local_cfg_copy.find(device_id);
                        if (cfg_it == local_cfg_copy.end()) {
                            continue;
                        }
                        host.reset(PrintHost::get_print_host(&cfg_it->second));
                    }

                    if (host) {
                        status = host->get_status(check_status_msg);
                    }
                } catch (...) {
                    status = "offline";
                }

                this->CallAfter([this, device_id, status]() {
                    if (m_isUpdating) {
                        return;
                    }
                    updateDeviceButton(device_id, status);
                });
            }

            for (int i = 0; i < 40 && !m_stop_legacy_status_polling.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
    });
}

//cj_4
void PrinterWebView::stopLegacyStatusPolling()
{
    m_stop_legacy_status_polling = true;
    if (m_legacy_status_thread.joinable()) {
        m_legacy_status_thread.join();
    }
}

//cj_4
void PrinterWebView::resetProgressWatchdogHeartbeat()
{
    m_last_progress_heartbeat = std::chrono::steady_clock::now();
    m_last_progress_signature.clear();
    m_watchdog_camera_active = false;
}

//cj_4
void PrinterWebView::onProgressWatchdogTimer(wxTimerEvent& event)
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
        BOOST_LOG_TRIVIAL(info) << "PrinterWebView auto close camera: progress not updated for 5 minutes";
        t_status_page->pause_camera(true);
        m_watchdog_camera_active = false;
        m_last_progress_heartbeat = now;
    }
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
    leftScrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    leftScrolledWindow->SetBackgroundColour(wxColour(255, 255, 255));
    leftScrolledWindow->SetSizer(devicesizer);
    leftScrolledWindow->SetScrollRate(10, 10);
    leftScrolledWindow->EnableScrolling(false, true);
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
                if (m_localIsExpand) {
                    m_localDeviceExpand->SetIcon("unfold");
                    for (DeviceButton* button : m_buttons) {
                        button->Hide();
                    }
                }
                else {
					m_localDeviceExpand->SetIcon("fold");
					for (DeviceButton* button : m_buttons) {
						button->Show();
					}

                    m_netIsExpand = false;
                    if (m_netDeviceExpand != nullptr) {
					    m_netDeviceExpand->SetIcon("unfold");
                    }
					for (DeviceButton* button : m_net_buttons) {
						button->Hide();
					}

                }
                m_localIsExpand = !m_localIsExpand;
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
                AddButton(from_u8(it->get_short_name(full_name)), host, model_id, from_u8(full_name), is_selected,
                    //cj_4_cursor
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
            bool is_link = wxGetApp().is_link_connect();
            if(is_link){
                NetDevicesLabel = new wxStaticText(leftScrolledWindow, wxID_ANY, "QIDI Link");
            } else {
                NetDevicesLabel = new wxStaticText(leftScrolledWindow, wxID_ANY, "QIDI Maker");
            }
            NetDevicesLabel->SetFont(wxFont(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, "Microsoft YaHei"));
            NetDevicesLabel->SetForegroundColour(wxColour(74, 74, 74));

            //cj_2
			wxStaticBitmap* netBitmap = new wxStaticBitmap(leftScrolledWindow, wxID_ANY, create_scaled_bitmap("netList", leftScrolledWindow, 13));
            netBitmap->Hide();
			m_netDeviceExpand = new DeviceButton(leftScrolledWindow, "fold", wxBU_LEFT);
            m_netDeviceExpand->SetBackgroundColor(trans_bg);
            m_netIsExpand = true;
            
            m_netDeviceExpand->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
				if (m_netIsExpand) {
                    m_netDeviceExpand->SetIcon("unfold");
					for (DeviceButton* button : m_net_buttons) {
						button->Hide();
					}

                    
				}
				else {
                    m_netDeviceExpand->SetIcon("fold");
                    removeDeviceButtonMapEntriesForButtons(m_net_buttons);
					for (DeviceButton* button : m_net_buttons) {
						delete button;
					}
                    m_net_buttons.clear();
#if QDT_RELEASE_TO_PUBLIC
                    if (!wxGetApp().is_link_connect()) {

                        MakerHttpHandle::getInstance().get_maker_device_list();

                    }
                    else {
						wxString    msg;
						QIDINetwork m_qidinetwork;
                        m_qidinetwork.get_device_list(msg);
                    }
					m_net_devices = wxGetApp().get_devices();
					for (const auto& device : m_net_devices) {
						AddNetButton(device);
					}
                    
#endif


					for (DeviceButton* button : m_net_buttons) {
						button->Show();
					}

					m_localIsExpand = false;
                    if (m_localDeviceExpand != nullptr) {
                        m_localDeviceExpand->SetIcon("unfold");
                    }
					for (DeviceButton* button : m_buttons) {
						button->Hide();
					}
				}
                m_netIsExpand = !m_netIsExpand;
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
        m_status_book->ChangeSelection(0);
        load_disconnect_url();
    }
    if (status) {
        syncDeviceSectionExpandFromLastSelection();
    }
    UpdateState();
    UpdateLayout();
 }
void PrinterWebView::SetLoginStatus(bool status) {
    //y77
    m_isloginin = false;


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
//                 MakerHttpHandle::getInstance().setSSEHandle([this](const std::string& event, const std::string& data) {
//                     this->onSSEMessageHandle(event, data);
//                     });
            }
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
        if (!wxGetApp().is_link_connect())
        {
            MakerHttpHandle::getInstance().closeSSEClient();
        }
#endif
        //cj_4
        // If the user was viewing an online (link / maker) device when the
        // login session ends, the button stays selected and the browser keeps
        // rendering the now-unauthenticated link page. Detect that case and
        // drop every piece of "last selected online device" state so nothing
        // of the previous session is left visible.
        const bool was_online_selected =
            (webisNetMode == isNetWeb) ||
            wxGetApp().app_config->get_bool("last_sel_machine_is_net");
        if (was_online_selected) {
            cancelAllDevButtonSelect();
            clearStatusPanelData();
            if (m_device_manager) {
                m_device_manager->unSelected();
            }
            m_cur_deviceId.clear();
            m_ip.clear();
            wxGetApp().app_config->set("last_selected_machine", "");
            wxGetApp().app_config->set_bool("last_sel_machine_is_net", false);
            wxGetApp().app_config->set("machine_list_net", "0");

            //cj_4
            // Blank the embedded browser. A link device was rendered into
            // m_browser via FormatNetUrl/load_net_url, so just switching the
            // simplebook page is not enough: the WebView still keeps the old
            // URL alive and a short flash of the previous device page shows
            // up on the next login. Use the WebView helper to force an
            // about:blank navigation and reset m_web so any later load_url /
            // load_net_url call with the same URL is not deduplicated away.
            if (m_browser) {
                WebView::LoadUrl(m_browser, "about:blank");
            }
            m_web.Clear();
            if (m_status_book) {
                m_status_book->ChangeSelection(0);
            }
            if (wxGetApp().mainframe) {
                wxGetApp().mainframe->is_webview     = false;
                wxGetApp().mainframe->is_net_url     = false;
                wxGetApp().mainframe->printer_view_ip  = "";
                wxGetApp().mainframe->printer_view_url = "";
            }
        }

        // y28
        if(webisNetMode == isNetWeb)
            webisNetMode = isDisconnect;
        SetPresetChanged(true);
        UpdateState();
    }
}


PrinterWebView::~PrinterWebView()
{
    //cj_4
    if (m_progress_watchdog_timer) {
        m_progress_watchdog_timer->Stop();
        Unbind(wxEVT_TIMER, &PrinterWebView::onProgressWatchdogTimer, this, m_progress_watchdog_timer->GetId());
        delete m_progress_watchdog_timer;
        m_progress_watchdog_timer = nullptr;
    }

    //cj_3
    stopLegacyStatusPolling();
    if (m_device_manager) {
        m_device_manager->setConnectionEventCallback({});
        m_device_manager->setParameterUpdateCallback({});
        m_device_manager->setDeleteDeviceIDCallback({});
        m_device_manager->setFileInfoUpdateCallback({});
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
}

// // B55
 void PrinterWebView::AddButton(const wxString &    device_name,
                                const wxString &    ip,
                                const wxString &    machine_type,
                                const wxString &    fullname,
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
    std::thread([this, device_name, ip, machine_type, machine_button, expert_mode, apikey]() {

        //y79
        wxString ip_without_colon = ip;
        if (ip_without_colon.Lower().ends_with("7125"))
            ip_without_colon.Remove(ip_without_colon.length() - 5);

        std::string t_device_id = m_device_manager->addDevice(
            into_u8(device_name), 
            into_u8(ip), 
            /* dev_url */ into_u8(ip_without_colon),
            into_u8(machine_type)
        );
        
        if (!t_device_id.empty()) {
            std::lock_guard<std::mutex> lock(m_ui_map_mutex);
            m_device_id_to_button[t_device_id] = machine_button;
            //cj_4
            m_device_id_to_expert_mode[t_device_id] = expert_mode;
            DynamicPrintConfig printer_cfg;
            printer_cfg.set_key_value("print_host", new ConfigOptionString(into_u8(ip)));
            printer_cfg.set_key_value("printhost_apikey", new ConfigOptionString(into_u8(apikey)));
            printer_cfg.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htOctoPrint));
            m_device_id_to_config[t_device_id] = printer_cfg;
        } else {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "Add device failed: " << into_u8(device_name) << std::endl;
        }
    }).detach(); 

    //y76
    machine_button->Bind(wxEVT_BUTTON, [this, ip, machine_button](wxCommandEvent &event) {
		t_status_page->clear_model_item();
#if QDT_RELEASE_TO_PUBLIC
        if (wxGetApp().app_config->get_bool("last_sel_machine_is_net") && !wxGetApp().is_link_connect()) {
            MakerHttpHandle::getInstance().closeSSEClient();
        }
#endif
        clearStatusPanelData();
        cancelAllDevButtonSelect();
        machine_button->SetIsSelected(true);

        bool expert_mode = true;
        for (auto it = m_device_id_to_button.begin(); it != m_device_id_to_button.end(); ++it) {
            if (it->second == machine_button) {
                m_cur_deviceId = it->first;
                //cj_4_cursor
                auto mode_it = m_device_id_to_expert_mode.find(m_cur_deviceId);
                if (mode_it != m_device_id_to_expert_mode.end()) {
                    expert_mode = mode_it->second;
                }
                break;
            }
        }

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
            }

            m_status_book->ChangeSelection(1);
            load_disconnect_url();
            allsizer->Layout();
            if (wxGetApp().mainframe != nullptr) {
                wxGetApp().mainframe->is_webview = false;

            }
            m_ip = ip;
        }
		this->webisNetMode = isLocalWeb;
        wxGetApp().app_config->set("machine_list_net", "0");
        wxGetApp().app_config->set("last_selected_machine", into_u8(machine_button->getIPLabel()));
        wxGetApp().app_config->set_bool("last_sel_machine_is_net", false);
        //cj_4 clear sidebar sync status when switching to a different device
        wxGetApp().plater()->update_machine_sync_status();
    });
    devicesizer->Add(machine_button, 0, wxEXPAND);
    devicesizer->Layout();
    Layout();
    m_buttons.push_back(machine_button);
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

//cj_4
bool PrinterWebView::select_device_by_id(const std::string& device_id)
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
         device_name = from_u8(device.device_name);
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
         device_name = from_u8(device.device_name);
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

     DeviceButton *machine_button = new DeviceButton(leftScrolledWindow, device_name, machine_icon_path, wxBU_LEFT, wxSize(20, 20),

                                                     device_name, device.local_ip);
     machine_button->SetBackgroundColor(mac_btn_bg);
     machine_button->SetForegroundColour(wxColor(119, 119, 119));
     machine_button->SetFont(Label::Body_16);
     //machine_button->SetBorderColor(wxColour(57, 51, 55));
     machine_button->SetCanFocus(false); 
     machine_button->SetCornerRadius(0);

     
     machine_button->Bind(wxEVT_BUTTON, [this, device, machine_button](wxCommandEvent& event) {
        t_status_page->clear_model_item();
        if (machine_button == nullptr) {
            return;
        }

        //cj_4
        //wxGetApp().CallAfter([this]{
			if (!wxGetApp().is_link_connect()) {
				showLoadingOverlay();
			}
           // });
            
         
        

        // cj_1
        cancelAllDevButtonSelect();
        clearStatusPanelData();
        machine_button->SetIsSelected(true);

        if (wxGetApp().is_link_connect()) {
            //cj_4
            hideLoadingOverlay();
            m_status_book->ChangeSelection(0);
            m_device_manager->unSelected();
            allsizer->Layout();
            FormatNetUrl(device.link_url, device.local_ip, device.isSpecialMachine);
        }
        else {
            
			// cj_2
			{
				MakerHttpHandle::getInstance().setSSEHandle([this](const std::string& event, const std::string& data) {
					this->onSSEMessageHandle(event, data);
					});

				//cj_2
				new std::thread([this]() {
					std::vector<NetDevice> netDevices = m_device_manager->getNetDevices();
					for (NetDevice device : netDevices) {
						HttpData httpData;
						json bodyJson;
						bodyJson["serialNumber"] = device.mac_address;
						httpData.body = bodyJson.dump();
						httpData.env = m_env;
						httpData.target = PRINTERTYPE;

						//y78
						httpData.taskPath = "/get/database/config/all";

						bool isSucceed = false;
						std::string resultBody = MakerHttpHandle::getInstance().httpPostTask(httpData, isSucceed);
						if (!isSucceed) {
							BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "http error" << isSucceed << std::endl;
							continue;;
						}

						try {
							json resultJson = json::parse(resultBody);
							//y78
							if (resultJson.contains("data") && resultJson["data"].is_object()) {
								if (resultJson["data"].contains("printing.polar_cooler") && resultJson["data"]["printing.polar_cooler"].is_string()) {
									std::shared_ptr<QDSDevice> tempQdsDev = m_device_manager->getDevice(device.mac_address);
									tempQdsDev->m_enable_polar_cooler = resultJson["data"]["printing.polar_cooler"].get<std::string>() == "1";
								}

								if (resultJson["data"].contains("nozzle.diameter")) {
									std::shared_ptr<QDSDevice> tempQdsDev = m_device_manager->getDevice(device.mac_address);
									tempQdsDev->m_nozzle_diameter.clear();
									std::vector<float> nozzle_diameter_temp;
									if (resultJson["data"]["nozzle.diameter"].is_string()) {
										nozzle_diameter_temp.push_back(std::stof(resultJson["data"]["nozzle.diameter"].get<std::string>()));
										tempQdsDev->m_nozzle_diameter = nozzle_diameter_temp;
									}
									else if (resultJson["data"]["nozzle.diameter"].is_array()) {
										for (const auto& item : resultJson["data"]["nozzle.diameter"]) {
											if (item.is_string()) {
												nozzle_diameter_temp.push_back(std::stof(item.get<std::string>()));
											}
										}
										tempQdsDev->m_nozzle_diameter = nozzle_diameter_temp;
									}
								}
							}
						}
						catch (...) {

						}

					}
					});
			}
            //device.
            m_status_book->ChangeSelection(1);
            load_disconnect_url();
            m_device_manager->setSelected(device.mac_address);
            m_cur_deviceId = device.mac_address;
            allsizer->Layout();
            if (wxGetApp().mainframe != nullptr) {
                wxGetApp().mainframe->is_webview = false;
            }
            m_ip = device.local_ip;
        }

        this->webisNetMode = isNetWeb;
        UpdateState();

        wxGetApp().app_config->set("last_selected_machine", into_u8(machine_button->getIPLabel()));
        wxGetApp().app_config->set_bool("last_sel_machine_is_net", true);
        wxGetApp().app_config->set("machine_list_net", "1");
        //cj_4 clear sidebar sync status when switching to a different device
        wxGetApp().plater()->update_machine_sync_status();

        m_device_manager->getFileInfo(device.mac_address);
		std::shared_ptr<QDSDevice> tempDevice = m_device_manager->getDevice(device.mac_address);
		if (tempDevice !=nullptr) {
            tempDevice->box_is_update = true;
		}

        });

     devicesizer->Add(machine_button, 0, wxEXPAND);
     devicesizer->Layout();
     m_net_buttons.push_back(machine_button);
     {
         std::lock_guard<std::mutex> lock(m_ui_map_mutex);
         m_device_id_to_button[device.mac_address] = machine_button;
     }
 	 auto qdsDevice = std::make_shared<QDSDevice>(device.mac_address, device.device_name, "", "", t_device_type);
     qdsDevice->m_frp_url = device.url;// +"/webcam/?action=snapshot";
     qdsDevice->m_ip = device.local_ip;
     //cj_4 线上旧：link_url；新（isSpecialMachine）：FRP
     qdsDevice->m_net_link_url     = device.link_url;
     qdsDevice->m_net_poll_use_frp = !wxGetApp().is_link_connect();
     qdsDevice->updateFilamentConfig();
     //y78
     qdsDevice->is_net_device = true;
     BOOST_LOG_TRIVIAL(trace) << "addDevice: " << device.mac_address << __FUNCTION__ ;
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
         removeDeviceButtonMapEntriesForButtons(m_net_buttons);
         for (DeviceButton *button : m_net_buttons) {
             delete button;
         }
         m_net_buttons.clear();
     }
 }

//cj_4
void PrinterWebView::removeDeviceButtonMapEntriesForButtons(const std::vector<DeviceButton*>& buttons)
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
    if (m_isloginin) {
        bool is_link = wxGetApp().is_link_connect();
        if(is_link){
            wxString    msg;
            QIDINetwork m_qidinetwork;
            m_qidinetwork.get_device_list(msg);
        }
        else {
           MakerHttpHandle::getInstance().get_maker_device_list();
        }
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

                //cj_4
                // The selected local (fluid) device is removed. Clear all
                // persisted "current monitor target" fields so MainFrame will
                // not restore the deleted URL when switching tabs back.
                m_cur_deviceId.clear();
                m_ip.clear();
                m_web.Clear();
                if (m_device_manager) {
                    m_device_manager->unSelected();
                }
                wxGetApp().app_config->set("last_selected_machine", "");
                wxGetApp().app_config->set_bool("last_sel_machine_is_net", false);
                if (wxGetApp().mainframe) {
                    wxGetApp().mainframe->printer_view_ip  = "";
                    wxGetApp().mainframe->printer_view_url = "";
                    wxGetApp().mainframe->is_net_url       = false;
                }

                webisNetMode = isDisconnect;
                load_disconnect_url();
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
                    if (m_task_dispatcher != nullptr) {
                        PrinterTask task;
                        task.type = PrinterTaskType::UnbindDevice;
                        task.transport = PrinterTaskTransport::Cloud;
                        task.device_id = m_cur_deviceId;
                        PrinterTaskResult result = m_task_dispatcher->dispatch(task, m_env, DEVICE);
                        emitTaskDispatchResult(task.type, result);
                    }
                }

#endif         
                //cj_4
                // The selected online device is removed. Clear all persisted
                // monitor target fields so tab switching cannot reload a stale
                // page for a device that no longer exists.
                m_cur_deviceId.clear();
                m_ip.clear();
                m_web.Clear();
                if (m_device_manager) {
                    m_device_manager->unSelected();
                }
                wxGetApp().app_config->set("last_selected_machine", "");
                wxGetApp().app_config->set_bool("last_sel_machine_is_net", false);
                if (wxGetApp().mainframe) {
                    wxGetApp().mainframe->printer_view_ip  = "";
                    wxGetApp().mainframe->printer_view_url = "";
                    wxGetApp().mainframe->is_net_url       = false;
                }

                webisNetMode = isDisconnect;
                load_disconnect_url();
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
     int    client_w = leftScrolledWindow->GetClientSize().GetWidth();
     if (client_w < 1) {
         client_w = leftScrolledWindow->GetSize().GetWidth();
     }
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

void PrinterWebView::emitTaskDispatchResult(PrinterTaskType task_type, const PrinterTaskResult& result)
{
    wxCommandEvent result_event(EVT_PRINTER_TASK_RESULT);
    result_event.SetInt(static_cast<int>(result.code));
    result_event.SetExtraLong(static_cast<long>(task_type));
    result_event.SetString(wxString::FromUTF8(result.message.c_str()));
    wxPostEvent(this, result_event);
}

void PrinterWebView::onTaskDispatchResult(wxCommandEvent& event)
{
    if (event.GetInt() == static_cast<int>(PrinterTaskErrorCode::None)) {
        return;
    }

    BOOST_LOG_TRIVIAL(error) << "task dispatch failed, taskType="
        << event.GetExtraLong() << ", code=" << event.GetInt()
        << ", msg=" << event.GetString().ToStdString();
}

void PrinterWebView::onStatusPanelTask(wxCommandEvent& event)
{
   if (m_task_dispatcher == nullptr)
        return;

    PrinterTask task;
    task.type = PrinterTaskType::StatusPanel;
    task.device_id = m_cur_deviceId;
    task.event_type = event.GetEventType();
    task.int_value = event.GetInt();
    task.string_key = event.GetString().ToStdString();

    if (webisNetMode == isLocalWeb) {
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
void PrinterWebView::onSetBoxTask(wxCommandEvent& event)
{
    if (m_task_dispatcher == nullptr)
        return;

    PrinterTask task;
    task.type = PrinterTaskType::SetBox;
    task.device_id = m_cur_deviceId;
    task.event_type = event.GetEventType();
    task.slot_index = event.GetInt();

    int index = 1;
    std::shared_ptr<QDSDevice> device = m_device_manager->getDevice(m_cur_deviceId);
    if (device) {
        for (int i = 0; i < device->m_filamentConfig.size(); ++i) {
            if (task.event_type == EVT_SET_COLOR
                && device->m_filamentConfig[i].colorHexCode == event.GetString().ToStdString()) {
                index = i;
                break;
            }
            if (task.event_type == EVTSET_FILAMENT_VENDOR
                && device->m_filamentConfig[i].vendor == event.GetString().ToStdString()) {
                index = i;
                break;
            }
            if (task.event_type == EVTSET_FILAMENT_TYPE
                && device->m_filamentConfig[i].name == event.GetString().ToStdString()) {
                index = i;
                break;
            }
        }
    }
    task.filament_index = index;

    if (webisNetMode == isLocalWeb) {
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


void PrinterWebView::onRefreshRfid(wxCommandEvent& event)
{
    if (m_task_dispatcher == nullptr)
        return;

    long canId = 0;
    event.GetString().ToLong(&canId);

    PrinterTask task;
    task.type = PrinterTaskType::RefreshRfid;
    task.device_id = m_cur_deviceId;
    task.slot_index = static_cast<int>(canId);

    if (webisNetMode == isLocalWeb) {
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
void PrinterWebView::onModelFileListCommand(wxCommandEvent& event)
{
    const auto cmd = static_cast<ModelFileListCommandType>(event.GetInt());
    const wxString path = event.GetString();
    switch (cmd) {
    case ModelFileListCommandType::Print:
        t_status_page->print_model_for_storage_path(path);
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

//cj_4
void PrinterWebView::downloadSinglePrinterFile(const wxString& wx_storage_path)
{
    std::string downloadPath = wxGetApp().app_config->get("download_path");
    if (downloadPath.empty()) {
        show_printer_webview_download_notice(
            t_status_page ? t_status_page->GetParent() : nullptr,
            _L("Download Failed"),
            _L("Please set the download path in Preferences first."));
        return;
    }

    if (local_download_target_exists(downloadPath, wx_storage_path))
        return;

    boost::filesystem::create_directories(boost::filesystem::path(downloadPath));

    std::shared_ptr<QDSDevice> device = m_device_manager->getDevice(m_cur_deviceId);
    if (!device)
        return;

    const std::string fileName  = wx_storage_path.ToUTF8().data();
    std::string       localPath = downloadPath + "/" + fileName;
    std::string       encodedName = UrlEncodeForFilename(fileName);
    std::string       urlStr      = device->m_frp_url + "/server/files/gcodes/" + encodedName + "?date=" +
        std::to_string(wxGetUTCTimeMillis().GetValue());

    const wxString wx_path_copy = wx_storage_path;

    const std::string task_id = DownloadManager::getInstance().downloadFile(
        urlStr,
        localPath,
        fileName,
        [this, wx_path_copy, fileName](FileDownloadProgress progress) {
            if (progress.state == FileDownloadProgress::State::Completed) {
                BOOST_LOG_TRIVIAL(info) << "Downloaded: " << fileName;
                t_status_page->end_model_file_download_ui(wx_path_copy, false);
            } else if (progress.state == FileDownloadProgress::State::Failed) {
                show_printer_webview_download_notice(
                    t_status_page ? t_status_page->GetParent() : nullptr,
                    _L("Download Error"),
                    wxString::Format(_L("Download failed: %s\n%s"),
                        wxString::FromUTF8(fileName),
                        wxString::FromUTF8(progress.error_msg)));
                t_status_page->end_model_file_download_ui(wx_path_copy, true);
            }
            t_status_page->refresh_model_file_local_exist_state();
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
            t_status_page->set_model_file_download_progress(wx_path_copy, std::min(1.f, f));
        });
    if (!task_id.empty())
        t_status_page->begin_model_file_download_ui(wx_path_copy, task_id);
}

//cj_4
void PrinterWebView::deleteSinglePrinterFile(const wxString& storage_path)
{
    if (t_status_page == nullptr)
        return;
    wxWindow* dlg_parent = t_status_page->GetParent();
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
                        t_status_page->refresh_model_file_local_exist_state();
                    else
                        show_printer_webview_download_notice(
                            t_status_page->GetParent(),
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
void PrinterWebView::run_delete_printer_file_task(const wxString& storage_path, bool also_remove_local_copy)
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
            t_status_page->remove_model_row_by_storage_path(storage_path);
        } else {
            const wxString msg = result.message.empty() ? _L("Delete failed.") : wxString::FromUTF8(result.message);
            show_printer_webview_download_notice(
                t_status_page->GetParent(), _L("Delete failed"), msg);
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
            t_status_page->remove_model_row_by_storage_path(storage_path);
        } else {
            const wxString msg = result.message.empty()
                ? _L("Could not send delete command to the printer. Check the connection and try again.")
                : wxString::FromUTF8(result.message);
            show_printer_webview_download_notice(
                t_status_page->GetParent(), _L("Delete failed"), msg);
        }
    }
}

//cj_4
void PrinterWebView::revealDownloadedPrinterFile(const wxString& storage_path)
{
    std::string downloadPath = wxGetApp().app_config->get("download_path");
    if (downloadPath.empty()) {
        show_printer_webview_download_notice(
            t_status_page ? t_status_page->GetParent() : nullptr,
            _L("Open Folder"),
            _L("Please set the download path in Preferences first."));
        return;
    }
    if (!local_download_target_exists(downloadPath, storage_path)) {
        show_printer_webview_download_notice(
            t_status_page ? t_status_page->GetParent() : nullptr,
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
            t_status_page ? t_status_page->GetParent() : nullptr,
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
void PrinterWebView::run_delete_timelapse_files(const std::vector<TimelapseFileItem*>& items, bool also_remove_local_copy)
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
            t_status_page->remove_timelapse_file_rows(items);
        } else {
            const wxString msg = result.message.empty() ? _L("Delete failed.") : wxString::FromUTF8(result.message);
            show_printer_webview_download_notice(t_status_page->GetParent(), _L("Delete failed"), msg);
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
            t_status_page->remove_timelapse_file_rows(items);
        } else {
            const wxString msg = result.message.empty()
                ? _L("Could not send delete command to the printer. Check the connection and try again.")
                : wxString::FromUTF8(result.message);
            show_printer_webview_download_notice(t_status_page->GetParent(), _L("Delete failed"), msg);
        }
    }
}

//cj_3
void PrinterWebView::onTimelapseDeleteUi(wxCommandEvent& event)
{
    if (!t_status_page || m_task_dispatcher == nullptr)
        return;

    std::vector<TimelapseFileItem*> items;
    const wxString                   hint = event.GetString();
    if (!hint.empty()) {
        TimelapseFileItem* hit = t_status_page->find_timelapse_item_by_name(hint);
        if (!hit)
            return;
        items.push_back(hit);
    } else {
        items = t_status_page->getTimelapseSelectItems();
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

    wxWindow* dlg_parent = t_status_page->GetParent();

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
                        t_status_page->refresh_timelapse_local_exist_state();
                    else
                        show_printer_webview_download_notice(
                            t_status_page->GetParent(),
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
void PrinterWebView::downloadTimelapseOne(wxCommandEvent& event)
{
	const wxString name = event.GetString();
	if (!t_status_page || name.empty())
		return;
	TimelapseFileItem* it = t_status_page->find_timelapse_item_by_name(name);
	if (!it)
		return;
	std::vector<TimelapseFileItem*> one = { it };
	downloadTimelapseItems(one);
}

//cj_4
void PrinterWebView::playTimelapseFile(wxCommandEvent& event)
{
	const wxString name = event.GetString();
	std::string downloadPath = wxGetApp().app_config->get("download_path");
	if (downloadPath.empty()) {
		show_printer_webview_download_notice(
			t_status_page ? t_status_page->GetParent() : nullptr,
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
	TimelapseFileItem* item = t_status_page->find_timelapse_item_by_name(name);
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
					t_status_page ? t_status_page->GetParent() : nullptr,
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
void PrinterWebView::revealTimelapseFile(wxCommandEvent& event)
{
	revealDownloadedPrinterFile(event.GetString());
}

//cj_4
void PrinterWebView::downloadTimelapseItems(const std::vector<TimelapseFileItem*>& items)
{
	std::string downloadPath = wxGetApp().app_config->get("download_path");
	if (downloadPath.empty()) {
		show_printer_webview_download_notice(
			t_status_page ? t_status_page->GetParent() : nullptr,
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
							t_status_page ? t_status_page->GetParent() : nullptr,
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
		wxWindow* dlg_parent = t_status_page ? t_status_page->GetParent() : nullptr;
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
void PrinterWebView::downloadTimelapseFile(wxCommandEvent& event)
{
	boost::ignore_unused(event);
	if (!t_status_page)
		return;
	downloadTimelapseItems(t_status_page->getTimelapseSelectItems());
}

void PrinterWebView::OnScroll(wxScrollWinEvent& event)
 {
     UpdateLayout();
     event.Skip();
 }

//y77
 void PrinterWebView::load_disconnect_url()
 {
    wxString strlang = wxGetApp().current_language_code_safe();
    wxString url;
    if (m_isNetMode)
    {

        url = wxString::Format("file://%s/web/qidi/link_missing_connection.html", from_u8(resources_dir()));
        if (strlang != "")
            url = wxString::Format("file://%s/web/qidi/link_missing_connection.html?lang=%s", from_u8(resources_dir()), strlang);
    }
    else
    {

        url = wxString::Format("file://%s/web/qidi/missing_connection.html", from_u8(resources_dir()));
        if (strlang != "")
            url = wxString::Format("file://%s/web/qidi/missing_connection.html?lang=%s", from_u8(resources_dir()), strlang);
    }
    webisNetMode = isDisconnect;
    m_web = url;
    m_ip = "";
    m_browser->LoadURL(m_web);

    if (wxGetApp().mainframe != nullptr) {
        wxGetApp().mainframe->is_webview = true;
        wxGetApp().mainframe->is_net_url = false;
    }
    UpdateState();
 }

 void PrinterWebView::load_url(wxString &url)
 {
     if (m_browser == nullptr)
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
     if (wxGetApp().mainframe != nullptr) {
         wxGetApp().mainframe->is_webview = true;
         wxGetApp().mainframe->is_net_url = false;
         wxGetApp().mainframe->printer_view_ip = m_ip;
         wxGetApp().mainframe->printer_view_url = m_web;
     }
     UpdateState();
 }
 void PrinterWebView::load_net_url(wxString& url, wxString& ip)
 {
     //cj_4 always reload even if the URL is the same
    if (m_browser == nullptr)
        return;
    // y13
    m_web = url;
    m_ip = ip;
    m_browser->LoadURL(m_web);
    //y28
    webisNetMode = isNetWeb;
    //cj_4
    // When switching back to monitor from another tab, MainFrame restores the
    // browser URL through load_net_url(). Keep the left device list selection
    // consistent with that restored URL/IP so the previously selected Link
    // device remains highlighted.
    for (DeviceButton *button : m_buttons) {
        button->SetIsSelected(false);
    }

    for (DeviceButton *button : m_net_buttons) {
        wxString button_ip = button->getIPLabel();
         if (button_ip.Lower().starts_with("http"))
             button_ip.Remove(0, 7);
        if (button_ip.Lower().ends_with("10088"))
            button_ip.Remove(button_ip.length() - 6);
        if (m_ip == button_ip)
            button->SetIsSelected(true);
        else
            button->SetIsSelected(false);
    }
    if (wxGetApp().mainframe != nullptr) {
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
             //cj_4 always reload even for the same fluid URL, skip return
             ;

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
         json dataJson;
         try {
             dataJson = json::parse(msgJson["data"].get<std::string>());
         }
         catch (...){
             BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "sse data parse fail: " << dataStr << std::endl;
         }
		 json status;

		 if (dataJson.contains("result") && dataJson["result"].contains("status")) {
			 status = dataJson["result"]["status"];
		 }
		 else if (dataJson.contains("status")) {
			 status = dataJson["status"];
        //y79
		 } else {
            if(dataJson.contains("jobState") && dataJson.contains("progress")){
                status = dataJson;
                device->updatePrinterStatusData(status);
                return;
            }
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

			 CallAfter([this, device_id]() {
                 if (m_isUpdating){
                     return;
                   }
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
        CallAfter([this, device_id, new_status](){
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
        
        CallAfter([this, device_id]() {
            //y78
            if (m_isUpdating) {
                return;
            }
            this->updateDeviceParameter(device_id);
        });
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

    m_device_manager->setFileInfoUpdateCallback([this](const std::string& device_id) {
        CallAfter([this, device_id]() {
            if (m_isUpdating) {
                return;
            }
            this->updateDeviceParameter(device_id);
        });
    });
}


void PrinterWebView::initEventToTaskPath()
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

void PrinterWebView::bindTaskHandle()
{
    if (t_status_page == nullptr) {
        return;
    }

    t_status_page->Bind(EVTSET_EXTRUESION, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_BACK, &PrinterWebView::onStatusPanelTask, this);
    //cj_4
    t_status_page->Bind(EVTSET_COOLER_SWITCH, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_LEVELING_ENABLE, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_AMS_ENABLE, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_COOLLINGFAN_SPEED, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_CHAMBERFAN_SPEED, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_AUXILIARYFAN_SPEED, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_CASE_LIGHT, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_BEEPER_SWITHC, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_EXTRUDER_TEMPERATURE, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_PRINT_SPEED, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_HEATERBED_TEMPERATURE, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_CHAMBER_TEMPERATURE, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_RETURN_SAFEHOME, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_X_AXIS, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_Y_AXIS, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_Z_AXIS, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_PRINT_CONTROL, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_INSERT_READ, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_BOOT_READ, &PrinterWebView::onStatusPanelTask, this);
    t_status_page->Bind(EVTSET_AUTO_FILAMENT, &PrinterWebView::onStatusPanelTask, this);
    
    // cj_4 Bind exclude print object event
    t_status_page->Bind(EVTSET_EXCLUDE_PRINT_OBJECT, &PrinterWebView::onStatusPanelTask, this);
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
    DownloadManager::getInstance().cancelAllDownloads();

    t_status_page->update_temp_data("_", "_", "_");
	t_status_page->update_temp_target("_", "_", "_");
    t_status_page->pause_camera();
	t_status_page->update_camera_url("");
    t_status_page->set_default();
    t_status_page->update_thumbnail("");

    t_status_page->clear_model_item();
    //cj_4
    resetProgressWatchdogHeartbeat();
}

//y76
void PrinterWebView::pauseCamera(){
    t_status_page->pause_camera();
    //cj_4
    m_watchdog_camera_active = false;
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
//cj_3
void PrinterWebView::updateDeviceButton(const std::string& device_id, std::string new_status){
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
void PrinterWebView::updateDeviceParameter(const std::string& device_id) {
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
            //cj_4
            hideLoadingOverlay();

            t_status_page->update_temp_data(
                m_device_manager->getDeviceTempNozzle(device_id),
                m_device_manager->getDeviceTempBed(device_id),
                m_device_manager->getDeviceTempChamber(device_id)
            );

			auto device = m_device_manager->getDevice(device_id);
            if (device == nullptr) {
                m_isUpdating = false;
                return;
            }
            //cj_4
            if (device->m_print_cur_layer!=0 && device->m_print_cur_layer != device->m_print_total_layer) {
                //m_last_progress_signature = progress_signature;
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
            //cj_4
            if (device->m_polar_cooler_dirty_for_ui.exchange(false)) {
                t_status_page->update_polar_cooler(device->m_polar_cooler.load());
            }
            t_status_page->update_print_speed_display_for_qds(device->m_print_speed_display_percent);

			//cj_1
			t_status_page->update_homed_axes(device->m_home_axes);
			//cj_2
			t_status_page->update_extruder_filament(device->m_extruder_filament);

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
					amsExt.ams_type = DevAmsType::EXT_SPOOL;
					amsExt.current_step = AMSPassRoadSTEP::AMS_ROAD_STEP_NONE;
					amsExt.cans = std::vector<Slic3r::GUI::Caninfo>{ cans[16] };
				}
				std::vector<AMSinfo> ext_info;
				ext_info = std::vector<AMSinfo>{ amsExt };


				std::vector<AMSinfo> boxS;
				for (int i = 0; i < device->m_box_count; ++i) {
					AMSinfo ams;
					ams.ams_id = std::to_string(i);
                    ams.ams_humidity_percent = device->m_boxHumidity[i];
                    ams.current_temperature = device->m_boxTemperature[i];
                    //cj_4
                    // QDS boxes report percentage humidity from aht20_f
                    // heater_box*. Use a percent-capable AMS type so the
                    // humidity widget renders the numeric value instead of
                    // the legacy 1-5 level icon.
					ams.ams_type = DevAmsType::N3F;
					ams.current_step = AMSPassRoadSTEP::AMS_ROAD_STEP_NONE;
					for (int j = i * 4; j < (i + 1) * 4; ++j) {
                        //cj_4
                        // AMS widgets expect slot ids local to a box (0-3).
                        // Device data is indexed globally (0-16), so copy the
                        // global slot data and remap only the UI can_id here.
                        Caninfo local_can = cans[j];
                        local_can.can_id = std::to_string(j - i * 4);
					    ams.cans.push_back(local_can);
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
                
                
                PresetBundle *preset_bundle = wxGetApp().preset_bundle;
                if(preset_bundle){
                    std::string cur_preset_name = wxGetApp().get_tab(Preset::TYPE_PRINTER)->get_presets()->get_edited_preset().name;
                    //y78
                    if(cur_preset_name.find(device->m_type) != std::string::npos){
                        wxGetApp().qdsdevmanager->upBoxInfoToBoxMsg(device);
                    }
                }

                device->box_is_update = false;
            }

            if (device->m_is_update_box_temp){
                for (int i = 0; i < device->m_boxTemperature.size(); ++i) {
                    t_status_page->update_AMS_temp(i, device->m_boxTemperature[i]);
                }
				for (int i = 0; i < device->m_boxHumidity.size(); ++i) {
					t_status_page->update_AMS_humidity(i, device->m_boxHumidity[i]);
				}
                device->m_is_update_box_temp = false;
            }

            if (device->m_fresh_file_info) {
                t_status_page->clear_model_items_only();
                for (GCodeFileInfo fileInfo : device->file_info) {
                    t_status_page->add_model_item(fileInfo.file_name, fileInfo.show_filament_weight, fileInfo.show_print_time, fileInfo.show_thumb_url,fileInfo.thumbnailsSize);
                }
                device->m_fresh_file_info = false;
            }

            //cj_4
            if (device->m_fresh_timelapse_file_info) {
                t_status_page->clear_timelapse_file_list();
                for (const auto& info : device->timelapse_file_info)
                    t_status_page->add_timelapse_file_item(
                        info.file_name, info.file_size, info.modified_time, info.thumb_url);
                device->m_fresh_timelapse_file_info = false;
            }
        }

        m_isUpdating = false;
    }
}

void PrinterWebView::syncDeviceSectionExpandFromLastSelection()
{
    if (m_localDeviceExpand == nullptr || m_netDeviceExpand == nullptr)
        return;

    std::string last_select_machine = wxGetApp().app_config->get("last_selected_machine");
    if (last_select_machine.empty()) {
        m_localIsExpand = true;
        m_localDeviceExpand->SetIcon("fold");
        for (DeviceButton* button : m_buttons)
            button->Show();

        m_netIsExpand = false;
        m_netDeviceExpand->SetIcon("unfold");
        for (DeviceButton* button : m_net_buttons)
            button->Hide();

        leftScrolledWindow->Layout();
        return;
    }

    const bool is_net = wxGetApp().app_config->get_bool("last_sel_machine_is_net");
    if (is_net) {
        m_netIsExpand = true;
        m_netDeviceExpand->SetIcon("fold");
        for (DeviceButton* button : m_net_buttons)
            button->Show();

        m_localIsExpand = false;
        m_localDeviceExpand->SetIcon("unfold");
        for (DeviceButton* button : m_buttons)
            button->Hide();
    } else {
        m_localIsExpand = true;
        m_localDeviceExpand->SetIcon("fold");
        for (DeviceButton* button : m_buttons)
            button->Show();

        m_netIsExpand = false;
        m_netDeviceExpand->SetIcon("unfold");
        for (DeviceButton* button : m_net_buttons)
            button->Hide();
    }

    leftScrolledWindow->Layout();
}

void PrinterWebView::init_select_machine() {
    std::string last_select_machine = wxGetApp().app_config->get("last_selected_machine");
    if (last_select_machine.empty()) {
        load_disconnect_url();
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
