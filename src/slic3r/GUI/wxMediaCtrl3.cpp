#include "wxMediaCtrl3.h"
#include "AVVideoDecoder.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/log/trivial.hpp>
#include <wx/dcclient.h>
#ifdef __WIN32__
#include <versionhelpers.h>
#include <wx/msw/registry.h>
#include <shellapi.h>
#endif

//wxDEFINE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent);
#ifdef _WIN32
BEGIN_EVENT_TABLE(wxMediaCtrl3, wxWindow)

// catch paint events
EVT_PAINT(wxMediaCtrl3::paintEvent)

END_EVENT_TABLE()

struct StaticQIDILib : QIDILib
{
    static StaticQIDILib &get(QIDILib *);
};

wxMediaCtrl3::wxMediaCtrl3(wxWindow *parent)
    : wxWindow(parent, wxID_ANY)
    , QIDILib(StaticQIDILib::get(this))
    , m_thread([this] { PlayThread(); })
{
    SetBackgroundColour("#000001ff");
}

wxMediaCtrl3::~wxMediaCtrl3()
{
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_url.reset(new wxURI);
        m_frame = wxImage(m_idle_image);
        m_cond.notify_all();
    }
    m_thread.join();
}

void wxMediaCtrl3::Load(wxURI url)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    m_video_size = wxDefaultSize;
    m_error = 0;
    m_url.reset(new wxURI(url));
    m_cond.notify_all();
}

void wxMediaCtrl3::Play()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    if (m_state != wxMEDIASTATE_PLAYING) {
        m_state = wxMEDIASTATE_PLAYING;
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
    }
}

void wxMediaCtrl3::Stop()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    m_url.reset();
    m_frame = wxImage(m_idle_image);
    NotifyStopped();
    m_cond.notify_all();
    Refresh();
}

void wxMediaCtrl3::SetIdleImage(wxString const &image)
{
    if (m_idle_image == image)
        return;
    m_idle_image = image;
    if (m_url == nullptr) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_frame = wxImage(m_idle_image);
        assert(m_frame.IsOk());
        Refresh();
    }
}

wxMediaState wxMediaCtrl3::GetState()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_state;
}

int wxMediaCtrl3::GetLastError()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_error;
}

wxSize wxMediaCtrl3::GetVideoSize()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_video_size;
}

wxSize wxMediaCtrl3::DoGetBestSize() const
{
    return {-1, -1};
}

static void adjust_frame_size(wxSize & frame, wxSize const & video, wxSize const & window)
{
    if (video.x * window.y < video.y * window.x)
        frame = { video.x * window.y / video.y, window.y };
    else
        frame = { window.x, video.y * window.x / video.x };
}

void wxMediaCtrl3::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    auto      size = GetSize();
    if (size.x <= 0 || size.y <= 0)
        return;
    std::unique_lock<std::mutex> lk(m_mutex);
    if (!m_frame.IsOk())
        return;
    auto size2 = m_frame.GetSize();
    if (size2.x != m_frame_size.x && size2.y == m_frame_size.y)
        size2.x = m_frame_size.x;
    auto size3 = (size - size2) / 2;
    if (size2.x != size.x && size2.y != size.y) {
        double scale = 1.;
        if (size.x * size2.y > size.y * size2.x) {
            size3 = {size.x * size2.y / size.y, size2.y};
            scale = double(size.y) / size2.y;
        } else {
            size3 = {size2.x, size.y * size2.x / size.x};
            scale = double(size.x) / size2.x;
        }
        dc.SetUserScale(scale, scale);
        size3 = (size3 - size2) / 2;
    }
    dc.DrawBitmap(m_frame, size3.x, size3.y);
}

void wxMediaCtrl3::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags == wxSIZE_USE_EXISTING) return;
    wxMediaCtrl_OnSize(this, m_video_size, width, height);
    std::unique_lock<std::mutex> lk(m_mutex);
    adjust_frame_size(m_frame_size, m_video_size, GetSize());
    Refresh();
}

