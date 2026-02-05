//
//  wxMediaCtrl3.h
//  libslic3r_gui
//
//  Created by cmguo on 2024/6/22.
//

#ifndef wxMediaCtrl3_h
#define wxMediaCtrl3_h

#include "wx/uri.h"
#include "wx/mediactrl.h"
#include <curl/curl.h>
#include <condition_variable>
#include <thread>

wxDECLARE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent);

void wxMediaCtrl_OnSize(wxWindow * ctrl, wxSize const & videoSize, int width, int height);

#ifdef __WXMAC__

#include "wxMediaCtrl2.h"
#define wxMediaCtrl3 wxMediaCtrl2

#else

#define QIDI_DYNAMIC
#include <condition_variable>
#include <thread>
#ifndef _WIN32
#include <wx/image.h>
#endif
#include "Printer/QIDITunnel.h"

class AVVideoDecoder;

class wxMediaCtrl3 : public wxWindow, QIDILib
{
public:
    wxMediaCtrl3(wxWindow *parent);

    ~wxMediaCtrl3();

    void Load(wxURI url);

    void Play();

    void Stop();

    void SetIdleImage(wxString const & image);

    wxMediaState GetState();

    int GetLastError();

    wxSize GetVideoSize();

protected:
    DECLARE_EVENT_TABLE()

    void paintEvent(wxPaintEvent &evt);

    wxSize DoGetBestSize() const override;

    void DoSetSize(int x, int y, int width, int height, int sizeFlags) override;

    static void qidi_log(void *ctx, int level, tchar const *msg);

    void PlayThread();

    void NotifyStopped();

private:
    wxString m_idle_image;
    wxMediaState m_state  = wxMEDIASTATE_STOPPED;
    int m_error  = 0;
    wxSize m_video_size = wxDefaultSize;
    wxSize m_frame_size = wxDefaultSize;
#ifdef _WIN32
    wxBitmap m_frame;
#else
    wxImage m_frame;
#endif

    std::shared_ptr<wxURI> m_url;
    std::uint64_t m_last_PTS{0};
    std::chrono::system_clock::time_point m_last_PTS_expected;
    std::chrono::system_clock::time_point m_last_PTS_practical;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::thread m_thread;
};

#endif

//y76
class VideoPanel : public wxPanel
{
public:
    VideoPanel(wxWindow* parent, 
               wxWindowID id = wxID_ANY,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxSize(640, 480));
    
    virtual ~VideoPanel();

    void Load(const std::string& url);
    void Play();
    void Stop();
    void SetIdleImage(wxString const &image);
    
    wxMediaState GetState();
    int GetLastError();
    wxSize GetVideoSize();
    
    wxSize DoGetBestSize() const override;
    
protected:
    void OnPaint(wxPaintEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    void OnSize(wxSizeEvent& event);
    
    void paintEvent(wxPaintEvent& evt);
    
    static void adjust_frame_size(wxSize& frame, const wxSize& video, const wxSize& window);
    
private:
    void PlayThread();
    void NotifyStopped();
    void ResetPlaybackState();
    void UpdateFrameStatistics();
    void SetErrorAndNotify(int errorCode, const std::string& errorMsg);
    
    static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp);
    
private:
    std::shared_ptr<std::string> m_url;
    wxMediaState m_state = wxMEDIASTATE_STOPPED;
    int m_error = 0;
    
    wxImage m_idle_image;
    wxImage m_frame;
    wxSize m_video_size = wxDefaultSize;
    wxSize m_frame_size = wxDefaultSize;
    
    int m_frameCount;
    std::chrono::steady_clock::time_point m_lastSecondTime;

    uint64_t m_last_PTS;
    std::chrono::steady_clock::time_point m_last_PTS_expected;
    std::chrono::steady_clock::time_point m_last_PTS_practical;
    
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::thread m_thread;
    std::atomic<bool> m_exit_flag{false};
    
    wxDECLARE_EVENT_TABLE();
};
//y76

#endif /* wxMediaCtrl3_h */
