#include "wxMediaCtrl3.h"
#include "AVVideoDecoder.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include <boost/log/trivial.hpp>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#ifdef __WIN32__
#include <versionhelpers.h>
#include <wx/msw/registry.h>
#include <shellapi.h>
#endif

//y77
#include "wxExtensions.hpp"

#include "GUI_App.hpp"
#include "QDSDeviceManager.hpp"

// P2PManager
#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/P2PManager.hpp"
#endif


#if defined(__linux__) || defined(_WIN32)
//wxDEFINE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent);

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
    , m_frame_buffer(9)
{
    SetBackgroundColour("#000001ff");
    m_render_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &wxMediaCtrl3::OnRenderTimer, this);
}

wxMediaCtrl3::~wxMediaCtrl3()
{
    {
        std::unique_lock<std::mutex> lk(m_ui_mutex);
        m_frame = wxImage(m_idle_image);
    }
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_url.reset(new wxURI);
        m_cond.notify_all();
    }
    m_thread.join();
}

void wxMediaCtrl3::Load(wxURI url, std::chrono::system_clock::time_point play_start_time)
{
    std::unique_lock<std::mutex> lk(m_mutex);
    m_video_size = wxDefaultSize;
    m_error = 0;
    m_play_start_time = play_start_time;
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
    {
        std::unique_lock<std::mutex> lk(m_ui_mutex);
        m_frame = wxImage(m_idle_image);
    }
    std::unique_lock<std::mutex> lk(m_mutex);
    m_url.reset();
    NotifyStopped();
    m_cond.notify_all();
    Refresh();
}

void wxMediaCtrl3::SetIdleImage(wxString const &image, wxString const &watermark_text)
{
    if (m_idle_image == image && m_watermark_text == watermark_text)
        return;
    m_idle_image = image;
    m_watermark_text = watermark_text;
    if (m_url == nullptr) {
        std::unique_lock<std::mutex> lk(m_ui_mutex);
        m_frame = wxImage(m_idle_image);
        assert(m_frame.IsOk());
        Refresh();
    }
}