void wxMediaCtrl3::qidi_log(void *ctx, int level, tchar const *msg2)
{
#ifdef _WIN32
    wxString msg(msg2);
#else
    wxString msg = wxString::FromUTF8(msg2);
#endif
    if (level == 1) {
        if (msg.EndsWith("]")) {
            int n = msg.find_last_of('[');
            if (n != wxString::npos) {
                long val = 0;
                wxMediaCtrl3 *ctrl = (wxMediaCtrl3 *) ctx;
                if (msg.SubString(n + 1, msg.Length() - 2).ToLong(&val)) {
                    std::unique_lock<std::mutex> lk(ctrl->m_mutex);
                    ctrl->m_error = (int) val;
                }
            }
        } else if (msg.Contains("stat_log")) {
            wxCommandEvent evt(EVT_MEDIA_CTRL_STAT);
            wxMediaCtrl3 *ctrl = (wxMediaCtrl3 *) ctx;
            evt.SetEventObject(ctrl);
            evt.SetString(msg.Mid(msg.Find(' ') + 1));
            wxPostEvent(ctrl, evt);
        }
    }
    BOOST_LOG_TRIVIAL(info) << msg.ToUTF8().data();
}

void wxMediaCtrl3::PlayThread()
{
    using namespace std::chrono_literals;
    std::shared_ptr<wxURI> url;
    std::unique_lock<std::mutex> lk(m_mutex);

    //frame count
    int                                                frameCount = 0;
    std::chrono::time_point<std::chrono::system_clock> lastSecondTime;

    while (true) {
        m_cond.wait(lk, [this, &url] { return m_url != url; });
        url = m_url;
        if (url == nullptr)
            continue;
        if (!url->HasScheme())
            break;


        //reset frame
        frameCount     = 0;
        lastSecondTime = std::chrono::system_clock::now();

        lk.unlock();
        QIDI_Tunnel tunnel = nullptr;
        int error = QIDI_Create(&tunnel, m_url->BuildURI().ToUTF8());
        if (error == 0) {
            QIDI_SetLogger(tunnel, &wxMediaCtrl3::qidi_log, this);
            error = QIDI_Open(tunnel);
            if (error == 0)
                error = QIDI_would_block;

            else if (error == -2)
            {
                m_error = error;
                BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::DLL load error ";
                lk.lock();
                NotifyStopped();
                continue;
            }
        }
        lk.lock();
        while (error == int(QIDI_would_block)) {
            m_cond.wait_for(lk, 100ms);
            if (m_url != url) {
                error = 1;
                break;
            }
            lk.unlock();
            error = QIDI_StartStream(tunnel, true);
            lk.lock();
        }
        QIDI_StreamInfo info;
        if (error == 0)
            error = QIDI_GetStreamInfo(tunnel, 0, &info);
        AVVideoDecoder decoder;
        int minFrameDuration = 0;
        if (error == 0) {
            decoder.open(info);
            m_video_size = { info.format.video.width, info.format.video.height };
            adjust_frame_size(m_frame_size, m_video_size, GetSize());
            minFrameDuration = 800 / info.format.video.frame_rate; // 80%
            NotifyStopped();
        }
        QIDI_Sample sample;
        while (error == 0) {
            lk.unlock();
            error = QIDI_ReadSample(tunnel, &sample);
            lk.lock();
            while (error == int(QIDI_would_block)) {
                m_cond.wait_for(lk, 100ms);
                if (m_url != url) {
                    error = 1;
                    break;
                }
                lk.unlock();
                error = QIDI_ReadSample(tunnel, &sample);
                lk.lock();
            }
            if (error == 0) {
                auto frame_size = m_frame_size;
                lk.unlock();
                decoder.decode(sample);
#ifdef _WIN32
                wxBitmap bm;
                decoder.toWxBitmap(bm, frame_size);
#else
                wxImage bm;
                decoder.toWxImage(bm, frame_size);
#endif
                lk.lock();
                if (m_url != url) {
                    error = 1;
                    break;
                }
                if (bm.IsOk()) {
                    auto now = std::chrono::system_clock::now();

                    frameCount++;
                    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSecondTime).count();

                    if (elapsedTime >= 10000) {
                        int fps = static_cast<int>(frameCount * 1000 / elapsedTime); // 100 is from frameCount * 1000 / elapsedTime * 10 , becasue  calculate the average rate over 10s
                        BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl3:Decode Real Rate: " << fps << " FPS";
                        frameCount     = 0;
                        lastSecondTime = now;
                    }

                    if (m_last_PTS && (sample.decode_time - m_last_PTS) < 30000000ULL) { // 3s
                        auto next_PTS_expected = m_last_PTS_expected + std::chrono::milliseconds((sample.decode_time - m_last_PTS) / 10000ULL);
                        // The frame is late, catch up a little
                        auto next_PTS_practical = m_last_PTS_practical + std::chrono::milliseconds(minFrameDuration);
                        auto next_PTS = std::max(next_PTS_expected, next_PTS_practical);
                        if(now < next_PTS)
                            std::this_thread::sleep_until(next_PTS);
                        else
                            next_PTS = now;
                        //auto text = wxString::Format(L"wxMediaCtrl3 pts diff %ld\n", std::chrono::duration_cast<std::chrono::milliseconds>(next_PTS - next_PTS_expected).count());
                        //OutputDebugString(text);
                        m_last_PTS = sample.decode_time;
                        m_last_PTS_expected = next_PTS_expected;
                        m_last_PTS_practical = next_PTS;
                    } else {
                        // Resync
                        m_last_PTS           = sample.decode_time;
                        m_last_PTS_expected  = now;
                        m_last_PTS_practical = now;
                    }
                    m_frame = bm;
                }
                CallAfter([this] { Refresh(); });
            }
        }
        if (tunnel) {
            lk.unlock();
            QIDI_Close(tunnel);
            QIDI_Destroy(tunnel);
            tunnel = nullptr;
            lk.lock();
        }
        if (m_url == url)
            m_error = error;
        m_frame_size = wxDefaultSize;
        m_video_size = wxDefaultSize;
        NotifyStopped();
    }

}

void wxMediaCtrl3::NotifyStopped()
{
    m_state = wxMEDIASTATE_STOPPED;
    wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
    event.SetId(GetId());
    event.SetEventObject(this);
    wxPostEvent(this, event);
}

#endif

//y76
wxBEGIN_EVENT_TABLE(VideoPanel, wxPanel)
    EVT_PAINT(VideoPanel::OnPaint)
    EVT_ERASE_BACKGROUND(VideoPanel::OnEraseBackground)
    EVT_SIZE(VideoPanel::OnSize)
wxEND_EVENT_TABLE()

VideoPanel::VideoPanel(wxWindow* parent, 
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size)
    : wxPanel(parent, id, pos, size)
    , m_state(wxMEDIASTATE_STOPPED)
    , m_error(0)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
    
    SetBackgroundColour(wxColour(20, 20, 20));
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    m_thread = std::thread(&VideoPanel::PlayThread, this);
}

VideoPanel::~VideoPanel()
{
    m_exit_flag = true;
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_url.reset();
        m_frame = wxImage(m_idle_image);
        m_cond.notify_all();
    }
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    if (m_curl) {
        curl_easy_cleanup(m_curl);
    }
    curl_global_cleanup();
}

void VideoPanel::Load(const std::string& url)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    
    if (url.empty()) {
        wxLogWarning("VideoPanel: Attempted to load empty URL");
        return;
    }
    
    m_video_size = wxDefaultSize;
    m_error = 0;

    // if (m_state != wxMEDIASTATE_LOADING) {
    //     m_state = wxMEDIASTATE_LOADING;
    //     wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
    //     event.SetId(GetId());
    //     event.SetEventObject(this);
    //     wxPostEvent(this, event);
    // }

    m_url = std::make_shared<std::string>(url);
    
    wxLogMessage("VideoPanel: Loaded URL: %s", url);
    
    m_cond.notify_all();
}