void wxMediaCtrl3::SetIdleImage(const wxImage &image, wxString const &watermark_text)
{
    if (!image.IsOk())
        return;
    m_idle_image.clear();
    m_watermark_text = watermark_text;
    if (m_url == nullptr) {
        std::unique_lock<std::mutex> lk(m_ui_mutex);
        m_frame = image;
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
    PlayFrame current_frame;
    {
        std::unique_lock<std::mutex> lk(m_ui_mutex);
        if (!m_frame.IsOk()) {
            return;
        }
        current_frame = m_frame;
    }
    wxSize frame_size;
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        frame_size = m_frame_size;
    }
    auto size2 = current_frame.GetSize();
    if (size2.x != frame_size.x && size2.y == frame_size.y)
        size2.x = frame_size.x;
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
    dc.DrawBitmap(current_frame, size3.x, size3.y);

    // Draw watermark overlay when showing device preview image
    if (!m_watermark_text.empty() && m_url == nullptr) {
        // Reset user scale for watermark (draw at 1:1)
        dc.SetUserScale(1.0, 1.0);

        wxString watermark_text = m_watermark_text;

        // Setup font
        wxFont font = dc.GetFont();
        font.SetPointSize(10);
        font.SetWeight(wxFONTWEIGHT_BOLD);
        dc.SetFont(font);
        wxSize text_size = dc.GetTextExtent(watermark_text);

        // Calculate watermark rectangle with padding
        int pad_h = FromDIP(12);
        int pad_v = FromDIP(8);
        int wm_w = text_size.GetWidth() + 2 * pad_h;
        int wm_h = text_size.GetHeight() + 2 * pad_v;
        int wm_x = (size.GetWidth() - wm_w) / 2;
        int wm_y = size.GetHeight() - wm_h - FromDIP(10);
        int radius = 8;

        // Use wxGCDC for alpha-blended rounded rectangle
        wxGCDC gcdc(dc);
        gcdc.SetBrush(wxBrush(wxColour(51, 51, 51, 160)));
        gcdc.SetPen(*wxTRANSPARENT_PEN);
        gcdc.DrawRoundedRectangle(wm_x, wm_y, wm_w, wm_h, radius);

        // Draw text centered in the rectangle
        gcdc.SetTextForeground(wxColour(220, 220, 220));
        gcdc.SetFont(font);
        int tx = wm_x + (wm_w - text_size.GetWidth()) / 2;
        int ty = wm_y + (wm_h - text_size.GetHeight()) / 2;
        gcdc.DrawText(watermark_text, tx, ty);
    }
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
    const int decode_warn_thres = 33;
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
        auto t0 = std::chrono::steady_clock::now();
        int error = QIDI_Create(&tunnel, m_url->BuildURI().ToUTF8());
        if (error == 0) {
            QIDI_SetLogger(tunnel, &wxMediaCtrl3::qidi_log, this);
            error = QIDI_Open(tunnel);
            auto t1 = std::chrono::steady_clock::now();
            BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl3: QIDI_Open took "
                                    << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << "ms, error=" << error;
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
        auto t_stream_start = std::chrono::steady_clock::now();
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
        {
            auto t_stream_end = std::chrono::steady_clock::now();
            BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl3: QIDI_StartStream loop took "
                                    << std::chrono::duration_cast<std::chrono::milliseconds>(t_stream_end - t_stream_start).count()
                                    << "ms, error=" << error;
        }
        QIDI_StreamInfo info;
        if (error == 0)
            error = QIDI_GetStreamInfo(tunnel, 0, &info);
        AVVideoDecoder decoder;
        if (error == 0) {
            decoder.open(info);
            m_video_size = { info.format.video.width, info.format.video.height };
            adjust_frame_size(m_frame_size, m_video_size, GetSize());
            NotifyStopped();
            size_t buffer_cap = (size_t) (m_buffer_time * info.format.video.frame_rate / 1000);
            if (buffer_cap == 0) {
                buffer_cap = 1;
            }
            m_frame_buffer.set_capacity(buffer_cap);
            m_get_frame_exit.store(false);
            m_get_frame_thread = std::thread(&wxMediaCtrl3::GetFrameThread, this, info.format.video.frame_rate);
            m_need_refresh.store(false);
            m_render_timer.Start(1000 / (info.format.video.frame_rate + 5));
        }
        QIDI_Sample sample;
        while (error == 0) {
            lk.unlock();
            error = QIDI_ReadSample(tunnel, &sample);
            lk.lock();
            while (error == int(QIDI_would_block)) {
                m_cond.wait_for(lk, 10ms);
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
                PlayFrame bm;
                auto start_decode = std::chrono::steady_clock::now();
                decoder.decode(sample);
                auto end_decode = std::chrono::steady_clock::now();
#ifdef _WIN32
                decoder.toWxBitmap(bm, frame_size);
#else
                decoder.toWxImage(bm, frame_size);
#endif
                auto end_convert = std::chrono::steady_clock::now();
                int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_convert - start_decode).count();
                if (elapsed_ms > decode_warn_thres) {
                    BOOST_LOG_TRIVIAL(warning) << "wxMediaCtrl3: decode + convert too long, decode: "
                                               << std::chrono::duration_cast<std::chrono::milliseconds>(end_decode - start_decode).count()
                                               << " convert: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_convert - end_decode).count();
                }
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

                    m_frame_buffer.enqueue(bm);
                    m_frame = bm;
                }
            }
        }
        if (m_get_frame_thread.joinable()) {
            m_get_frame_exit.store(true);
            m_get_frame_thread.join();
        }
        m_frame_buffer.reset();
        if (tunnel) {
            lk.unlock();
            auto t_close_start = std::chrono::steady_clock::now();
            QIDI_Close(tunnel);
            QIDI_Destroy(tunnel);
            auto t_close_end = std::chrono::steady_clock::now();
            auto close_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_close_end - t_close_start).count();
            if (close_ms > 3000) {
                BOOST_LOG_TRIVIAL(warning) << "wxMediaCtrl3: QIDI_Close+Destroy took " << close_ms << "ms (>3s, potential hang source)";
            } else {
                BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl3: QIDI_Close+Destroy took " << close_ms << "ms";
            }
            tunnel = nullptr;
            lk.lock();
        }
        m_render_timer.Stop();
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