void VideoPanel::Play()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    
    if (m_state != wxMEDIASTATE_PLAYING && m_url) {
        m_state = wxMEDIASTATE_PLAYING;
        
        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);
    }
}

void VideoPanel::Stop()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    
    m_url.reset();
    m_frame = wxImage(m_idle_image);
    m_video_size = wxDefaultSize;
    m_frame_size = wxDefaultSize;
    NotifyStopped();
    m_cond.notify_all();
    CallAfter([this] { 
        Refresh(); 
    });
}

void VideoPanel::SetIdleImage(wxString const &image)
{
    if (m_idle_image == image)
        return;
    m_idle_image = image;
    if (m_url == nullptr) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_frame = wxImage(m_idle_image);

        if (m_frame.IsOk()) {
            CallAfter([this] { 
                Refresh(); 
            });
        }
    }
}

wxMediaState VideoPanel::GetState()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_state;
}

int VideoPanel::GetLastError()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_error;
}

wxSize VideoPanel::GetVideoSize()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    return m_video_size;
}

wxSize VideoPanel::DoGetBestSize() const
{
    return { -1, -1 };
}

void VideoPanel::OnPaint(wxPaintEvent& event)
{
    paintEvent(event);
}

void VideoPanel::OnEraseBackground(wxEraseEvent& event)
{
}

void VideoPanel::OnSize(wxSizeEvent& event)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    
    if (m_video_size.IsFullySpecified()) {
        adjust_frame_size(m_frame_size, m_video_size, GetSize());
    }
    
    Refresh();
    event.Skip();
}

void VideoPanel::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    auto size = GetSize();
    
    if (size.x <= 0 || size.y <= 0) {
        return;
    }
    
    std::unique_lock<std::mutex> lk(m_mutex);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(*wxBLACK_BRUSH);
    dc.DrawRectangle(0, 0, size.x, size.y);
    
    if (!m_frame.IsOk()) {
        return;
    }
    
    wxSize frameSize = m_frame.GetSize();
    
    double scaleX = (double)size.x / frameSize.x;
    double scaleY = (double)size.y / frameSize.y;
    double scale = std::min(scaleX, scaleY);
    
    wxSize scaledSize(frameSize.x * scale, frameSize.y * scale);
    wxPoint pos((size.x - scaledSize.x) / 2, (size.y - scaledSize.y) / 2);
    wxImage scaledImage = m_frame.Scale(scaledSize.x, scaledSize.y, wxIMAGE_QUALITY_HIGH);
    dc.DrawBitmap(wxBitmap(scaledImage), pos.x, pos.y, true);
}

void VideoPanel::adjust_frame_size(wxSize& frame, const wxSize& video, const wxSize& window)
{
    if (video.x * window.y < video.y * window.x) {
        frame = { video.x * window.y / video.y, window.y };
    } else {
        frame = { window.x, video.y * window.x / video.x };
    }
}

void VideoPanel::PlayThread()
{
    using namespace std::chrono_literals;
    std::unique_lock<std::mutex> lk(m_mutex);
    
    std::shared_ptr<std::string> url;
    
    int frameCount = 0;
    std::chrono::steady_clock::time_point lastSecondTime;
    
    while (!m_exit_flag) {
        m_cond.wait(lk, [this, &url] {
            return m_exit_flag ||
                (!m_url && url) ||
                (m_url && !url) ||
                (m_url && url && *m_url != *url);
            });
        
        if (m_exit_flag) {
            break;
        }

        url = m_url;
        
        if (!url) {
            continue;
        }
        
        if (url->empty()) {
            continue;
        }
        
        frameCount = 0;
        lastSecondTime = std::chrono::steady_clock::now();
        
        m_last_PTS = 0;
        m_last_PTS_expected = std::chrono::steady_clock::now();
        m_last_PTS_practical = std::chrono::steady_clock::now();
        
        m_state = wxMEDIASTATE_PLAYING;
         wxMediaEvent stateEvent(wxEVT_MEDIA_STATECHANGED);
         stateEvent.SetId(GetId());
         stateEvent.SetEventObject(this);
         wxPostEvent(this, stateEvent);
        
        lk.unlock();
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            lk.lock();
            m_error = -1;
            NotifyStopped();
            continue;
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url->c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "VideoPanel/1.0");
        
        bool shouldCleanup = true;
        
        while (true) {
            auto now = std::chrono::steady_clock::now();
            uint64_t currentPTS = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
            
            wxMemoryOutputStream memStream;
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &memStream);
            
            CURLcode res = curl_easy_perform(curl);
            
            lk.lock();
            
            if (m_url != url) {
                shouldCleanup = false;
                lk.unlock();
                break;
            }
            
            if (res == CURLE_OK && memStream.GetSize() > 0) {
                wxMemoryInputStream imgStream(memStream);
                wxImage newImage;
                
                if (newImage.LoadFile(imgStream, wxBITMAP_TYPE_JPEG)) {
                    frameCount++;
                    auto nowStat = std::chrono::steady_clock::now();
                    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                        nowStat - lastSecondTime).count();
                    
                    if (elapsedTime >= 1000) {
                        int fps = static_cast<int>(frameCount * 1000 / elapsedTime);
                        wxLogMessage("VideoPanel: Decode Rate: %d FPS", fps);                    
                        frameCount = 0;
                        lastSecondTime = nowStat;
                    }
                    
                    const int minFrameDuration = 100;
                    
                    if (m_last_PTS && (currentPTS - m_last_PTS) < 3000000) {
                        auto next_PTS_expected = m_last_PTS_expected + 
                            std::chrono::microseconds(currentPTS - m_last_PTS);
                        
                        auto next_PTS_practical = m_last_PTS_practical + 
                            std::chrono::milliseconds(minFrameDuration);
                        
                        auto next_PTS = std::max(next_PTS_expected, next_PTS_practical);
                        
                        if (nowStat < next_PTS) {
                            lk.unlock();
                            std::this_thread::sleep_until(next_PTS);
                            lk.lock();
                            
                            if (m_url != url) {
                                shouldCleanup = false;
                                lk.unlock();
                                break;
                            }
                        } else {
                            next_PTS = nowStat;
                        }
                        
                        m_last_PTS = currentPTS;
                        m_last_PTS_expected = next_PTS_expected;
                        m_last_PTS_practical = next_PTS;
                    } else {
                        m_last_PTS = currentPTS;
                        m_last_PTS_expected = nowStat;
                        m_last_PTS_practical = nowStat;
                    }
                    
                    m_frame = newImage;

                    CallAfter([this] { 
                        Refresh(); 
                    });
                    
                } else {
                    m_error = -2;
                    wxLogWarning("VideoPanel: Failed to decode JPEG image");
                }
            } else if (res != CURLE_OK) {
                m_error = res;
                
                lk.unlock();
                std::this_thread::sleep_for(500ms);
                lk.lock();
                
                if (m_url != url) {
                    shouldCleanup = false;
                    lk.unlock();
                    break;
                }
                continue;
            }
            
            lk.unlock();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30FPS
        }
        
        curl_easy_cleanup(curl);
        
        if (shouldCleanup) {
            std::unique_lock<std::mutex> lockAfterCleanup(m_mutex);
            if (m_url == url) {
                m_error = 0;
            }
            
            m_frame_size = wxDefaultSize;
            m_video_size = wxDefaultSize;
            NotifyStopped();
        }
        
        lk = std::unique_lock<std::mutex>(m_mutex);
    }
}

void VideoPanel::NotifyStopped()
{
    m_state = wxMEDIASTATE_STOPPED;
    wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
    event.SetId(GetId());
    event.SetEventObject(this);
    wxPostEvent(this, event);
}

size_t VideoPanel::WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    wxMemoryOutputStream* mem = static_cast<wxMemoryOutputStream*>(userp);
    
    if (mem && realsize > 0) {
        try {
            mem->Write(contents, realsize);
            return realsize;
        } catch (...) {
            return 0;
        }
    }
    
    return 0;
}
//y76