void wxMediaCtrl3::GetFrameThread(int frame_rate)
{
    PlayFrame temp_frame;
    long long frame_count = 0;
    bool pop_success = false;
    std::chrono::system_clock::time_point first_frame_time;
    while (m_get_frame_exit.load() == false) {
        if (m_frame_buffer.try_dequeue(temp_frame) == true) {
            if (!temp_frame.IsOk()) {
                continue;
            }
            {
                std::unique_lock<std::mutex> lk(m_ui_mutex);
                m_frame = temp_frame;
                m_need_refresh.store(true);
            }
            if (pop_success == false) {
                first_frame_time = std::chrono::system_clock::now();
                pop_success = true;
                frame_count = 0;
                auto play_start = m_play_start_time;
                if (play_start != std::chrono::system_clock::time_point{}) {
                    int ms = (int) std::chrono::duration_cast<std::chrono::milliseconds>(first_frame_time - play_start).count();
                    CallAfter([this, ms] {
                        wxCommandEvent evt(EVT_MEDIA_CTRL_FIRST_FRAME);
                        evt.SetEventObject(this);
                        evt.SetInt(ms);
                        wxPostEvent(this, evt);
                    });
                }
            }
            ++frame_count;
            long long  pts_gap = (frame_count * 1000) / frame_rate;
            auto wake_up_time = first_frame_time + std::chrono::milliseconds(pts_gap);
            std::this_thread::sleep_until(wake_up_time);
        } else {
            if (pop_success == true) {
                BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl3:decode too slow or unsteady network, bitmap buffer running out...";
                pop_success = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    {
        std::unique_lock<std::mutex> lk(m_ui_mutex);
        m_frame = wxImage(m_idle_image);
        m_need_refresh.store(true);
        CallAfter([this] { Refresh(false); });
    }
}

void wxMediaCtrl3::OnRenderTimer(wxTimerEvent &evt)
{
    if (m_need_refresh.load() == true) {
        Refresh(false);
        Update();
        m_need_refresh.store(false);
    }
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
    , m_exit_flag(false)
    , m_frameCount(0)
    , m_last_PTS(0)
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
        m_cond.notify_all();
    }
    
    if (m_thread.joinable()) {
        m_thread.join();
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
    m_start_video_requested = true;  // 标记需要向设备请求视频流
    m_stop_video_requested = false;

    m_url = std::make_shared<std::string>(url);

    m_cond.notify_all();
}

void VideoPanel::Play()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    
    if (m_state != wxMEDIASTATE_PLAYING && m_url) {
        m_state = wxMEDIASTATE_PLAYING;
        m_start_video_requested = true;  // 标记需要向设备请求视频流
        m_stop_video_requested = false;

        wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
        event.SetId(GetId());
        event.SetEventObject(this);
        wxPostEvent(this, event);

        m_cond.notify_all();
    }
}

void VideoPanel::Stop()
{
    std::unique_lock<std::mutex> lk(m_mutex);
    
    m_url.reset();
    m_start_video_requested = false;
    m_stop_video_requested = true;
    m_frame = m_idle_image;
    m_video_size = wxDefaultSize;
    m_frame_size = wxDefaultSize;
    
    m_state = wxMEDIASTATE_STOPPED;
    wxMediaEvent event(wxEVT_MEDIA_STATECHANGED);
    event.SetId(GetId());
    event.SetEventObject(this);
    wxPostEvent(this, event);

    m_cond.notify_all();
    CallAfter([this] { 
        Refresh(); 
    });
}

void VideoPanel::SetIdleImage(wxString const &image)
{

    if (m_url == nullptr) {
        std::unique_lock<std::mutex> lk(m_mutex);
        //y77
        m_idle_image = create_scaled_bitmap_form_path(image.ToStdString(), 1046, 601).ConvertToImage();
        m_frame = m_idle_image;

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
    double scale = std::min(scaleX, scaleY);  // 保持宽高比的最小缩放

    wxSize scaledSize(frameSize.x * scale, frameSize.y * scale);
    wxPoint pos((size.x - scaledSize.x) / 2, (size.y - scaledSize.y) / 2);
    wxImage scaledImage = m_frame.Scale(scaledSize.x, scaledSize.y, wxIMAGE_QUALITY_HIGH);
    dc.DrawBitmap(wxBitmap(scaledImage), pos.x, pos.y, true);

#if 0
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

    // Use NORMAL quality for real-time video (HIGH is too slow for 100KB+ images)
    wxSize scaledSize(frameSize.x * scale, frameSize.y * scale);
    wxPoint pos((size.x - scaledSize.x) / 2, (size.y - scaledSize.y) / 2);
    
    // Use cached scaled bitmap when possible (avoid scaling on every paint event)
    bool needRescale = !m_cached_scaled_bitmap.IsOk()
        || m_frame.GetSize() != m_cached_scaled_bitmap.GetSize()
        || size != m_last_window_size;
    
    if (needRescale) {
        wxImage scaledImage = m_frame.Scale(scaledSize.x, scaledSize.y, wxIMAGE_QUALITY_NORMAL);
        m_cached_scaled_bitmap = wxBitmap(scaledImage);
        m_last_window_size = size;
    }
    dc.DrawBitmap(m_cached_scaled_bitmap, pos.x, pos.y, true);
#endif
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

    while (!m_exit_flag) {
        // 等待播放指令
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cond.wait(lk, [this] {
                return m_exit_flag || m_start_video_requested.load();
            });
            if (m_exit_flag) break;
            m_start_video_requested = false;

            ResetPlaybackState();
            m_state = wxMEDIASTATE_PLAYING;
            wxMediaEvent stateEvent(wxEVT_MEDIA_STATECHANGED);
            stateEvent.SetId(GetId());
            stateEvent.SetEventObject(this);
            wxPostEvent(this, stateEvent);
        }

        bool shouldContinue = true;

        auto qds_dev_obj = Slic3r::GUI::wxGetApp().qdsdevmanager->getSelectedDevice();
        bool support_p2p = qds_dev_obj->active_p2p;

        if(support_p2p){
#if QDT_RELEASE_TO_PUBLIC
            // get from p2p
            auto &p2p = P2PManager::instance();
            int videoToken = p2p.onVideo([this, &shouldContinue](uint8_t type, int64_t,
                                          int32_t, const uint8_t *data, size_t len) {
                if (type != 0x02 || !data || len <= 12) return;

                // Skip 12-byte header: [ptsUs(8)][flags(4)][JPEG]
                const uint8_t *jpegData = data + 12;
                size_t         jpegLen  = len - 12;

                wxMemoryInputStream imgStream(jpegData, jpegLen);
                wxImage newImage;
                if (newImage.LoadFile(imgStream, wxBITMAP_TYPE_JPEG)) {
                    {
                        std::lock_guard<std::mutex> glk(m_mutex);
                        if (shouldContinue)
                            m_frame = newImage;
                        else
                            m_frame = m_idle_image;
                        m_video_size = wxSize(newImage.GetWidth(), newImage.GetHeight());
                        UpdateFrameStatistics();
                    }
                    CallAfter([this] { Refresh(); Update(); });
                }
            });

            // Handle P2P state changes – send request_video on connect
            int stateToken = p2p.on(0xFF, [&p2p](uint8_t type, int64_t, int32_t,
                                                  const uint8_t *, size_t) {
                // 0xFF is a pseudo-type we use to receive state change events
                // For simplicity, keep using setStateCallback for state:
            });
            // Actually use setStateCallback for connection state (not packet-based)
            // (state callback is not packet-based, so it remains as a legacy callback)
            int64_t reqId = (int64_t)(std::chrono::system_clock::now().time_since_epoch().count());
            std::string cmd = "{\"method\":\"request_video\"}";
            const int maxRetries = 5;
            for (int retry = 0; retry < maxRetries; retry++) {
                int wrRet = p2p.sendTextCommand(cmd, reqId);
                if (wrRet >= 0) {
                    BOOST_LOG_TRIVIAL(trace) << "VideoPanel: Sent request_video (reqId=" << reqId << ")";
                    break;
                } else {
                    BOOST_LOG_TRIVIAL(trace) << "VideoPanel: Failed to send request_video (attempt " << (retry + 1) << "/" << maxRetries << "), ret=" << wrRet;
                    if (retry < maxRetries - 1)
                        std::this_thread::sleep_for(500ms);
                }
            }

            // Wait loop – replaces the manual pgEvent loop
            while (shouldContinue && !m_exit_flag) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_exit_flag) { shouldContinue = false; break; }
                    if (m_stop_video_requested.load()) {
                        m_stop_video_requested = false;
                        if (p2p.sessionId() != 0) {
                            int64_t reqId = (int64_t)time(nullptr);
                            std::string cmd = "{\"method\":\"stop_video\"}";
                            int wrRet = p2p.sendTextCommand(cmd, reqId);
                            if (wrRet >= 0)
                                BOOST_LOG_TRIVIAL(info) << "VideoPanel: Sent stop_video (reqId=" << reqId << ")";
                            else
                                BOOST_LOG_TRIVIAL(error) << "VideoPanel: Failed to send stop_video, ret=" << wrRet;
                        }
                        shouldContinue = false; break;
                    }
                }

                std::this_thread::sleep_for(100ms);
            }
            //y83
            if (shouldContinue) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_error = 0;
                NotifyStopped();
            }
            p2p.off(videoToken);
            p2p.off(stateToken);
#endif
            m_frame = m_idle_image;
        } else {
            // ============================================================
            // 方式二：URL 下载 JPEG 帧
            // ============================================================
            CURL* curl = nullptr;
            if (curl) { curl_easy_cleanup(curl); curl = nullptr; }
            curl = curl_easy_init();
            if (!curl) { SetErrorAndNotify(-1, "Failed to init CURL"); continue; }

            curl_easy_setopt(curl, CURLOPT_URL, m_url->c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "VideoPanel/1.0");
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            bool shouldContinue = true;
            int consecutiveErrors = 0;
            const int maxConsecutiveErrors = 3;

            while (shouldContinue && !m_exit_flag) {

                wxMemoryOutputStream memStream;
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &memStream);
                CURLcode res = curl_easy_perform(curl);

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    //y83
                    if (m_exit_flag || m_stop_video_requested.load()) { shouldContinue = false; break; }
                }

                if (res == CURLE_OK && memStream.GetSize() > 0) {
                    consecutiveErrors = 0;
                    wxMemoryInputStream imgStream(memStream);
                    wxImage newImage;
                    if (newImage.LoadFile(imgStream, wxBITMAP_TYPE_JPEG)) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_frame = newImage;
                        m_video_size = wxSize(newImage.GetWidth(), newImage.GetHeight());
                        adjust_frame_size(m_frame_size, m_video_size, GetSize());
                        UpdateFrameStatistics();
                        CallAfter([this] { Refresh(); });
                    } else {
                        wxLogWarning("VideoPanel: JPEG decode failed");
                        SetErrorAndNotify(-2, "Image decode failed");
                    }
                } else {
                    consecutiveErrors++;
                    wxLogWarning("VideoPanel: Network error (%d/%d): %s",
                        consecutiveErrors, maxConsecutiveErrors, curl_easy_strerror(res));
                    if (consecutiveErrors >= maxConsecutiveErrors) {
                        SetErrorAndNotify(res, "Too many network errors");
                        shouldContinue = false; break;
                    }
                    std::this_thread::sleep_for(500ms);
                }
            }

            if (shouldContinue) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_error = 0;
                NotifyStopped();
            }
            if (curl) curl_easy_cleanup(curl);
            m_frame = m_idle_image;
        }
    }
}

void VideoPanel::ResetPlaybackState()
{
    m_frameCount = 0;
    m_lastSecondTime = std::chrono::steady_clock::now();
    
    m_last_PTS = 0;
    m_last_PTS_expected = std::chrono::steady_clock::now();
    m_last_PTS_practical = std::chrono::steady_clock::now();
    
    m_error = 0;
}

void VideoPanel::UpdateFrameStatistics()
{
    m_frameCount++;
    auto now = std::chrono::steady_clock::now();
    auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastSecondTime).count();
    
    if (elapsedTime >= 1000) { // 每秒统计一次
        int fps = static_cast<int>(m_frameCount * 1000 / elapsedTime);
        BOOST_LOG_TRIVIAL(trace) << "VideoPanel: Decode FPS=" << fps
            << " (" << m_frameCount << " frames in " << elapsedTime << "ms)";
        m_frameCount = 0;
        m_lastSecondTime = now;
    }
}

void VideoPanel::SetErrorAndNotify(int errorCode, const std::string& errorMsg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = errorCode;
    }
    NotifyStopped();
    wxLogError("VideoPanel: %s", errorMsg);
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