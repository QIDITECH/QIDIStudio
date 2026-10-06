#include "QidiMcpServer.hpp"
#include "QDSDeviceManager.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GLCanvas3D.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "PartPlate.hpp"
#include "Tab.hpp"
#include "OctoPrint.hpp"

#include "slic3r/Utils/Http.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <boost/asio.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <wx/bitmap.h>
#include <wx/glcanvas.h>
#include <wx/dcmemory.h>
#include <wx/dcscreen.h>
#include <wx/dialog.h>
#include <wx/mstream.h>
#include <wx/utils.h>
#include <wx/window.h>

#ifdef __WXMSW__
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <future>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Slic3r::GUI {
namespace {

using json = nlohmann::json;
using tcp  = boost::asio::ip::tcp;

constexpr unsigned short MCP_PORT = 8765;
constexpr std::size_t MAX_REQUEST_BYTES = 1024 * 1024;
constexpr std::size_t MAX_IMAGE_BYTES = 8 * 1024 * 1024;
constexpr std::size_t MAX_ATTACHED_MODEL_FILE_BYTES = 256 * 1024 * 1024;
constexpr std::size_t MAX_ATTACHED_MODEL_TOTAL_BYTES = 512 * 1024 * 1024;
constexpr std::size_t MAX_ATTACHED_MODEL_FILES = 8;
constexpr auto GUI_CALL_TIMEOUT = std::chrono::seconds(30);
constexpr auto PRINT_TOKEN_DEFAULT_TTL = std::chrono::seconds(600);
constexpr auto PRINT_TOKEN_MAX_TTL = std::chrono::seconds(1800);
constexpr auto CAPTURE_DOWNLOAD_TTL = std::chrono::minutes(10);
constexpr std::size_t MAX_CAPTURE_DOWNLOADS = 8;
constexpr const char* CAPTURE_VIEWER_URI = "ui://qidi-studio/capture-viewer-v3.html";

const char* capture_viewer_html()
{
    return R"QIDI_UI(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>QIDI Studio Capture</title>
<style>
  :root { color-scheme: light dark; font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
  * { box-sizing: border-box; }
  body { margin: 0; background: transparent; color: CanvasText; }
  main { overflow: hidden; border: 1px solid color-mix(in srgb, CanvasText 18%, transparent); border-radius: 12px; background: Canvas; }
  .frame { display: grid; min-height: 180px; place-items: center; background: #111; }
  img { display: block; width: 100%; height: auto; max-height: 72vh; object-fit: contain; }
  .loading { padding: 48px 20px; color: #ddd; text-align: center; }
  footer { display: flex; align-items: center; gap: 8px; padding: 10px 12px; }
  .details { min-width: 0; flex: 1; }
  .title { overflow: hidden; font-weight: 650; text-overflow: ellipsis; white-space: nowrap; }
  .meta { margin-top: 2px; color: color-mix(in srgb, CanvasText 62%, transparent); font-size: 12px; }
  a { border: 1px solid color-mix(in srgb, CanvasText 24%, transparent); border-radius: 8px; padding: 7px 10px; color: inherit; text-decoration: none; white-space: nowrap; }
  a[hidden] { display: none; }
</style>
</head>
<body>
<main>
  <div class="frame">
    <div id="loading" class="loading">Waiting for the QIDI capture...</div>
    <img id="capture" hidden alt="QIDI capture">
  </div>
  <footer>
    <div class="details">
      <div id="title" class="title">QIDI Studio capture</div>
      <div id="meta" class="meta">The image will appear when capture completes.</div>
    </div>
    <a id="download" hidden download>Download PNG</a>
  </footer>
</main>
<script>
(() => {
  const capture = document.getElementById("capture");
  const loading = document.getElementById("loading");
  const title = document.getElementById("title");
  const meta = document.getElementById("meta");
  const download = document.getElementById("download");

  function normalizeToolResult(value) {
    if (!value || typeof value !== "object") return null;
    if (Array.isArray(value.content) || value.structuredContent) return value;
    for (const key of ["mcp_tool_result", "call_tool_result", "result"]) {
      const nested = normalizeToolResult(value[key]);
      if (nested) return nested;
    }
    return null;
  }

  function render(result) {
    const blocks = Array.isArray(result && result.content) ? result.content : [];
    const image = blocks.find(block => block && block.type === "image" && typeof block.data === "string");
    const details = result && result.structuredContent && typeof result.structuredContent === "object"
      ? result.structuredContent : {};

    if (!image) {
      loading.textContent = details.error || "Capture completed without an image that this host can display.";
      return;
    }

    const mimeType = image.mimeType || "image/png";
    const dataUrl = `data:${mimeType};base64,${image.data}`;
    const filename = details.download && details.download.filename
      ? details.download.filename : "qidi-capture.png";
    const view = details.view || details.source || "QIDI";
    const dimensions = details.width && details.height ? `${details.width} x ${details.height}` : "";

    capture.src = dataUrl;
    capture.alt = `${view} capture`;
    capture.hidden = false;
    loading.hidden = true;
    title.textContent = filename;
    meta.textContent = [view, dimensions].filter(Boolean).join(" | ");
    download.href = dataUrl;
    download.download = filename;
    download.hidden = false;
  }

  window.addEventListener("message", event => {
    if (event.source !== window.parent) return;
    const message = event.data;
    if (message && message.jsonrpc === "2.0" && message.method === "ui/notifications/tool-result") {
      const result = normalizeToolResult(message.params);
      if (result) render(result);
    }
  });

  const openai = typeof window !== "undefined" ? window.openai : undefined;
  const initialResult = normalizeToolResult(openai && openai.toolResponseMetadata)
    || (openai && openai.toolOutput ? { structuredContent: openai.toolOutput } : null);
  if (initialResult) render(initialResult);
})();
</script>
</body>
</html>)QIDI_UI";
}

struct PreparedPrintJob {
    std::string token;
    std::string fingerprint;
    std::string device_id;
    std::string printer_name;
    std::string upload_name;
    int plate_index{-1};
    bool bed_leveling{false};
    bool timelapse{false};
    int project_filament_index{-1};
    int physical_slot_id{-1};
    bool use_qidi_box{false};
    std::string project_filament_preset;
    std::string physical_filament_type;
    std::string physical_filament_color;
    bool used{false};
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
};

struct ActivePrintJob {
    std::string job_id;
    std::string device_id;
    std::string printer_name;
    std::string upload_name;
    std::string stage{"packaging"};
    std::string error;
    int upload_progress_percent{0};
    bool bed_leveling{false};
    bool timelapse{false};
    int project_filament_index{-1};
    int physical_slot_id{-1};
    bool use_qidi_box{false};
    std::string project_filament_preset;
    std::string physical_filament_type;
    std::string physical_filament_color;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
};

struct DownloadableCapture {
    std::string bytes;
    std::string mime_type;
    std::string filename;
    std::chrono::system_clock::time_point expires_at;
};

std::mutex& print_job_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, PreparedPrintJob>& prepared_print_jobs()
{
    static std::unordered_map<std::string, PreparedPrintJob> jobs;
    return jobs;
}

std::unordered_map<std::string, std::shared_ptr<ActivePrintJob>>& active_print_jobs()
{
    static std::unordered_map<std::string, std::shared_ptr<ActivePrintJob>> jobs;
    return jobs;
}

std::mutex& capture_download_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, DownloadableCapture>& capture_downloads()
{
    static std::unordered_map<std::string, DownloadableCapture> captures;
    return captures;
}

std::string random_hex_id(std::size_t bytes)
{
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes; ++index)
        out << std::setw(2) << (random() & 0xffu);
    return out.str();
}

std::string utc_time_string(std::chrono::system_clock::time_point value)
{
    const std::time_t raw = std::chrono::system_clock::to_time_t(value);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

void update_active_job(const std::shared_ptr<ActivePrintJob>& job,
                       const std::string& stage, const std::string& error = {}, int progress = -1)
{
    std::lock_guard<std::mutex> lock(print_job_mutex());
    job->stage = stage;
    job->error = error;
    if (progress >= 0)
        job->upload_progress_percent = std::clamp(progress, 0, 100);
    job->updated_at = std::chrono::system_clock::now();
}

struct HttpRequest {
    std::string method;
    std::string target;
    std::string body;
};

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

json get_tunnel_status()
{
    json result = {
        {"supported", false},
        {"configured", false},
        {"status_available", false},
        {"healthy", false},
        {"supervisor_healthy", false},
        {"fresh", false},
        {"state", "unavailable"}
    };

#ifdef __WXMSW__
    result["supported"] = true;
    wxString local_app_data;
    if (!wxGetEnv("LOCALAPPDATA", &local_app_data) || local_app_data.empty()) {
        result["error"] = "Windows local application-data directory is unavailable";
        return result;
    }

    const boost::filesystem::path directory = boost::filesystem::path(into_u8(local_app_data)) / "QIDIStudio-MCP";
    const boost::filesystem::path config_path = directory / "tunnel.json";
    const boost::filesystem::path status_path = directory / "tunnel-status.json";
    boost::system::error_code ec;
    result["configured"] = boost::filesystem::is_regular_file(config_path, ec);
    ec.clear();
    if (!boost::filesystem::is_regular_file(status_path, ec)) {
        result["state"] = result["configured"].get<bool>() ? "unknown" : "not_configured";
        return result;
    }

    boost::nowide::ifstream input(status_path.string(), std::ios::binary);
    if (!input) {
        result["state"] = "unreadable";
        result["error"] = "Tunnel status file could not be read";
        return result;
    }
    std::string body((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xef &&
        static_cast<unsigned char>(body[1]) == 0xbb && static_cast<unsigned char>(body[2]) == 0xbf)
        body.erase(0, 3);

    json status;
    try {
        status = json::parse(body);
    } catch (const std::exception&) {
        result["state"] = "invalid";
        result["error"] = "Tunnel status file is not valid JSON";
        return result;
    }
    if (!status.is_object()) {
        result["state"] = "invalid";
        result["error"] = "Tunnel status file must contain a JSON object";
        return result;
    }

    ec.clear();
    const std::time_t modified = boost::filesystem::last_write_time(status_path, ec);
    const std::time_t now = std::time(nullptr);
    const long long age_seconds = ec ? -1 : std::max<long long>(0, static_cast<long long>(now - modified));
    const long long heartbeat_seconds = std::clamp<long long>(status.value("heartbeat_interval_seconds", 15), 1, 300);
    const long long stale_after_seconds = std::max<long long>(45, heartbeat_seconds * 3);
    const std::string state = status.value("state", "unknown");
    const bool fresh = age_seconds >= 0 && age_seconds <= stale_after_seconds;
    const bool healthy = result["configured"].get<bool>() && state == "running" && fresh;

    result["status_available"] = true;
    result["healthy"] = healthy;
    result["supervisor_healthy"] = healthy;
    result["fresh"] = fresh;
    result["state"] = state;
    result["updated_utc"] = status.value("updated_utc", "");
    result["status_age_seconds"] = age_seconds >= 0 ? json(age_seconds) : json(nullptr);
    result["stale_after_seconds"] = stale_after_seconds;
    result["process_id"] = status.value("process_id", json(nullptr));
    result["exit_code"] = status.value("exit_code", json(nullptr));
    result["endpoint"] = "http://127.0.0.1:8765/mcp";
    result["message"] = status.value("message", "");
    result["health_basis"] = "configured companion with a fresh running-state heartbeat";
    result["public_reachability_verified"] = false;
#else
    result["state"] = "unsupported_platform";
#endif
    return result;
}

bool read_http_request(tcp::socket& socket, std::atomic<bool>& stopping, HttpRequest& request)
{
    boost::system::error_code ec;
    socket.non_blocking(true, ec);
    if (ec)
        return false;

    std::string data;
    std::array<char, 4096> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::size_t header_end = std::string::npos;

    while (!stopping && std::chrono::steady_clock::now() < deadline) {
        const std::size_t count = socket.read_some(boost::asio::buffer(buffer), ec);
        if (!ec) {
            data.append(buffer.data(), count);
            if (data.size() > MAX_REQUEST_BYTES)
                return false;
            header_end = data.find("\r\n\r\n");
            if (header_end != std::string::npos)
                break;
            continue;
        }
        if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
            ec.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        return false;
    }
    if (header_end == std::string::npos)
        return false;

    std::istringstream headers(data.substr(0, header_end));
    std::string line;
    if (!std::getline(headers, line))
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();

    std::istringstream request_line(line);
    std::string http_version;
    if (!(request_line >> request.method >> request.target >> http_version))
        return false;

    std::size_t content_length = 0;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string name = lower_copy(line.substr(0, colon));
        if (name == "content-length") {
            try {
                content_length = static_cast<std::size_t>(std::stoull(line.substr(colon + 1)));
            } catch (...) {
                return false;
            }
        }
    }

    if (content_length > MAX_REQUEST_BYTES)
        return false;

    const std::size_t body_start = header_end + 4;
    while (data.size() - body_start < content_length &&
           !stopping && std::chrono::steady_clock::now() < deadline) {
        const std::size_t count = socket.read_some(boost::asio::buffer(buffer), ec);
        if (!ec) {
            data.append(buffer.data(), count);
            if (data.size() > MAX_REQUEST_BYTES)
                return false;
            continue;
        }
        if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
            ec.clear();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        return false;
    }

    if (data.size() - body_start < content_length)
        return false;
    request.body.assign(data, body_start, content_length);
    return true;
}

void write_http_response(tcp::socket& socket, int status, const std::string& body,
                         const char* content_type = "application/json")
{
    const char* reason = status == 200 ? "OK" :
                         status == 202 ? "Accepted" :
                         status == 204 ? "No Content" :
                         status == 404 ? "Not Found" :
                         status == 405 ? "Method Not Allowed" : "Bad Request";
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Access-Control-Allow-Origin: http://localhost\r\n"
             << "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type, Accept, MCP-Protocol-Version\r\n"
             << "Connection: close\r\n\r\n"
             << body;
    const std::string response_bytes = response.str();
    boost::system::error_code ec;
    socket.non_blocking(false, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(warning) << "QIDI MCP could not prepare response socket: "
                                   << ec.message();
        return;
    }
    boost::asio::write(socket, boost::asio::buffer(response_bytes), ec);
    if (ec)
        BOOST_LOG_TRIVIAL(warning) << "QIDI MCP response write failed: " << ec.message();
}

bool find_capture_download(const std::string& target, DownloadableCapture& capture)
{
    static const std::string prefix = "/captures/";
    if (target.rfind(prefix, 0) != 0)
        return false;
    const std::string remainder = target.substr(prefix.size());
    const std::size_t separator = remainder.find('/');
    const std::string token = remainder.substr(0, separator);
    if (token.size() != 48 ||
        !std::all_of(token.begin(), token.end(), [](unsigned char value) { return std::isxdigit(value) != 0; }))
        return false;

    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(capture_download_mutex());
    auto& captures = capture_downloads();
    auto found = captures.find(token);
    if (found == captures.end())
        return false;
    if (found->second.expires_at <= now) {
        captures.erase(found);
        return false;
    }
    capture = found->second;
    return true;
}

void write_capture_download(tcp::socket& socket, const DownloadableCapture& capture)
{
    std::ostringstream headers;
    headers << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: " << capture.mime_type << "\r\n"
            << "Content-Length: " << capture.bytes.size() << "\r\n"
            << "Content-Disposition: attachment; filename=\"" << capture.filename << "\"\r\n"
            << "Cache-Control: private, no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n"
            << "Connection: close\r\n\r\n";
    const std::string header_bytes = headers.str();
    std::array<boost::asio::const_buffer, 2> buffers{
        boost::asio::buffer(header_bytes), boost::asio::buffer(capture.bytes)};
    boost::system::error_code ec;
    socket.non_blocking(false, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(warning) << "QIDI MCP could not prepare capture socket: "
                                   << ec.message();
        return;
    }
    boost::asio::write(socket, buffers, ec);
    if (ec)
        BOOST_LOG_TRIVIAL(warning) << "QIDI MCP capture write failed: " << ec.message();
}

json rpc_error(const json& id, int code, const std::string& message)
{
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
}

class UiBlockedError : public std::runtime_error
{
public:
    UiBlockedError(std::string message, std::string dialog, std::string title = {})
        : std::runtime_error(std::move(message)), m_dialog(std::move(dialog)), m_title(std::move(title)) {}

    const std::string& dialog() const { return m_dialog; }
    const std::string& title() const { return m_title; }

private:
    std::string m_dialog;
    std::string m_title;
};

struct ModalDialogInfo {
    std::string title;
    std::string class_name;
};

bool active_modal_dialog(ModalDialogInfo& result)
{
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()) {
        wxWindow* window = node->GetData();
        wxDialog* dialog = dynamic_cast<wxDialog*>(window);
        if (dialog == nullptr || !dialog->IsShown() || !dialog->IsModal())
            continue;
        result.title = into_u8(dialog->GetTitle());
        result.class_name = dialog->GetClassInfo() != nullptr
            ? into_u8(wxString(dialog->GetClassInfo()->GetClassName())) : "wxDialog";
        return true;
    }
    return false;
}

std::mutex& gui_call_mutex()
{
    static std::mutex mutex;
    return mutex;
}

template<class Fn>
json on_gui_thread(Fn fn)
{
    struct InvocationState {
        std::atomic<int> phase{0}; // 0 queued, 1 executing, 2 cancelled before execution
    };

    std::unique_lock<std::mutex> serialize(gui_call_mutex(), std::try_to_lock);
    if (!serialize.owns_lock())
        return {{"error", "Another QIDI Studio MCP GUI request is still running"},
                {"error_code", "GUI_BUSY"}, {"retry_after_ms", 250}};
    auto promise = std::make_shared<std::promise<json>>();
    auto future  = promise->get_future();
    auto state   = std::make_shared<InvocationState>();
    wxGetApp().CallAfter([promise, state, fn = std::move(fn)]() mutable {
        int queued = 0;
        if (!state->phase.compare_exchange_strong(queued, 1))
            return;
        try {
            promise->set_value(fn());
        } catch (const UiBlockedError& e) {
            json blocked = {
                {"error", e.what()},
                {"error_code", "UI_BLOCKED"},
                {"dialog", e.dialog()}
            };
            if (!e.title().empty())
                blocked["title"] = e.title();
            promise->set_value(std::move(blocked));
        } catch (const std::exception& e) {
            promise->set_value({{"error", e.what()}});
        } catch (...) {
            promise->set_value({{"error", "Unknown QIDI Studio error"}});
        }
    });
    if (future.wait_for(GUI_CALL_TIMEOUT) == std::future_status::ready)
        return future.get();

    int queued = 0;
    const bool cancelled = state->phase.compare_exchange_strong(queued, 2);
    return {{"error", "QIDI Studio did not complete the GUI request within 30 seconds"},
            {"error_code", "GUI_TIMEOUT"},
            {"execution_started", !cancelled}};
}

Plater* require_plater()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        throw std::runtime_error("Plater is not available");
    if (plater->project_recovery_state() != Plater::ProjectRecoveryState::None)
        throw UiBlockedError("QIDI Studio is waiting for the project recovery dialog to be resolved",
                             "project_recovery", "QIDI Studio - Restore");
    ModalDialogInfo modal;
    if (active_modal_dialog(modal))
        throw UiBlockedError("QIDI Studio is waiting for a modal dialog to be resolved",
                             "modal_dialog", modal.title);
    return plater;
}

const char* recovery_state_name(Plater::ProjectRecoveryState state)
{
    switch (state) {
    case Plater::ProjectRecoveryState::None:       return "none";
    case Plater::ProjectRecoveryState::Prompted:   return "prompted";
    case Plater::ProjectRecoveryState::Restoring:  return "restoring";
    case Plater::ProjectRecoveryState::Cancelling: return "cancelling";
    }
    return "unknown";
}

json recovery_state_payload(Plater* plater)
{
    if (plater == nullptr)
        return {{"available", false}, {"state", "unavailable"},
                {"pending", false}, {"ui_blocked", true},
                {"actions", json::array()}};

    const auto state = plater->project_recovery_state();
    json actions = json::array();
    if (state == Plater::ProjectRecoveryState::Prompted) {
        actions.push_back("restore");
        actions.push_back("cancel");
    }
    return {{"available", true}, {"state", recovery_state_name(state)},
            {"pending", state == Plater::ProjectRecoveryState::Prompted},
            {"ui_blocked", state != Plater::ProjectRecoveryState::None},
            {"actions", std::move(actions)}};
}

PartPlate* require_plate(Plater* plater)
{
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    if (plate == nullptr)
        throw std::runtime_error("No active plate");
    return plate;
}

int required_int(const json& args, const char* key)
{
    if (!args.contains(key) || !args[key].is_number_integer())
        throw std::runtime_error(std::string("Missing integer parameter: ") + key);
    return args[key].get<int>();
}

std::string required_string(const json& args, const char* key)
{
    if (!args.contains(key) || !args[key].is_string() || args[key].get<std::string>().empty())
        throw std::runtime_error(std::string("Missing string parameter: ") + key);
    return args[key].get<std::string>();
}

json get_recovery_state()
{
    return on_gui_thread([]() {
        return recovery_state_payload(wxGetApp().plater());
    });
}

json resolve_project_recovery(const json& args)
{
    std::string action;
    try {
        action = required_string(args, "action");
    } catch (const std::exception& e) {
        return {{"error", e.what()}};
    }

    if (action != "restore" && action != "cancel")
        return {{"error", "action must be restore or cancel"}};
    if (action == "cancel" && !args.value("confirm", false))
        return {{"error", "Cancelling deletes the pending recovery data; retry with confirm=true"}};

    const bool restore = action == "restore";
    return on_gui_thread([action, restore]() {
        Plater* plater = wxGetApp().plater();
        if (plater == nullptr)
            return json{{"error", "Plater is not available"}};

        const auto state = plater->project_recovery_state();
        if (state != Plater::ProjectRecoveryState::Prompted) {
            return json{{"error", "No project recovery dialog is awaiting a decision"},
                        {"state", recovery_state_name(state)}};
        }
        if (!plater->resolve_project_recovery(restore)) {
            return json{{"error", "The project recovery dialog could not be resolved"},
                        {"state", recovery_state_name(plater->project_recovery_state())}};
        }

        return json{{"accepted", true}, {"action", action},
                    {"message", "QIDI Studio accepted the recovery decision; use get_recovery_state to observe completion"}};
    });
}

std::string base64_encode(const std::string& bytes)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int a = static_cast<unsigned char>(bytes[i]);
        const unsigned int b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const unsigned int c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        const unsigned int value = (a << 16) | (b << 8) | c;
        encoded.push_back(alphabet[(value >> 18) & 0x3f]);
        encoded.push_back(alphabet[(value >> 12) & 0x3f]);
        encoded.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 0x3f] : '=');
        encoded.push_back(i + 2 < bytes.size() ? alphabet[value & 0x3f] : '=');
    }
    return encoded;
}

const char* image_mime_type(const std::string& bytes)
{
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 && static_cast<unsigned char>(bytes[2]) == 0xff)
        return "image/jpeg";
    if (bytes.size() >= 8 && static_cast<unsigned char>(bytes[0]) == 0x89 &&
        bytes.compare(1, 3, "PNG") == 0)
        return "image/png";
    if (bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WEBP") == 0)
        return "image/webp";
    return nullptr;
}

json attach_mcp_image(json metadata, const std::string& bytes, const std::string& mime_type,
                      const std::string& filename_stem)
{
    const auto now = std::chrono::system_clock::now();
    const auto expires_at = now + CAPTURE_DOWNLOAD_TTL;
    const std::string token = random_hex_id(24);
    const std::string extension = mime_type == "image/jpeg" ? ".jpg" :
                                  mime_type == "image/webp" ? ".webp" : ".png";
    const std::string filename = filename_stem + "-" + token.substr(0, 12) + extension;
    {
        std::lock_guard<std::mutex> lock(capture_download_mutex());
        auto& captures = capture_downloads();
        for (auto iterator = captures.begin(); iterator != captures.end();) {
            if (iterator->second.expires_at <= now)
                iterator = captures.erase(iterator);
            else
                ++iterator;
        }
        while (captures.size() >= MAX_CAPTURE_DOWNLOADS)
            captures.erase(captures.begin());
        captures.emplace(token, DownloadableCapture{bytes, mime_type, filename, expires_at});
    }

    metadata["image"] = {{"mime_type", mime_type}, {"byte_count", bytes.size()}};
    metadata["download"] = {
        {"scheme", "http"},
        {"origin", "127.0.0.1:8765"},
        {"path", "/captures/" + token + "/" + filename},
        {"filename", filename},
        {"expires_at_utc", utc_time_string(expires_at)},
        {"local_computer_only", true}
    };
    metadata["_mcp_image"] = {{"mimeType", mime_type}, {"data", base64_encode(bytes)}};
    return metadata;
}

json get_ui_state()
{
    return on_gui_thread([]() {
        Plater* plater = wxGetApp().plater();
        json dialogs = json::array();
        bool has_modal = false;
        for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()) {
            wxWindow* window = node->GetData();
            wxDialog* dialog = dynamic_cast<wxDialog*>(window);
            if (dialog == nullptr || !dialog->IsShown())
                continue;
            const bool modal = dialog->IsModal();
            has_modal = has_modal || modal;
            dialogs.push_back({
                {"title", into_u8(dialog->GetTitle())},
                {"class_name", dialog->GetClassInfo() != nullptr
                    ? into_u8(wxString(dialog->GetClassInfo()->GetClassName())) : "wxDialog"},
                {"modal", modal},
                {"enabled", dialog->IsEnabled()}
            });
        }

        int selected_tab = -1;
        std::string selected_view = "unavailable";
        MainFrame* frame = wxGetApp().mainframe;
        if (frame != nullptr && frame->m_tabpanel != nullptr) {
            selected_tab = frame->m_tabpanel->GetSelection();
            selected_view = selected_tab == MainFrame::tp3DEditor ? "prepare" :
                            selected_tab == MainFrame::tpPreview ? "preview" : "other";
        }
        const bool recovering = plater != nullptr &&
            plater->project_recovery_state() != Plater::ProjectRecoveryState::None;
        return json{{"ui_blocked", recovering || has_modal},
                    {"selected_tab", selected_tab}, {"selected_view", selected_view},
                    {"dialogs", std::move(dialogs)},
                    {"recovery", recovery_state_payload(plater)}};
    });
}

json capture_studio_screenshot(const json& args)
{
    const std::string target = args.value("target", "current");
    if (target != "current" && target != "prepare" && target != "preview")
        return {{"error", "target must be current, prepare, or preview"}};

    bool background = true;
    if (args.contains("background")) {
        if (!args["background"].is_boolean())
            return {{"error", "background must be a boolean"}};
        background = args["background"].get<bool>();
    }

    return on_gui_thread([target, background]() {
        MainFrame* frame = wxGetApp().mainframe;
        if (frame == nullptr || frame->m_tabpanel == nullptr)
            throw std::runtime_error("QIDI Studio main window is not available");

        const int original_tab = frame->m_tabpanel->GetSelection();
#ifdef __WXMSW__
        const HWND hwnd = reinterpret_cast<HWND>(frame->GetHandle());
        if (hwnd == nullptr)
            throw std::runtime_error("Could not access the QIDI Studio window");

        struct CaptureRestoreGuard
        {
            MainFrame* frame{nullptr};
            HWND hwnd{nullptr};
            int selected_tab{-1};
            WINDOWPLACEMENT placement{};
            bool placement_valid{false};
            bool was_visible{false};
            bool was_iconized{false};
            bool relocated{false};
            bool restored{false};
            bool tab_restore_succeeded{true};
            bool placement_restore_succeeded{true};

            CaptureRestoreGuard(MainFrame* value, HWND handle, int tab)
                : frame(value), hwnd(handle), selected_tab(tab)
            {
                placement.length = sizeof(placement);
                placement_valid = ::GetWindowPlacement(hwnd, &placement) != FALSE;
                was_visible = ::IsWindowVisible(hwnd) != FALSE;
                was_iconized = ::IsIconic(hwnd) != FALSE;
            }

            bool show_offscreen()
            {
                if (!placement_valid)
                    throw std::runtime_error("Could not save the QIDI Studio window placement");

                RECT normal = placement.rcNormalPosition;
                int width = normal.right - normal.left;
                int height = normal.bottom - normal.top;
                if (width <= 0 || height <= 0) {
                    RECT current{};
                    if (::GetWindowRect(hwnd, &current) == FALSE)
                        throw std::runtime_error("Could not determine the QIDI Studio window size");
                    width = current.right - current.left;
                    height = current.bottom - current.top;
                }
                if (width <= 0 || height <= 0)
                    throw std::runtime_error("QIDI Studio main window has no capturable area");

                const int offscreen_x = ::GetSystemMetrics(SM_XVIRTUALSCREEN) +
                                        ::GetSystemMetrics(SM_CXVIRTUALSCREEN) + 64;
                const int offscreen_y = ::GetSystemMetrics(SM_YVIRTUALSCREEN) + 64;

                WINDOWPLACEMENT capture = placement;
                capture.flags = 0;
                capture.showCmd = SW_SHOWNOACTIVATE;
                capture.rcNormalPosition.left = offscreen_x;
                capture.rcNormalPosition.top = offscreen_y;
                capture.rcNormalPosition.right = offscreen_x + width;
                capture.rcNormalPosition.bottom = offscreen_y + height;

                relocated = true;
                if (::SetWindowPlacement(hwnd, &capture) == FALSE)
                    throw std::runtime_error("Could not prepare the QIDI Studio window for background capture");
                if (::SetWindowPos(hwnd, HWND_BOTTOM, offscreen_x, offscreen_y, width, height,
                                   SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW) == FALSE)
                    throw std::runtime_error("Could not show the QIDI Studio window off-screen");
                return true;
            }

            void restore() noexcept
            {
                if (restored)
                    return;
                restored = true;

                try {
                    if (frame != nullptr && frame->m_tabpanel != nullptr &&
                        selected_tab >= 0 && frame->m_tabpanel->GetSelection() != selected_tab) {
                        frame->select_tab(static_cast<size_t>(selected_tab));
                        frame->Layout();
                        frame->Update();
                        wxYieldIfNeeded();
                    }
                } catch (...) {
                    tab_restore_succeeded = false;
                }
                if (frame != nullptr && frame->m_tabpanel != nullptr && selected_tab >= 0)
                    tab_restore_succeeded = tab_restore_succeeded &&
                        frame->m_tabpanel->GetSelection() == selected_tab;

                if (relocated && placement_valid && hwnd != nullptr) {
                    placement_restore_succeeded =
                        ::SetWindowPlacement(hwnd, &placement) != FALSE;
                    if (!was_visible)
                        ::ShowWindow(hwnd, SW_HIDE);
                    placement_restore_succeeded = placement_restore_succeeded &&
                        ((::IsWindowVisible(hwnd) != FALSE) == was_visible) &&
                        (!was_iconized || ::IsIconic(hwnd) != FALSE);
                }
            }

            ~CaptureRestoreGuard()
            {
                restore();
            }
        };

        CaptureRestoreGuard restore_guard(frame, hwnd, original_tab);
        if (!background && restore_guard.was_iconized)
            throw std::runtime_error("Restore the QIDI Studio window before taking a screenshot");
        const bool captured_offscreen = background &&
            (restore_guard.was_iconized || !restore_guard.was_visible) &&
            restore_guard.show_offscreen();
#else
        if (frame->IsIconized())
            throw std::runtime_error("Restore the QIDI Studio window before taking a screenshot");
        const bool captured_offscreen = false;
#endif

        if (target != "current") {
            require_plater();
            frame->select_tab(static_cast<size_t>(target == "prepare" ? MainFrame::tp3DEditor
                                                                       : MainFrame::tpPreview));
        }
#ifdef __WXMSW__
        if (!background)
            frame->Show(true);
#else
        frame->Show(true);
#endif
        frame->Layout();
        frame->Update();
        wxYieldIfNeeded();

        const int captured_tab = frame->m_tabpanel->GetSelection();
        const std::string captured_view = captured_tab == MainFrame::tp3DEditor ? "prepare" :
                                          captured_tab == MainFrame::tpPreview ? "preview" : "other";
#ifdef __WXMSW__
        GLCanvas3D* canvas_to_capture = nullptr;
        if (captured_tab == MainFrame::tp3DEditor)
            canvas_to_capture = require_plater()->get_view3D_canvas3D();
        else if (captured_tab == MainFrame::tpPreview)
            canvas_to_capture = require_plater()->get_preview_canvas3D();
#endif

        const wxRect rect = frame->GetScreenRect();
        if (rect.width <= 0 || rect.height <= 0)
            throw std::runtime_error("QIDI Studio main window has no capturable area");
        wxImage image;
        std::string capture_method;
#ifdef __WXMSW__
        HDC reference_dc = ::GetDC(hwnd);
        if (reference_dc == nullptr)
            throw std::runtime_error("Could not access the QIDI Studio window");

        HDC memory_dc = ::CreateCompatibleDC(reference_dc);
        BITMAPINFO bitmap_info{};
        bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap_info.bmiHeader.biWidth = rect.width;
        bitmap_info.bmiHeader.biHeight = -rect.height;
        bitmap_info.bmiHeader.biPlanes = 1;
        bitmap_info.bmiHeader.biBitCount = 32;
        bitmap_info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        HBITMAP dib = memory_dc != nullptr
            ? ::CreateDIBSection(reference_dc, &bitmap_info, DIB_RGB_COLORS, &pixels, nullptr, 0)
            : nullptr;
        HGDIOBJ previous = dib != nullptr ? ::SelectObject(memory_dc, dib) : nullptr;
        const BOOL captured = previous != nullptr && previous != HGDI_ERROR
            ? ::PrintWindow(hwnd, memory_dc, PW_RENDERFULLCONTENT)
            : FALSE;
        ::GdiFlush();

        if (captured && pixels != nullptr) {
            image.Create(rect.width, rect.height, false);
            unsigned char* rgb = image.GetData();
            const unsigned char* bgra = static_cast<const unsigned char*>(pixels);
            const std::size_t pixel_count = static_cast<std::size_t>(rect.width) * rect.height;
            for (std::size_t i = 0; i < pixel_count; ++i) {
                rgb[i * 3] = bgra[i * 4 + 2];
                rgb[i * 3 + 1] = bgra[i * 4 + 1];
                rgb[i * 3 + 2] = bgra[i * 4];
            }
        }

        if (previous != nullptr && previous != HGDI_ERROR)
            ::SelectObject(memory_dc, previous);
        if (dib != nullptr)
            ::DeleteObject(dib);
        if (memory_dc != nullptr)
            ::DeleteDC(memory_dc);
        ::ReleaseDC(hwnd, reference_dc);

        if (!captured || !image.IsOk())
            throw std::runtime_error("Windows could not render the QIDI Studio window for capture");
        capture_method = captured_offscreen ? "windows_print_window_offscreen"
                                            : "windows_print_window";

        bool gl_canvas_composited = false;
        int gl_canvas_x = 0;
        int gl_canvas_y = 0;
        int gl_canvas_width = 0;
        int gl_canvas_height = 0;
        int gl_canvas_dynamic_range = 0;
        if (canvas_to_capture != nullptr) {
            std::vector<unsigned char> rgba;
            unsigned int framebuffer_width = 0;
            unsigned int framebuffer_height = 0;
            if (!canvas_to_capture->capture_framebuffer(
                    rgba, framebuffer_width, framebuffer_height))
                throw std::runtime_error(
                    "QIDI Studio could not capture the OpenGL build-plate canvas");

            wxGLCanvas* canvas_window = canvas_to_capture->get_wxglcanvas();
            const HWND canvas_hwnd = canvas_window != nullptr
                ? reinterpret_cast<HWND>(canvas_window->GetHandle()) : nullptr;
            RECT frame_bounds{};
            RECT canvas_bounds{};
            if (canvas_hwnd == nullptr ||
                ::GetWindowRect(hwnd, &frame_bounds) == FALSE ||
                ::GetWindowRect(canvas_hwnd, &canvas_bounds) == FALSE)
                throw std::runtime_error(
                    "QIDI Studio could not locate the OpenGL canvas in the captured window");

            const int native_frame_width = frame_bounds.right - frame_bounds.left;
            const int native_frame_height = frame_bounds.bottom - frame_bounds.top;
            if (native_frame_width <= 0 || native_frame_height <= 0)
                throw std::runtime_error("QIDI Studio returned invalid window geometry");
            gl_canvas_x = static_cast<int>(
                static_cast<std::int64_t>(canvas_bounds.left - frame_bounds.left) *
                image.GetWidth() / native_frame_width);
            gl_canvas_y = static_cast<int>(
                static_cast<std::int64_t>(canvas_bounds.top - frame_bounds.top) *
                image.GetHeight() / native_frame_height);
            gl_canvas_width = static_cast<int>(
                static_cast<std::int64_t>(canvas_bounds.right - canvas_bounds.left) *
                image.GetWidth() / native_frame_width);
            gl_canvas_height = static_cast<int>(
                static_cast<std::int64_t>(canvas_bounds.bottom - canvas_bounds.top) *
                image.GetHeight() / native_frame_height);
            if (gl_canvas_width <= 0 || gl_canvas_height <= 0 ||
                framebuffer_width == 0 || framebuffer_height == 0 ||
                rgba.size() != static_cast<std::size_t>(framebuffer_width) *
                                   framebuffer_height * 4)
                throw std::runtime_error("QIDI Studio returned an invalid OpenGL canvas capture");

            unsigned char minimum_canvas_channel = 255;
            unsigned char maximum_canvas_channel = 0;
            unsigned char* destination = image.GetData();
            for (int destination_y = 0; destination_y < gl_canvas_height; ++destination_y) {
                const int image_y = gl_canvas_y + destination_y;
                if (image_y < 0 || image_y >= image.GetHeight())
                    continue;
                const unsigned int source_y = framebuffer_height - 1 -
                    static_cast<unsigned int>(
                        static_cast<std::uint64_t>(destination_y) * framebuffer_height /
                        static_cast<unsigned int>(gl_canvas_height));
                for (int destination_x = 0; destination_x < gl_canvas_width; ++destination_x) {
                    const int image_x = gl_canvas_x + destination_x;
                    if (image_x < 0 || image_x >= image.GetWidth())
                        continue;
                    const unsigned int source_x = static_cast<unsigned int>(
                        static_cast<std::uint64_t>(destination_x) * framebuffer_width /
                        static_cast<unsigned int>(gl_canvas_width));
                    const std::size_t source_offset =
                        (static_cast<std::size_t>(source_y) * framebuffer_width + source_x) * 4;
                    const std::size_t destination_offset =
                        (static_cast<std::size_t>(image_y) * image.GetWidth() + image_x) * 3;
                    for (int channel = 0; channel < 3; ++channel) {
                        const unsigned char value = rgba[source_offset + channel];
                        destination[destination_offset + channel] = value;
                        minimum_canvas_channel = std::min(minimum_canvas_channel, value);
                        maximum_canvas_channel = std::max(maximum_canvas_channel, value);
                    }
                }
            }
            gl_canvas_dynamic_range = static_cast<int>(maximum_canvas_channel) -
                                      static_cast<int>(minimum_canvas_channel);
            if (gl_canvas_dynamic_range < 4)
                throw std::runtime_error(
                    "QIDI Studio returned a blank OpenGL build-plate canvas");
            gl_canvas_composited = true;
            capture_method += "+opengl_back_buffer";
        }
#else
        const bool gl_canvas_composited = false;
        const int gl_canvas_x = 0;
        const int gl_canvas_y = 0;
        const int gl_canvas_width = 0;
        const int gl_canvas_height = 0;
        const int gl_canvas_dynamic_range = 0;
        wxBitmap bitmap(rect.width, rect.height, 24);
        if (!bitmap.IsOk())
            throw std::runtime_error("Could not allocate the QIDI Studio screenshot bitmap");
        wxScreenDC screen;
        wxMemoryDC memory;
        memory.SelectObject(bitmap);
        const bool captured = memory.Blit(0, 0, rect.width, rect.height,
                                          &screen, rect.x, rect.y, wxCOPY, false);
        memory.SelectObject(wxNullBitmap);
        if (!captured)
            throw std::runtime_error("Could not capture the QIDI Studio window");
        image = bitmap.ConvertToImage();
        capture_method = "screen_pixels";
#endif

        if (!image.IsOk() || image.GetData() == nullptr)
            throw std::runtime_error("QIDI Studio returned an invalid screenshot");
        unsigned char minimum_channel = 255;
        unsigned char maximum_channel = 0;
        const unsigned char* image_data = image.GetData();
        const std::size_t channel_count =
            static_cast<std::size_t>(image.GetWidth()) * image.GetHeight() * 3;
        const std::size_t sample_stride = std::max<std::size_t>(3, channel_count / 12000);
        for (std::size_t offset = 0; offset < channel_count; offset += sample_stride) {
            minimum_channel = std::min(minimum_channel, image_data[offset]);
            maximum_channel = std::max(maximum_channel, image_data[offset]);
        }
        const int sampled_dynamic_range =
            static_cast<int>(maximum_channel) - static_cast<int>(minimum_channel);
        if (sampled_dynamic_range < 4)
            throw std::runtime_error(
                "QIDI Studio returned a blank background capture; off-screen OpenGL rendering is required");

        wxMemoryOutputStream stream;
        if (!image.SaveFile(stream, wxBITMAP_TYPE_PNG))
            throw std::runtime_error("Could not encode the QIDI Studio screenshot");
        if (stream.GetSize() == 0 || stream.GetSize() > MAX_IMAGE_BYTES)
            throw std::runtime_error("QIDI Studio screenshot exceeds the 8 MiB MCP image limit");
        std::string bytes(stream.GetSize(), '\0');
        stream.CopyTo(bytes.data(), bytes.size());
#ifdef __WXMSW__
        const bool window_was_visible = restore_guard.was_visible;
        const bool window_was_iconized = restore_guard.was_iconized;
        restore_guard.restore();
        const bool state_restored = restore_guard.placement_restore_succeeded;
        const bool selected_tab_restored = restore_guard.tab_restore_succeeded;
#else
        bool selected_tab_restored = true;
        if (original_tab >= 0 && frame->m_tabpanel->GetSelection() != original_tab) {
            try {
                frame->select_tab(static_cast<size_t>(original_tab));
                frame->Layout();
                frame->Update();
                wxYieldIfNeeded();
            } catch (...) {
                selected_tab_restored = false;
            }
        }
        selected_tab_restored = selected_tab_restored &&
            (original_tab < 0 || frame->m_tabpanel->GetSelection() == original_tab);
        const bool state_restored = true;
        const bool window_was_visible = frame->IsShown();
        const bool window_was_iconized = false;
#endif

        return attach_mcp_image({{"captured", true}, {"source", "qidi_studio_window"},
                                 {"view", captured_view}, {"width", rect.width}, {"height", rect.height},
                                 {"background_requested", background},
                                 {"captured_offscreen", captured_offscreen},
                                 {"window_was_visible", window_was_visible},
                                 {"window_was_iconized", window_was_iconized},
                                 {"window_state_restored", state_restored},
                                 {"selected_tab_restored", selected_tab_restored},
                                 {"sampled_dynamic_range", sampled_dynamic_range},
                                 {"gl_canvas_composited", gl_canvas_composited},
                                 {"gl_canvas_x", gl_canvas_x},
                                 {"gl_canvas_y", gl_canvas_y},
                                 {"gl_canvas_width", gl_canvas_width},
                                 {"gl_canvas_height", gl_canvas_height},
                                 {"gl_canvas_dynamic_range", gl_canvas_dynamic_range},
                                 {"screen_capture", capture_method == "screen_pixels"},
                                 {"capture_method", capture_method},
                                 {"note", capture_method == "screen_pixels"
                                     ? "The capture contains visible screen pixels; overlapping windows may appear."
                                     : captured_offscreen
                                         ? "QIDI Studio was rendered outside the visible desktop without activation and its prior window state was restored."
                                         : "Captured directly from the QIDI Studio window; overlapping windows are excluded."}},
                                bytes, "image/png", "qidi-studio-" + captured_view);
    });
}

json printer_monitor_state(const std::shared_ptr<QDSDevice>& device)
{
    if (!device)
        return nullptr;
    return {
        {"device_id", device->m_id},
        {"name", device->m_name},
        {"online", device->is_online()},
        {"status", device->m_status},
        {"print_state", device->m_print_state},
        {"filename", device->m_print_filename},
        {"progress", device->m_print_progress},
        {"progress_fraction", device->m_print_progress_float},
        {"plate_index", device->m_plate_index},
        {"layer", {{"current", device->m_print_cur_layer}, {"total", device->m_print_total_layer}}},
        {"duration", {{"elapsed", device->m_print_duration}, {"total", device->m_print_total_duration},
                      {"estimated_total", device->m_print_total_time}}},
        {"temperatures", {{"nozzle", device->m_extruder_temperature},
                          {"nozzle_target", device->m_target_extruder},
                          {"bed", device->m_bed_temperature}, {"bed_target", device->m_target_bed},
                          {"chamber", device->m_chamber_temperature}, {"chamber_target", device->m_target_chamber}}},
        {"fans", {{"part", device->m_cooling_fan_speed}, {"auxiliary", device->m_auxiliary_fan_speed},
                  {"chamber", device->m_chamber_fan_speed}}},
        {"speed_percent", device->m_print_speed_display_percent},
        {"case_light", device->m_case_light},
        {"filament_sensor", device->m_extruder_filament}
    };
}

json capture_printer_camera(QDSDeviceManager* manager, const json& args)
{
    if (manager == nullptr)
        return {{"error", "QIDI device manager is not available"}};
    const std::string device_id = required_string(args, "device_id");
    int warmup_ms = 1200;
    if (args.contains("light_warmup_ms")) {
        if (!args["light_warmup_ms"].is_number_integer())
            return {{"error", "light_warmup_ms must be an integer"}};
        warmup_ms = args["light_warmup_ms"].get<int>();
    }
    if (warmup_ms < 0 || warmup_ms > 5000)
        return {{"error", "light_warmup_ms must be between 0 and 5000"}};

    std::shared_ptr<QDSDevice> device = manager->getDevice(device_id);
    if (!device)
        return {{"error", "Printer was not found"}};
    if (!device->is_online())
        return {{"error", "Printer is offline"}};
    const std::string camera_base = device->m_frp_url;
    if (camera_base.empty() || (camera_base.rfind("http://", 0) != 0 && camera_base.rfind("https://", 0) != 0))
        return {{"error", "Printer camera endpoint is not available"}};

    const bool light_was_on = device->m_case_light;
    manager->sendCommand(device_id, "SET_PIN PIN=caselight VALUE=1");
    if (warmup_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(warmup_ms));

    std::string body;
    std::string request_error;
    unsigned int response_status = 0;
    auto http = Slic3r::Http::get(camera_base + "/webcam/?action=snapshot");
    http.timeout_max(8)
        .header("Accept", "image/jpeg,image/png,image/webp")
        .on_complete([&](std::string response, unsigned int status) {
            body = std::move(response);
            response_status = status;
        })
        .on_error([&](std::string, std::string error, unsigned int status) {
            request_error = std::move(error);
            response_status = status;
        })
        .perform_sync();

    if (!request_error.empty())
        return {{"error", "Printer camera request failed: " + request_error},
                {"device_id", device_id}, {"light_command_sent", true}};
    if (response_status < 200 || response_status >= 300)
        return {{"error", "Printer camera returned HTTP status " + std::to_string(response_status)},
                {"device_id", device_id}, {"light_command_sent", true}};
    if (body.empty() || body.size() > MAX_IMAGE_BYTES)
        return {{"error", body.empty() ? "Printer camera returned an empty image"
                                        : "Printer camera image exceeds the 8 MiB MCP image limit"},
                {"device_id", device_id}, {"light_command_sent", true}};
    const char* mime_type = image_mime_type(body);
    if (mime_type == nullptr)
        return {{"error", "Printer camera response is not a supported JPEG, PNG, or WebP image"},
                {"device_id", device_id}, {"light_command_sent", true}};

    return attach_mcp_image({{"captured", true}, {"source", "printer_camera"},
                             {"device_id", device_id}, {"printer_name", device->m_name},
                             {"light_was_on", light_was_on}, {"light_command_sent", true},
                             {"light_left_on", true}, {"light_warmup_ms", warmup_ms}},
                            body, mime_type, "qidi-printer-camera");
}

json set_printer_case_light(QDSDeviceManager* manager, const json& args)
{
    if (manager == nullptr)
        return {{"error", "QIDI device manager is not available"}};
    const std::string device_id = required_string(args, "device_id");
    if (!args.contains("on") || !args["on"].is_boolean())
        return {{"error", "on must be a boolean"}};
    const bool requested_on = args["on"].get<bool>();
    int confirm_wait_ms = 750;
    if (args.contains("confirm_wait_ms")) {
        if (!args["confirm_wait_ms"].is_number_integer())
            return {{"error", "confirm_wait_ms must be an integer"}};
        confirm_wait_ms = args["confirm_wait_ms"].get<int>();
    }
    if (confirm_wait_ms < 0 || confirm_wait_ms > 5000)
        return {{"error", "confirm_wait_ms must be between 0 and 5000"}};
    std::shared_ptr<QDSDevice> device = manager->getDevice(device_id);
    if (!device)
        return {{"error", "Printer was not found"}};
    if (!device->is_online())
        return {{"error", "Printer is offline"}};

    const bool reported_before = device->m_case_light;
    manager->sendCommand(device_id, requested_on ? "SET_PIN PIN=caselight VALUE=1"
                                                 : "SET_PIN PIN=caselight VALUE=0");
    if (confirm_wait_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(confirm_wait_ms));
    const bool reported_after = device->m_case_light;
    return {
        {"accepted", true},
        {"device_id", device->m_id},
        {"printer_name", device->m_name},
        {"requested_on", requested_on},
        {"reported_before", reported_before},
        {"reported_after", reported_after},
        {"confirm_wait_ms", confirm_wait_ms},
        {"telemetry_confirmed", reported_after == requested_on},
        {"note", reported_after == requested_on
            ? "Printer telemetry confirms the requested case-light state."
            : "The command was sent, but printer telemetry has not confirmed the new state yet."}
    };
}

json capture_print_monitor_snapshot(QDSDeviceManager* manager, const json& args)
{
    const auto started = std::chrono::steady_clock::now();
    json result = capture_printer_camera(manager, args);
    const auto finished = std::chrono::steady_clock::now();
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
    if (result.contains("error")) {
        result["monitoring_snapshot"] = false;
        result["capture_duration_ms"] = duration_ms;
        return result;
    }

    const std::string device_id = required_string(args, "device_id");
    std::shared_ptr<QDSDevice> device = manager == nullptr ? nullptr : manager->getDevice(device_id);
    result["monitoring_snapshot"] = true;
    result["snapshot_id"] = random_hex_id(8);
    result["captured_utc"] = utc_time_string(std::chrono::system_clock::now());
    result["capture_duration_ms"] = duration_ms;
    result["printer"] = printer_monitor_state(device);
    result["image_interpreted"] = false;
    result["automatic_print_control"] = false;
    result["note"] = "This snapshot is observational. It does not diagnose the image or pause, cancel, or otherwise control the print.";
    return result;
}

ModelInstance* require_instance(PartPlate* plate, int object_id, int instance_id)
{
    if (object_id < 0 || instance_id < 0)
        throw std::runtime_error("object_id and instance_id must be non-negative");
    ModelInstance* instance = plate->get_instance(object_id, instance_id);
    if (instance == nullptr)
        throw std::runtime_error("Object instance was not found on the active plate");
    return instance;
}

ModelObject* require_model_object(Plater* plater, int object_id)
{
    if (object_id < 0 || static_cast<size_t>(object_id) >= plater->model().objects.size() ||
        plater->model().objects[object_id] == nullptr)
        throw std::runtime_error("Object was not found");
    return plater->model().objects[object_id];
}

ModelVolume* require_model_volume(ModelObject* object, int volume_id)
{
    if (volume_id < 0 || static_cast<size_t>(volume_id) >= object->volumes.size() ||
        object->volumes[volume_id] == nullptr)
        throw std::runtime_error("Model volume was not found");
    return object->volumes[volume_id];
}

json bounding_box_to_json(const BoundingBoxf3& box)
{
    const Vec3d size = box.size();
    const Vec3d center = box.center();
    return {
        {"min_mm", {{"x", box.min.x()}, {"y", box.min.y()}, {"z", box.min.z()}}},
        {"max_mm", {{"x", box.max.x()}, {"y", box.max.y()}, {"z", box.max.z()}}},
        {"center_mm", {{"x", center.x()}, {"y", center.y()}, {"z", center.z()}}},
        {"size_mm", {{"x", size.x()}, {"y", size.y()}, {"z", size.z()}}}
    };
}

json mesh_stats_to_json(const TriangleMeshStats& stats, size_t facet_count)
{
    const RepairedMeshErrors& repaired = stats.repaired_errors;
    const int repaired_count = repaired.edges_fixed + repaired.degenerate_facets +
                               repaired.facets_removed + repaired.facets_reversed +
                               repaired.backwards_edges;
    return {
        {"facet_count", facet_count},
        {"volume_mm3", stats.volume},
        {"part_count", stats.number_of_parts},
        {"manifold", stats.manifold()},
        {"has_open_edges", stats.has_open_edges()},
        {"has_issues", stats.has_any_issue()},
        {"open_edges", stats.open_edges},
        {"non_manifold_edges", stats.non_manifold_edges},
        {"non_manifold_vertices", stats.non_manifold_vertices},
        {"repaired", stats.repaired()},
        {"repaired_error_count", repaired_count},
        {"repairs", {
            {"edges_fixed", repaired.edges_fixed},
            {"degenerate_facets", repaired.degenerate_facets},
            {"facets_removed", repaired.facets_removed},
            {"facets_reversed", repaired.facets_reversed},
            {"backwards_edges", repaired.backwards_edges}
        }}
    };
}

json volume_state_to_json(ModelObject* object, ModelVolume* volume, int volume_id)
{
    const TriangleMeshStats& stats = volume->mesh().stats();
    const Vec3d offset = volume->get_offset();
    const Vec3d rotation = volume->get_rotation();
    const Vec3d scale = volume->get_scaling_factor();
    const Vec3d mirror = volume->get_mirror();
    const bool inherits_extruder = !volume->config.has("extruder");
    const int extruder = inherits_extruder
        ? (object->config.has("extruder") ? object->config.extruder() : 1)
        : volume->config.extruder();
    return {
        {"volume_id", volume_id},
        {"name", volume->name},
        {"type", ModelVolume::type_to_string(volume->type())},
        {"extruder", extruder},
        {"inherits_object_extruder", inherits_extruder},
        {"mesh", mesh_stats_to_json(stats, volume->mesh().facets_count())},
        {"local_transform", {
            {"position_mm", {{"x", offset.x()}, {"y", offset.y()}, {"z", offset.z()}}},
            {"rotation_rad", {{"x", rotation.x()}, {"y", rotation.y()}, {"z", rotation.z()}}},
            {"scale", {{"x", scale.x()}, {"y", scale.y()}, {"z", scale.z()}}},
            {"mirror", {{"x", mirror.x()}, {"y", mirror.y()}, {"z", mirror.z()}}}
        }},
        {"bounding_box", bounding_box_to_json(volume->mesh().transformed_bounding_box(volume->get_matrix()))}
    };
}

json object_state_to_json(ModelObject* object, int object_id)
{
    // Some QIDI transform paths do not invalidate the ModelObject aggregate
    // bounding-box cache.  Instance bounds are live, so invalidate the
    // aggregate cache before reporting geometry as well.
    object->invalidate_bounding_box();
    json instances = json::array();
    for (size_t i = 0; i < object->instances.size(); ++i) {
        ModelInstance* instance = object->instances[i];
        if (instance == nullptr)
            continue;
        const Vec3d offset = instance->get_offset();
        const Vec3d rotation = instance->get_rotation();
        const Vec3d scale = instance->get_scaling_factor();
        const Vec3d mirror = instance->get_mirror();
        instances.push_back({
            {"instance_id", i},
            {"printable", instance->is_printable()},
            {"position_mm", {{"x", offset.x()}, {"y", offset.y()}, {"z", offset.z()}}},
            {"rotation_rad", {{"x", rotation.x()}, {"y", rotation.y()}, {"z", rotation.z()}}},
            {"scale", {{"x", scale.x()}, {"y", scale.y()}, {"z", scale.z()}}},
            {"mirror", {{"x", mirror.x()}, {"y", mirror.y()}, {"z", mirror.z()}}},
            {"bounding_box", bounding_box_to_json(object->instance_bounding_box(i))}
        });
    }
    return {
        {"object_id", object_id},
        {"name", object->name},
        {"printable", object->printable},
        {"instance_count", object->instances.size()},
        {"volume_count", object->volumes.size()},
        {"facet_count", object->facets_count()},
        {"part_count", object->parts_count()},
        {"cut", object->is_cut()},
        {"bounding_box", bounding_box_to_json(object->bounding_box())},
        {"instances", std::move(instances)}
    };
}

Preset::Type preset_type(const std::string& scope)
{
    if (scope == "print")
        return Preset::TYPE_PRINT;
    if (scope == "filament")
        return Preset::TYPE_FILAMENT;
    if (scope == "printer")
        return Preset::TYPE_PRINTER;
    throw std::runtime_error("scope must be print, filament, or printer");
}

PresetCollection& preset_collection(PresetBundle& bundle, Preset::Type type)
{
    switch (type) {
    case Preset::TYPE_PRINT:    return bundle.prints;
    case Preset::TYPE_FILAMENT: return bundle.filaments;
    case Preset::TYPE_PRINTER:  return bundle.printers;
    default: throw std::runtime_error("Unsupported preset type");
    }
}

Tab* require_tab(Preset::Type type)
{
    Tab* tab = wxGetApp().get_tab(type);
    if (tab == nullptr || tab->get_config() == nullptr)
        throw std::runtime_error("Requested settings tab is not available");
    return tab;
}

json preset_to_json(const Preset& preset, const std::string& active_name)
{
    return {
        {"name", preset.name},
        {"active", preset.name == active_name},
        {"visible", preset.is_visible},
        {"compatible", preset.is_compatible},
        {"system", preset.is_system},
        {"default", preset.is_default},
        {"user", preset.is_user()},
        {"project_embedded", preset.is_project_embedded}
    };
}

json map_to_json(const std::map<size_t, double>& values)
{
    json result = json::object();
    for (const auto& value : values)
        result[std::to_string(value.first)] = value.second;
    return result;
}

constexpr double MCP_PI = 3.14159265358979323846;

void repair_config_option_enum_map(const std::string& key, ConfigOption* option)
{
    if (option == nullptr)
        throw std::runtime_error("Setting is unavailable: " + key);

    const ConfigOptionDef* definition = print_config_def.get(key);
    const t_config_enum_values* enum_keys = definition != nullptr ? definition->enum_keys_map : nullptr;
    const auto require_enum_keys = [&]() {
        if (enum_keys == nullptr)
            throw std::runtime_error("Enum setting has no serialization map: " + key);
        return enum_keys;
    };

    if (auto* value = dynamic_cast<ConfigOptionEnumGeneric*>(option);
        value != nullptr && value->keys_map == nullptr)
        value->keys_map = require_enum_keys();
    if (auto* values = dynamic_cast<ConfigOptionEnumsGeneric*>(option);
        values != nullptr && values->keys_map == nullptr)
        values->keys_map = require_enum_keys();
    if (auto* values = dynamic_cast<ConfigOptionEnumsGenericNullable*>(option);
        values != nullptr && values->keys_map == nullptr)
        values->keys_map = require_enum_keys();
}

std::string serialize_config_option(const std::string& key, const ConfigOption* option)
{
    if (option == nullptr)
        throw std::runtime_error("Setting is unavailable: " + key);

    const ConfigOptionDef* definition = print_config_def.get(key);
    const t_config_enum_values* enum_keys = definition != nullptr ? definition->enum_keys_map : nullptr;

    if (const auto* value = dynamic_cast<const ConfigOptionEnumGeneric*>(option);
        value != nullptr && value->keys_map == nullptr) {
        if (enum_keys == nullptr)
            throw std::runtime_error("Enum setting has no serialization map: " + key);
        ConfigOptionEnumGeneric repaired(*value);
        repaired.keys_map = enum_keys;
        return repaired.serialize();
    }
    if (const auto* values = dynamic_cast<const ConfigOptionEnumsGeneric*>(option);
        values != nullptr && values->keys_map == nullptr) {
        if (enum_keys == nullptr)
            throw std::runtime_error("Enum-array setting has no serialization map: " + key);
        ConfigOptionEnumsGeneric repaired(*values);
        repaired.keys_map = enum_keys;
        return repaired.serialize();
    }
    if (const auto* values = dynamic_cast<const ConfigOptionEnumsGenericNullable*>(option);
        values != nullptr && values->keys_map == nullptr) {
        if (enum_keys == nullptr)
            throw std::runtime_error("Nullable enum-array setting has no serialization map: " + key);
        ConfigOptionEnumsGenericNullable repaired(*values);
        repaired.keys_map = enum_keys;
        return repaired.serialize();
    }
    return option->serialize();
}

json config_snapshot(const DynamicPrintConfig& config, const std::vector<std::string>& keys)
{
    json values = json::object();
    for (const std::string& key : keys) {
        const ConfigOption* option = config.option(key);
        values[key] = option != nullptr ? json(serialize_config_option(key, option)) : json(nullptr);
    }
    return values;
}

const char* config_option_type_name(ConfigOptionType type)
{
    switch (type) {
    case coFloat:            return "float";
    case coFloats:           return "float_array";
    case coInt:              return "integer";
    case coInts:             return "integer_array";
    case coString:           return "string";
    case coStrings:          return "string_array";
    case coPercent:          return "percent";
    case coPercents:         return "percent_array";
    case coFloatOrPercent:   return "float_or_percent";
    case coFloatsOrPercents: return "float_or_percent_array";
    case coPoint:            return "point";
    case coPoints:           return "point_array";
    case coPoint3:           return "point3";
    case coBool:             return "boolean";
    case coBools:            return "boolean_array";
    case coEnum:             return "enum";
    case coEnums:            return "enum_array";
    case coPointsGroups:     return "point_groups";
    case coIntsGroups:       return "integer_groups";
    default:                 return "unknown";
    }
}

const char* config_option_mode_name(ConfigOptionMode mode)
{
    switch (mode) {
    case comSimple:   return "simple";
    case comAdvanced: return "advanced";
    case comDevelop:  return "developer";
    default:          return "unknown";
    }
}

json setting_definition_to_json(const ConfigOptionDef& definition,
                                const json& supported_scopes,
                                const ConfigOption* effective,
                                const ConfigOption* local,
                                const std::string& value_source)
{
    json result = {
        {"key", definition.opt_key},
        {"type", config_option_type_name(definition.type)},
        {"vector", (static_cast<int>(definition.type) & static_cast<int>(coVectorType)) != 0},
        {"nullable", definition.nullable},
        {"read_only", definition.readonly},
        {"mode", config_option_mode_name(definition.mode)},
        {"label", definition.label},
        {"full_label", definition.full_label},
        {"description", definition.tooltip},
        {"category", definition.category},
        {"unit", definition.sidetext},
        {"ratio_over", definition.ratio_over.empty() ? json(nullptr) : json(definition.ratio_over)},
        {"default_value", definition.default_value ? json(serialize_config_option(definition.opt_key, definition.default_value.get())) : json(nullptr)},
        {"supported_scopes", supported_scopes},
        {"current_value", effective != nullptr ? json(serialize_config_option(definition.opt_key, effective)) : json(nullptr)},
        {"local_value", local != nullptr ? json(serialize_config_option(definition.opt_key, local)) : json(nullptr)},
        {"overridden", value_source == "object_override" || value_source == "volume_override"},
        {"value_source", value_source.empty() ? json(nullptr) : json(value_source)}
    };
    if (definition.min != ConfigOptionDef::min_default)
        result["minimum"] = definition.min;
    if (definition.max != ConfigOptionDef::max_default)
        result["maximum"] = definition.max;
    if (!definition.enum_values.empty()) {
        json values = json::array();
        for (size_t index = 0; index < definition.enum_values.size(); ++index) {
            const std::string label = index < definition.enum_labels.size() && !definition.enum_labels[index].empty()
                ? definition.enum_labels[index] : definition.enum_values[index];
            values.push_back({{"value", definition.enum_values[index]}, {"label", label}});
        }
        result["enum_values"] = std::move(values);
    }
    if (!definition.aliases.empty())
        result["aliases"] = definition.aliases;
    return result;
}

bool setting_text_matches(const ConfigOptionDef& definition, const std::string& query)
{
    if (query.empty())
        return true;
    const std::string haystack = lower_copy(definition.opt_key + "\n" + definition.label + "\n" +
        definition.full_label + "\n" + definition.category + "\n" + definition.tooltip);
    return haystack.find(query) != std::string::npos;
}

json introduced_validation_errors(const std::map<std::string, std::string>& baseline,
                                  const std::map<std::string, std::string>& candidate)
{
    json result = json::array();
    for (const auto& [key, message] : candidate) {
        const auto existing = baseline.find(key);
        if (existing == baseline.end() || existing->second != message)
            result.push_back({{"key", key}, {"message", message}});
    }
    return result;
}

double first_numeric_option(const DynamicPrintConfig& config, const std::string& key,
                            double fallback = 0.0)
{
    const ConfigOption* option = config.option(key);
    if (option == nullptr)
        return fallback;
    if (const auto* value = dynamic_cast<const ConfigOptionFloat*>(option))
        return value->value;
    if (const auto* value = dynamic_cast<const ConfigOptionPercent*>(option))
        return value->value;
    if (const auto* value = dynamic_cast<const ConfigOptionInt*>(option))
        return static_cast<double>(value->value);
    if (const auto* values = dynamic_cast<const ConfigOptionFloats*>(option))
        return values->values.empty() ? fallback : values->values.front();
    if (const auto* values = dynamic_cast<const ConfigOptionFloatsNullable*>(option))
        return values->values.empty() || std::isnan(values->values.front()) ? fallback : values->values.front();
    if (const auto* values = dynamic_cast<const ConfigOptionInts*>(option))
        return values->values.empty() ? fallback : static_cast<double>(values->values.front());
    return fallback;
}

std::string first_string_option(const DynamicPrintConfig& config, const std::string& key)
{
    const ConfigOption* option = config.option(key);
    if (option == nullptr)
        return {};
    if (const auto* value = dynamic_cast<const ConfigOptionString*>(option))
        return value->value;
    if (const auto* values = dynamic_cast<const ConfigOptionStrings*>(option))
        return values->values.empty() ? std::string() : values->values.front();
    return serialize_config_option(key, option);
}

json validation_errors_to_json(const std::map<std::string, std::string>& errors)
{
    json result = json::array();
    for (const auto& [key, message] : errors)
        result.push_back({{"key", key}, {"message", message}});
    return result;
}

json filament_profile_to_json(const Preset& preset, size_t filament_index)
{
    static const std::vector<std::string> keys{
        "filament_vendor", "filament_type", "filament_diameter", "filament_density",
        "filament_cost", "filament_max_volumetric_speed", "filament_flow_ratio",
        "nozzle_temperature", "nozzle_temperature_initial_layer",
        "nozzle_temperature_range_low", "nozzle_temperature_range_high",
        "textured_plate_temp", "textured_plate_temp_initial_layer",
        "hot_plate_temp", "hot_plate_temp_initial_layer", "chamber_temperatures",
        "filament_softening_temperature", "required_nozzle_HRC", "filament_retraction_length",
        "filament_retraction_speed", "filament_deretraction_speed", "filament_colour"
    };
    return {
        {"filament_index", filament_index},
        {"preset", preset.name},
        {"vendor", first_string_option(preset.config, "filament_vendor")},
        {"type", first_string_option(preset.config, "filament_type")},
        {"diameter_mm", first_numeric_option(preset.config, "filament_diameter")},
        {"density_g_cm3", first_numeric_option(preset.config, "filament_density")},
        {"cost_per_kg", first_numeric_option(preset.config, "filament_cost")},
        {"max_volumetric_speed_mm3_s", first_numeric_option(preset.config, "filament_max_volumetric_speed")},
        {"required_nozzle_hrc", first_numeric_option(preset.config, "required_nozzle_HRC")},
        {"settings", config_snapshot(preset.config, keys)}
    };
}

struct MeshGeometryMetrics {
    double surface_area_mm2{0.0};
    double downward_area_mm2{0.0};
    double severe_overhang_area_mm2{0.0};
    double bed_contact_area_mm2{0.0};
    size_t sampled_facets{0};
    size_t total_facets{0};
    size_t stride{1};
};

MeshGeometryMetrics calculate_mesh_metrics(const TriangleMesh& mesh, double overhang_angle_deg,
                                           double contact_tolerance_mm, size_t max_sample_facets)
{
    MeshGeometryMetrics metrics;
    const indexed_triangle_set& its = mesh.its;
    metrics.total_facets = its.indices.size();
    if (metrics.total_facets == 0)
        return metrics;

    if (max_sample_facets == 0)
        max_sample_facets = 1;
    metrics.stride = std::max<size_t>(1, (metrics.total_facets + max_sample_facets - 1) / max_sample_facets);
    const double min_z = mesh.bounding_box().min.z();

    for (size_t face_index = 0; face_index < its.indices.size(); face_index += metrics.stride) {
        const Vec3i& face = its.indices[face_index];
        const Vec3d a = its.vertices[face[0]].cast<double>();
        const Vec3d b = its.vertices[face[1]].cast<double>();
        const Vec3d c = its.vertices[face[2]].cast<double>();
        const Vec3d cross = (b - a).cross(c - a);
        const double twice_area = cross.norm();
        if (twice_area <= std::numeric_limits<double>::epsilon())
            continue;
        const double scaled_area = 0.5 * twice_area * static_cast<double>(metrics.stride);
        const Vec3d normal = cross / twice_area;
        metrics.surface_area_mm2 += scaled_area;
        if (normal.z() < 0.0) {
            metrics.downward_area_mm2 += scaled_area;
            const double angle = std::acos(std::clamp(-normal.z(), 0.0, 1.0)) * 180.0 / MCP_PI;
            if (angle <= overhang_angle_deg)
                metrics.severe_overhang_area_mm2 += scaled_area;
        }
        if (std::abs(a.z() - min_z) <= contact_tolerance_mm &&
            std::abs(b.z() - min_z) <= contact_tolerance_mm &&
            std::abs(c.z() - min_z) <= contact_tolerance_mm)
            metrics.bed_contact_area_mm2 += 0.5 * std::abs(cross.z()) * static_cast<double>(metrics.stride);
        ++metrics.sampled_facets;
    }
    return metrics;
}

MeshGeometryMetrics calculate_transformed_mesh_metrics(const TriangleMesh& mesh, const Transform3d& transform,
                                                       double overhang_angle_deg, double contact_tolerance_mm,
                                                       size_t max_sample_facets)
{
    MeshGeometryMetrics metrics;
    const indexed_triangle_set& its = mesh.its;
    metrics.total_facets = its.indices.size();
    if (metrics.total_facets == 0)
        return metrics;
    if (max_sample_facets == 0)
        max_sample_facets = 1;
    metrics.stride = std::max<size_t>(1, (metrics.total_facets + max_sample_facets - 1) / max_sample_facets);
    const double min_z = mesh.transformed_bounding_box(transform).min.z();
    for (size_t face_index = 0; face_index < its.indices.size(); face_index += metrics.stride) {
        const Vec3i& face = its.indices[face_index];
        const Vec3d a = transform * its.vertices[face[0]].cast<double>();
        const Vec3d b = transform * its.vertices[face[1]].cast<double>();
        const Vec3d c = transform * its.vertices[face[2]].cast<double>();
        const Vec3d cross = (b - a).cross(c - a);
        const double twice_area = cross.norm();
        if (twice_area <= std::numeric_limits<double>::epsilon())
            continue;
        const double scaled_area = 0.5 * twice_area * static_cast<double>(metrics.stride);
        const Vec3d normal = cross / twice_area;
        metrics.surface_area_mm2 += scaled_area;
        if (normal.z() < 0.0) {
            metrics.downward_area_mm2 += scaled_area;
            const double angle = std::acos(std::clamp(-normal.z(), 0.0, 1.0)) * 180.0 / MCP_PI;
            if (angle <= overhang_angle_deg)
                metrics.severe_overhang_area_mm2 += scaled_area;
        }
        if (std::abs(a.z() - min_z) <= contact_tolerance_mm &&
            std::abs(b.z() - min_z) <= contact_tolerance_mm &&
            std::abs(c.z() - min_z) <= contact_tolerance_mm)
            metrics.bed_contact_area_mm2 += 0.5 * std::abs(cross.z()) * static_cast<double>(metrics.stride);
        ++metrics.sampled_facets;
    }
    return metrics;
}

json mesh_metrics_to_json(const MeshGeometryMetrics& metrics, double overhang_angle_deg,
                          double contact_tolerance_mm)
{
    return {
        {"surface_area_mm2", metrics.surface_area_mm2},
        {"downward_facing_area_mm2", metrics.downward_area_mm2},
        {"severe_overhang_area_mm2", metrics.severe_overhang_area_mm2},
        {"overhang_angle_from_downward_vertical_deg", overhang_angle_deg},
        {"bed_contact_area_mm2", metrics.bed_contact_area_mm2},
        {"contact_tolerance_mm", contact_tolerance_mm},
        {"total_facets", metrics.total_facets},
        {"sampled_facets", metrics.sampled_facets},
        {"sampling_stride", metrics.stride},
        {"approximate", metrics.stride > 1}
    };
}

TriangleMesh transformed_instance_mesh(ModelObject* object, ModelInstance* instance)
{
    TriangleMesh mesh = object->raw_mesh();
    instance->transform_mesh(&mesh);
    return mesh;
}

json bounding_box_relationship(const BoundingBoxf3& first, const BoundingBoxf3& second)
{
    json overlap = json::object();
    json clearance = json::object();
    double squared_distance = 0.0;
    bool intersects = true;
    const std::array<const char*, 3> axes{{"x", "y", "z"}};
    for (int axis = 0; axis < 3; ++axis) {
        const double overlap_value = std::max(0.0, std::min(first.max[axis], second.max[axis]) -
                                                    std::max(first.min[axis], second.min[axis]));
        const double gap = std::max({0.0, second.min[axis] - first.max[axis], first.min[axis] - second.max[axis]});
        overlap[axes[axis]] = overlap_value;
        clearance[axes[axis]] = gap;
        squared_distance += gap * gap;
        intersects = intersects && overlap_value > 0.0;
    }
    const Vec3d overlap_size(overlap["x"].get<double>(), overlap["y"].get<double>(), overlap["z"].get<double>());
    return {
        {"aabb_intersects", intersects},
        {"overlap_mm", std::move(overlap)},
        {"overlap_volume_mm3", overlap_size.x() * overlap_size.y() * overlap_size.z()},
        {"axis_clearance_mm", std::move(clearance)},
        {"minimum_aabb_distance_mm", std::sqrt(squared_distance)},
        {"method", "axis_aligned_bounding_boxes"}
    };
}

struct OrientationSpec {
    const char* id;
    Vec3d rotation;
};

const std::array<OrientationSpec, 6>& orientation_specs()
{
    static const std::array<OrientationSpec, 6> specs{{
        {"z_up", Vec3d(0.0, 0.0, 0.0)},
        {"z_down", Vec3d(MCP_PI, 0.0, 0.0)},
        {"x_up", Vec3d(0.0, -MCP_PI * 0.5, 0.0)},
        {"x_down", Vec3d(0.0, MCP_PI * 0.5, 0.0)},
        {"y_up", Vec3d(MCP_PI * 0.5, 0.0, 0.0)},
        {"y_down", Vec3d(-MCP_PI * 0.5, 0.0, 0.0)}
    }};
    return specs;
}

const OrientationSpec& require_orientation_spec(const std::string& id)
{
    for (const OrientationSpec& spec : orientation_specs())
        if (id == spec.id)
            return spec;
    throw std::runtime_error("candidate_id must be z_up, z_down, x_up, x_down, y_up, or y_down");
}

const char* build_volume_state_name(BuildVolume::ObjectState state)
{
    switch (state) {
    case BuildVolume::ObjectState::Inside: return "inside";
    case BuildVolume::ObjectState::Colliding: return "colliding";
    case BuildVolume::ObjectState::Outside: return "outside";
    case BuildVolume::ObjectState::Below: return "below";
    case BuildVolume::ObjectState::Limited: return "limited";
    }
    return "unknown";
}

json list_printers(QDSDeviceManager* manager)
{
    json printers = json::array();
    if (manager == nullptr)
        return {{"printers", printers}};

    // snapshotDevices() holds the manager lock only while copying shared_ptrs.
    // Copy the displayed values immediately and do not retain device pointers.
    for (const auto& entry : manager->snapshotDevices()) {
        const auto& map_id = entry.first;
        const auto& device = entry.second;
        if (!device)
            continue;

        printers.push_back({
            {"id", map_id},
            {"device_id", device->m_id},
            {"name", device->m_name},
            {"ip", device->m_ip},
            {"type", device->m_type},
            {"online", device->is_online()},
            {"selected", device->is_selected.load()},
            {"network_device", device->is_net_device},
            {"status", device->m_status},
            {"print_state", device->m_print_state},
            {"filename", device->m_print_filename},
            {"progress", device->m_print_progress},
            {"progress_fraction", device->m_print_progress_float},
            {"plate_index", device->m_plate_index},
            {"layer", {
                {"current", device->m_print_cur_layer},
                {"total", device->m_print_total_layer}
            }},
            {"temperature", {
                {"nozzle", device->m_extruder_temperature},
                {"nozzle_target", device->m_target_extruder},
                {"bed", device->m_bed_temperature},
                {"bed_target", device->m_target_bed},
                {"chamber", device->m_chamber_temperature},
                {"chamber_target", device->m_target_chamber}
            }}
        });
    }

    std::sort(printers.begin(), printers.end(), [](const json& lhs, const json& rhs) {
        const auto lhs_name = lhs.value("name", "");
        const auto rhs_name = rhs.value("name", "");
        return lhs_name == rhs_name ? lhs.value("id", "") < rhs.value("id", "")
                                    : lhs_name < rhs_name;
    });
    return {{"printers", std::move(printers)}};
}

json get_plate_state()
{
    return on_gui_thread([]() {
        auto* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        const auto origin = plate->get_origin();
        const auto size   = plate->get_size();
        json instances = json::array();
        for (const auto& item : plate->get_obj_and_inst_set()) {
            ModelInstance* instance = plate->get_instance(item.first, item.second);
            if (instance == nullptr)
                continue;
            ModelObject* object = instance->get_object();
            const auto offset   = instance->get_offset();
            const auto rotation = instance->get_rotation();
            const auto scale    = instance->get_scaling_factor();
            instances.push_back({
                {"object_id", item.first}, {"instance_id", item.second},
                {"name", object != nullptr ? object->name : ""},
                {"printable", instance->is_printable()},
                {"position_mm", {{"x", offset.x()}, {"y", offset.y()}, {"z", offset.z()}}},
                {"rotation_rad", {{"x", rotation.x()}, {"y", rotation.y()}, {"z", rotation.z()}}},
                {"scale", {{"x", scale.x()}, {"y", scale.y()}, {"z", scale.z()}}}
            });
        }
        return json{{"plate", {
            {"index", plate->get_index()}, {"name", plate->get_plate_name()},
            {"empty", plate->empty()}, {"locked", plate->is_locked()}, {"can_slice", plate->can_slice()},
            {"origin_mm", {{"x", origin.x()}, {"y", origin.y()}}},
            {"size_mm", {{"x", size.x()}, {"y", size.y()}}},
            {"instances", std::move(instances)}
        }}};
    });
}

const char* print_sequence_name(PrintSequence sequence)
{
    switch (sequence) {
    case PrintSequence::ByDefault: return "default";
    case PrintSequence::ByLayer:   return "by_layer";
    case PrintSequence::ByObject:  return "by_object";
    }
    return "unknown";
}

json print_object_sequence_payload(Plater* plater, PartPlate* plate)
{
    json objects = json::array();
    int order = 1;
    size_t total_instances = 0;
    for (size_t object_id = 0; object_id < plater->model().objects.size(); ++object_id) {
        ModelObject* object = plater->model().objects[object_id];
        if (object == nullptr)
            continue;
        std::vector<int> instance_ids;
        for (size_t instance_id = 0; instance_id < object->instances.size(); ++instance_id) {
            ModelInstance* instance = object->instances[instance_id];
            if (instance != nullptr && instance->is_printable() &&
                (plate->contain_instance(static_cast<int>(object_id), static_cast<int>(instance_id)) ||
                 plate->contain_instance_totally(object, static_cast<int>(instance_id))))
                instance_ids.push_back(static_cast<int>(instance_id));
        }
        if (instance_ids.empty())
            continue;
        std::sort(instance_ids.begin(), instance_ids.end(), [object](int lhs, int rhs) {
            const int lhs_order = object->instances[lhs]->arrange_order;
            const int rhs_order = object->instances[rhs]->arrange_order;
            return lhs_order == rhs_order ? lhs < rhs : lhs_order < rhs_order;
        });
        json instances = json::array();
        for (int instance_id : instance_ids) {
            ModelInstance* instance = object->instances[instance_id];
            instances.push_back({{"instance_id", instance_id},
                                 {"stable_instance_id", instance->id().id},
                                 {"arrange_order", instance->arrange_order}});
        }
        total_instances += instance_ids.size();
        objects.push_back({{"order", order++}, {"object_id", object_id},
                           {"stable_object_id", object != nullptr ? json(object->id().id) : json(nullptr)},
                           {"name", object != nullptr ? object->name : ""},
                           {"instances", std::move(instances)}});
    }
    bool same_as_global = false;
    return {{"plate_index", plate->get_index()},
            {"configured_mode", print_sequence_name(plate->get_print_seq())},
            {"effective_mode", print_sequence_name(plate->get_real_print_seq(&same_as_global))},
            {"same_as_global", same_as_global},
            {"instance_count", total_instances},
            {"order_basis", "model_object_order"},
            {"instance_order_basis", "arrange_order"},
            {"identity_note", "object_id is the current model-vector index and may change after native reordering; stable_object_id tracks object identity across the operation"},
            {"objects", std::move(objects)}};
}

std::string print_job_fingerprint(Plater* plater, PartPlate* plate)
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        throw std::runtime_error("Preset bundle is not available");

    std::ostringstream state;
    state << std::setprecision(17)
          << "plate=" << plate->get_index()
          << ";mode=" << static_cast<int>(plate->get_real_print_seq())
          << ";slice_valid=" << plate->is_slice_result_valid()
          << ";slice_ready=" << plate->is_slice_result_ready_for_print();

    for (const auto& item : plate->get_obj_and_inst_set()) {
        ModelObject* object = require_model_object(plater, item.first);
        ModelInstance* instance = require_instance(plate, item.first, item.second);
        state << ";object=" << object->id().id
              << ",instance=" << item.second
              << ",printable=" << instance->is_printable()
              << ",facets=" << object->facets_count()
              << ",arrange=" << instance->arrange_order;
        const auto matrix = instance->get_matrix();
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column)
                state << ',' << matrix(row, column);
    }

    DynamicPrintConfig full = bundle->full_config();
    std::vector<std::string> keys = full.keys();
    std::sort(keys.begin(), keys.end());
    for (const std::string& key : keys) {
        const ConfigOption* option = full.option(key);
        if (option != nullptr)
            state << ";config:" << key << '=' << serialize_config_option(key, option);
    }

    if (GCodeProcessorResult* result = plate->get_slice_result(); result != nullptr) {
        std::lock_guard<std::mutex> lock(result->result_mutex);
        const auto& normal = result->print_statistics.modes[
            static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
        state << ";gcode=" << result->filename
              << ";moves=" << result->moves.size()
              << ";layers=" << normal.layers_times.size()
              << ";initial_layer_time=" << result->initial_layer_time
              << ";gcode_check=" << static_cast<int>(result->gcode_check_result.error_code)
              << ";warnings=" << result->warnings.size();
        for (const auto& entry : result->print_statistics.total_volumes_per_extruder)
            state << ";volume:" << entry.first << '=' << entry.second;
    }

    const std::string serialized = state.str();
    const std::size_t first = std::hash<std::string>{}(serialized);
    const std::size_t second = std::hash<std::string>{}("qidi-mcp-v1.3:" + serialized);
    std::ostringstream fingerprint;
    fingerprint << std::hex << std::setfill('0')
                << std::setw(static_cast<int>(sizeof(std::size_t) * 2)) << first
                << std::setw(static_cast<int>(sizeof(std::size_t) * 2)) << second;
    return fingerprint.str();
}

std::string packaged_upload_name(Plater* plater, int plate_index)
{
    std::string name = into_u8(plater->get_export_gcode_filename("", true, false));
    name = boost::filesystem::path(name).filename().string();
    if (name.empty())
        name = "qidi_mcp_plate_" + std::to_string(plate_index + 1);
    const std::string lowered = lower_copy(name);
    if (lowered.size() >= 11 && lowered.substr(lowered.size() - 11) == ".gcode.3mf")
        name.erase(name.size() - 11);
    else if (lowered.size() >= 6 && lowered.substr(lowered.size() - 6) == ".gcode")
        name.erase(name.size() - 6);
    else if (lowered.size() >= 4 && lowered.substr(lowered.size() - 4) == ".3mf")
        name.erase(name.size() - 4);
    return name + ".gcode.3mf";
}

std::string normalized_print_filename(std::string name)
{
    name = lower_copy(boost::filesystem::path(name).filename().string());
    for (const char* suffix : {".gcode.3mf", ".gcode", ".3mf"}) {
        const std::size_t length = std::strlen(suffix);
        if (name.size() >= length && name.substr(name.size() - length) == suffix) {
            name.erase(name.size() - length);
            break;
        }
    }
    return name;
}

bool print_filename_matches(const std::string& expected, const std::string& reported)
{
    const std::string left = normalized_print_filename(expected);
    const std::string right = normalized_print_filename(reported);
    return !left.empty() && !right.empty() && left == right;
}

json get_print_object_sequence()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        return print_object_sequence_payload(plater, require_plate(plater));
    });
}

json set_print_object_sequence(const json& args)
{
    if (!args.contains("object_ids") || !args["object_ids"].is_array())
        return {{"error", "object_ids must be an array containing every printable object on the active plate"}};
    std::vector<int> requested;
    std::set<int> unique;
    for (const json& value : args["object_ids"]) {
        if (!value.is_number_integer() || value.get<int>() < 0)
            return {{"error", "Every object_ids entry must be a non-negative integer"}};
        const int object_id = value.get<int>();
        if (!unique.insert(object_id).second)
            return {{"error", "object_ids must not contain duplicates"}};
        requested.push_back(object_id);
    }
    const bool enable_by_object = args.value("enable_by_object", true);

    return on_gui_thread([requested, enable_by_object]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        PartPlate* plate = require_plate(plater);
        std::set<int> active_ids;
        for (const auto& item : plate->get_obj_and_inst_set()) {
            ModelInstance* instance = plate->get_instance(item.first, item.second);
            if (instance != nullptr && instance->is_printable())
                active_ids.insert(item.first);
        }
        if (requested.size() != active_ids.size() ||
            !std::all_of(requested.begin(), requested.end(), [&](int id) { return active_ids.count(id) != 0; }))
            throw std::runtime_error("object_ids must be an exact permutation of every printable object on the active plate");

        std::vector<ModelObject*> ordered_objects;
        ordered_objects.reserve(requested.size());
        for (int object_id : requested)
            ordered_objects.push_back(require_model_object(plater, object_id));

        json requested_stable_ids = json::array();
        for (const ModelObject* object : ordered_objects)
            requested_stable_ids.push_back(object->id().id);

        std::vector<int> current_order(active_ids.begin(), active_ids.end());
        const bool object_ids_reindexed = requested != current_order;

        plater->take_snapshot("MCP Set Print Object Sequence");
        std::set<ModelObject*> active_objects(ordered_objects.begin(), ordered_objects.end());
        size_t next = 0;
        for (ModelObject*& slot : plater->model().objects) {
            if (active_objects.count(slot) != 0)
                slot = ordered_objects[next++];
        }
        int arrange_order = 1;
        for (ModelObject* object : ordered_objects) {
            for (ModelInstance* instance : object->instances) {
                if (instance != nullptr)
                    instance->arrange_order = arrange_order++;
            }
        }

        const int plate_index = plate->get_index();
        plater->get_partplate_list().reload_all_objects(false, plate_index);
        plate = plater->get_partplate_list().get_curr_plate();
        if (plate == nullptr)
            throw std::runtime_error("Active plate was lost while updating print order");
        if (enable_by_object)
            plate->set_print_seq(PrintSequence::ByObject);
        std::vector<size_t> changed(plater->model().objects.size());
        for (size_t i = 0; i < changed.size(); ++i)
            changed[i] = i;
        plater->changed_objects(changed);
        plater->object_list_changed();
        plater->schedule_background_process();
        json result = print_object_sequence_payload(plater, plate);
        result["changed"] = true;
        result["undo_snapshot"] = true;
        result["requested_object_ids"] = requested;
        result["requested_stable_object_ids"] = std::move(requested_stable_ids);
        result["object_ids_reindexed"] = object_ids_reindexed;
        result["native_order_applied"] = true;
        result["clearance_validation_required"] = true;
        result["next_tool"] = "validate_print_by_object";
        return result;
    });
}

json validate_print_by_object()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        PartPlate* plate = require_plate(plater);
        const PrintSequence mode = plate->get_real_print_seq();
        if (mode != PrintSequence::ByObject)
            return json{{"applicable", false}, {"valid", false},
                        {"effective_mode", print_sequence_name(mode)},
                        {"message", "The active plate is not configured for print by object"}};
        const bool plate_apply_invalid = plate->is_apply_result_invalid();
        Print* print = plate->fff_print();
        if (print == nullptr)
            throw std::runtime_error("The active plate has no FFF print state to validate");

        Polygons collision_polygons;
        std::vector<std::pair<Polygon, float>> height_polygons;
        const StringObjectException issue =
            Print::sequential_print_clearance_valid(*print, &collision_polygons, &height_polygons);
        int object_id = -1;
        if (issue.object != nullptr) {
            for (size_t i = 0; i < plater->model().objects.size(); ++i) {
                if (plater->model().objects[i] == issue.object) {
                    object_id = static_cast<int>(i);
                    break;
                }
            }
        }
        const bool valid = !plate_apply_invalid && issue.string.empty() &&
                           collision_polygons.empty() && height_polygons.empty();
        json result{{"applicable", true}, {"valid", valid},
                    {"effective_mode", "by_object"},
                    {"plate_apply_invalid", plate_apply_invalid},
                    {"message", !issue.string.empty() ? issue.string :
                        (plate_apply_invalid ? "QIDI rejected the sequential layout; change object order or placement" : "")},
                    {"horizontal_collision_polygon_count", collision_polygons.size()},
                    {"height_collision_polygon_count", height_polygons.size()},
                    {"native_validator", "Print::sequential_print_clearance_valid"}};
        result["object_id"] = object_id >= 0 ? json(object_id) : json(nullptr);
        return result;
    });
}

json list_objects()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        json objects = json::array();
        for (size_t i = 0; i < plater->model().objects.size(); ++i) {
            ModelObject* object = plater->model().objects[i];
            if (object != nullptr)
                objects.push_back(object_state_to_json(object, static_cast<int>(i)));
        }
        return json{{"count", objects.size()}, {"objects", std::move(objects)}};
    });
}

json get_object_state(const json& args)
{
    const int object_id = required_int(args, "object_id");
    return on_gui_thread([object_id]() {
        Plater* plater = require_plater();
        return json{{"object", object_state_to_json(require_model_object(plater, object_id), object_id)}};
    });
}

json get_model_diagnostics(const json& args)
{
    int requested_object_id = -1;
    if (args.contains("object_id")) {
        if (!args["object_id"].is_number_integer() || args["object_id"].get<int>() < 0)
            return {{"error", "object_id must be a non-negative integer when provided"}};
        requested_object_id = args["object_id"].get<int>();
    }

    return on_gui_thread([requested_object_id]() {
        Plater* plater = require_plater();
        json objects = json::array();
        bool has_mesh_errors = false;
        bool has_open_edges = false;

        const size_t begin = requested_object_id >= 0 ? static_cast<size_t>(requested_object_id) : 0;
        const size_t end = requested_object_id >= 0 ? begin + 1 : plater->model().objects.size();
        if (requested_object_id >= 0)
            require_model_object(plater, requested_object_id);

        for (size_t object_id = begin; object_id < end; ++object_id) {
            ModelObject* object = plater->model().objects[object_id];
            if (object == nullptr)
                continue;
            const TriangleMeshStats stats = object->get_object_stl_stats();
            has_mesh_errors = has_mesh_errors || !stats.manifold();
            has_open_edges = has_open_edges || stats.has_open_edges();

            json volumes = json::array();
            for (size_t volume_id = 0; volume_id < object->volumes.size(); ++volume_id) {
                ModelVolume* volume = object->volumes[volume_id];
                if (volume != nullptr)
                    volumes.push_back(volume_state_to_json(object, volume, static_cast<int>(volume_id)));
            }
            objects.push_back({
                {"object_id", object_id},
                {"name", object->name},
                {"cut", object->is_cut()},
                {"mesh", mesh_stats_to_json(stats, object->facets_count())},
                {"volumes", std::move(volumes)}
            });
        }

        PartPlate* plate = require_plate(plater);
        const bool slice_valid = plate->is_slice_result_valid();
        json slice_warnings = json::array();
        bool floating_regions = false;
        if (slice_valid) {
            if (GCodeProcessorResult* result = plate->get_slice_result(); result != nullptr) {
                std::lock_guard<std::mutex> lock(result->result_mutex);
                for (const auto& warning : result->warnings) {
                    const std::string message_lower = lower_copy(warning.msg);
                    if (message_lower.find("floating") != std::string::npos ||
                        message_lower.find("levitating") != std::string::npos)
                        floating_regions = true;
                    slice_warnings.push_back({
                        {"level", warning.level},
                        {"message", warning.msg},
                        {"error_code", warning.error_code},
                        {"params", warning.params}
                    });
                }
            }
        }

        return json{
            {"object_filter", requested_object_id >= 0 ? json(requested_object_id) : json(nullptr)},
            {"objects", std::move(objects)},
            {"summary", {
                {"has_mesh_errors", has_mesh_errors},
                {"has_open_edges", has_open_edges},
                {"slice_result_valid", slice_valid},
                {"has_slice_warnings", !slice_warnings.empty()},
                {"floating_regions", floating_regions},
                {"print_attention_required", has_mesh_errors || has_open_edges || !slice_warnings.empty()}
            }},
            {"active_plate", {
                {"plate_index", plate->get_index()},
                {"slice_warnings", std::move(slice_warnings)}
            }}
        };
    });
}

const char* print_step_name(int step)
{
    static const std::array<const char*, psCount> names{{
        "wipe_tower_or_tool_ordering", "skirt_brim_or_slicing_finished",
        "gcode_export", "conflict_check"
    }};
    return step >= 0 && step < psCount ? names[static_cast<size_t>(step)] : "unknown";
}

const char* print_object_step_name(int step)
{
    static const std::array<const char*, posCount> names{{
        "slice", "perimeters", "prepare_infill", "infill", "ironing",
        "support_material", "detect_overhangs_for_lift", "simplify_wall",
        "simplify_infill", "simplify_support_path"
    }};
    return step >= 0 && step < posCount ? names[static_cast<size_t>(step)] : "unknown";
}

json get_slicing_warnings(const json& args)
{
    const bool include_stale = args.value("include_stale", false);
    return on_gui_thread([include_stale]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        Print& print = plater->get_partplate_list().get_current_fff_print();
        json warnings = json::array();
        size_t critical_count = 0;

        const auto append_warning = [&](const PrintStateBase::Warning& warning, const char* source,
                                        const char* step, int object_id, const std::string& object_name) {
            if (!include_stale && !warning.current)
                return;
            const bool critical = warning.level == PrintStateBase::WarningLevel::CRITICAL;
            if (critical)
                ++critical_count;
            warnings.push_back({
                {"source", source}, {"step", step},
                {"object_id", object_id >= 0 ? json(object_id) : json(nullptr)},
                {"object_name", object_name.empty() ? json(nullptr) : json(object_name)},
                {"level", critical ? "critical" : "warning"}, {"current", warning.current},
                {"message_id", warning.message_id}, {"message", warning.message}
            });
        };

        for (int step = 0; step < psCount; ++step) {
            const auto state = print.step_state_with_warnings(static_cast<PrintStep>(step));
            for (const auto& warning : state.warnings)
                append_warning(warning, "print", print_step_name(step), -1, "");
        }
        for (const PrintObject* print_object : print.objects()) {
            if (print_object == nullptr || print_object->model_object() == nullptr)
                continue;
            const ModelObject* model_object = print_object->model_object();
            int object_id = -1;
            for (size_t i = 0; i < plater->model().objects.size(); ++i)
                if (plater->model().objects[i] == model_object) { object_id = static_cast<int>(i); break; }
            for (int step = 0; step < posCount; ++step) {
                const auto state = print_object->step_state_with_warnings(static_cast<PrintObjectStep>(step));
                for (const auto& warning : state.warnings)
                    append_warning(warning, "print_object", print_object_step_name(step),
                                   object_id, model_object->name);
            }
        }

        if (GCodeProcessorResult* result = plate->get_slice_result(); result != nullptr) {
            const bool slice_valid = plate->is_slice_result_valid();
            std::lock_guard<std::mutex> lock(result->result_mutex);
            for (const auto& warning : result->warnings)
                warnings.push_back({
                    {"source", "gcode"}, {"step", "gcode_result"}, {"object_id", nullptr},
                    {"object_name", nullptr}, {"level", warning.level > 0 ? "warning" : "info"},
                    {"current", slice_valid}, {"message", warning.msg},
                    {"error_code", warning.error_code}, {"params", warning.params}
                });
        }
        return json{{"plate_index", plate->get_index()},
                    {"slicing", plater->is_background_process_slicing()},
                    {"slice_result_valid", plate->is_slice_result_valid()},
                    {"warning_count", warnings.size()}, {"critical_count", critical_count},
                    {"warnings", std::move(warnings)}};
    });
}

json list_object_volumes(const json& args)
{
    const int object_id = required_int(args, "object_id");
    return on_gui_thread([object_id]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        json volumes = json::array();
        for (size_t volume_id = 0; volume_id < object->volumes.size(); ++volume_id) {
            ModelVolume* volume = object->volumes[volume_id];
            if (volume != nullptr)
                volumes.push_back(volume_state_to_json(object, volume, static_cast<int>(volume_id)));
        }
        return json{{"object_id", object_id}, {"name", object->name},
                    {"count", volumes.size()}, {"volumes", std::move(volumes)}};
    });
}

json repair_object_mesh(const json& args)
{
    const int object_id = required_int(args, "object_id");
    if (!args.value("confirm", false))
        return {{"error", "Mesh repair can alter geometry and remove painted facets; pass confirm=true to continue"}};

    return on_gui_thread([object_id]() {
#ifndef __WXMSW__
        throw std::runtime_error("QIDI Studio's native mesh repair is available only on Windows");
#else
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ObjectList* object_list = wxGetApp().obj_list();
        if (object_list == nullptr)
            throw std::runtime_error("QIDI object list is not available");

        const json before = mesh_stats_to_json(object->get_object_stl_stats(), object->facets_count());
        ObjectVolumeID selection;
        selection.object = object;
        object_list->select_item(selection);
        object_list->fix_through_netfabb();

        object = require_model_object(plater, object_id);
        const TriangleMeshStats after_stats = object->get_object_stl_stats();
        return json{
            {"repair_completed", true},
            {"object_id", object_id},
            {"before", before},
            {"after", mesh_stats_to_json(after_stats, object->facets_count())},
            {"issues_remaining", after_stats.has_any_issue()},
            {"undo_available", plater->can_undo()}
        };
#endif
    });
}

json center_object(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    return on_gui_thread([object_id, instance_id]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        PartPlate* plate = require_plate(plater);
        ModelInstance* instance = require_instance(plate, object_id, instance_id);
        ModelObject* object = instance->get_object();
        if (object == nullptr)
            throw std::runtime_error("Object was not found");

        const BoundingBoxf3 box = object->instance_bounding_box(static_cast<size_t>(instance_id));
        const Vec3d origin = plate->get_origin();
        const Vec2d size = plate->get_size();
        const Vec2d target(origin.x() + size.x() * 0.5, origin.y() + size.y() * 0.5);
        Vec3d offset = instance->get_offset();
        offset.x() += target.x() - box.center().x();
        offset.y() += target.y() - box.center().y();

        plater->take_snapshot("MCP Center Object");
        instance->set_offset(offset);
        plater->get_partplate_list().notify_instance_update(object_id, instance_id);
        plater->changed_object(object_id);
        offset = instance->get_offset();
        return json{{"object_id", object_id}, {"instance_id", instance_id},
                    {"position_mm", {{"x", offset.x()}, {"y", offset.y()}, {"z", offset.z()}}},
                    {"bounding_box", bounding_box_to_json(object->instance_bounding_box(static_cast<size_t>(instance_id)))}};
    });
}

json drop_object_to_bed(const json& args)
{
    const int object_id = required_int(args, "object_id");
    return on_gui_thread([object_id]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        plater->take_snapshot("MCP Drop Object To Bed");
        object->ensure_on_bed(false);
        plater->changed_object(object_id);
        for (size_t instance_id = 0; instance_id < object->instances.size(); ++instance_id)
            if (object->instances[instance_id] != nullptr)
                plater->get_partplate_list().notify_instance_update(object_id, static_cast<int>(instance_id));
        return json{{"object_id", object_id}, {"bounding_box", bounding_box_to_json(object->bounding_box())}};
    });
}

json mirror_object(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    const std::string axis_name = args.value("axis", "x");
    Axis axis;
    if (axis_name == "x") axis = X;
    else if (axis_name == "y") axis = Y;
    else if (axis_name == "z") axis = Z;
    else return {{"error", "axis must be x, y, or z"}};

    return on_gui_thread([object_id, instance_id, axis, axis_name]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        ModelObject* object = instance->get_object();
        if (object == nullptr)
            throw std::runtime_error("Object was not found");

        Vec3d mirror = instance->get_mirror();
        mirror[static_cast<int>(axis)] *= -1.0;
        plater->take_snapshot("MCP Mirror Object");
        instance->set_mirror(mirror);
        object->invalidate_bounding_box();
        plater->get_partplate_list().notify_instance_update(object_id, instance_id);
        plater->changed_object(object_id);
        mirror = instance->get_mirror();
        return json{{"object_id", object_id}, {"instance_id", instance_id}, {"axis", axis_name},
                    {"mirror", {{"x", mirror.x()}, {"y", mirror.y()}, {"z", mirror.z()}}},
                    {"bounding_box", bounding_box_to_json(object->instance_bounding_box(static_cast<size_t>(instance_id)))}};
    });
}

json cut_object_horizontal(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    if (!args.contains("z_mm") || !args["z_mm"].is_number())
        return {{"error", "Missing numeric parameter: z_mm"}};
    const double z_mm = args["z_mm"].get<double>();
    const std::string keep = args.value("keep", "both");
    if (keep != "upper" && keep != "lower" && keep != "both")
        return {{"error", "keep must be upper, lower, or both"}};
    if (!args.value("confirm", false))
        return {{"error", "Horizontal cut replaces the source object; pass confirm=true to continue"}};

    return on_gui_thread([object_id, instance_id, z_mm, keep]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        ModelObject* object = instance->get_object();
        if (object == nullptr)
            throw std::runtime_error("Object was not found");
        object->invalidate_bounding_box();
        const BoundingBoxf3 bounds = object->instance_bounding_box(static_cast<size_t>(instance_id));
        if (z_mm <= bounds.min.z() || z_mm >= bounds.max.z())
            throw std::runtime_error("z_mm must lie strictly inside the selected instance bounding box");

        ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper;
        if (keep == "lower")
            attributes = ModelObjectCutAttribute::KeepLower;
        else if (keep == "both")
            attributes = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower;

        const size_t before_count = plater->model().objects.size();
        plater->take_snapshot("MCP Horizontal Cut");
        plater->cut_horizontal(static_cast<size_t>(object_id), static_cast<size_t>(instance_id),
                               z_mm, attributes);

        json objects = json::array();
        for (size_t i = 0; i < plater->model().objects.size(); ++i) {
            ModelObject* current = plater->model().objects[i];
            if (current != nullptr)
                objects.push_back(object_state_to_json(current, static_cast<int>(i)));
        }
        return json{{"cut", true}, {"source_object_id", object_id}, {"z_mm", z_mm}, {"keep", keep},
                    {"object_count_before", before_count},
                    {"object_count_after", plater->model().objects.size()},
                    {"objects", std::move(objects)}};
    });
}

json undo_project_change()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        if (!plater->can_undo())
            throw std::runtime_error("There is no QIDI Studio change to undo");
        plater->undo();
        return json{{"undone", true}, {"can_undo", plater->can_undo()}, {"can_redo", plater->can_redo()},
                    {"object_count", plater->model().objects.size()}};
    });
}

json redo_project_change()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        if (!plater->can_redo())
            throw std::runtime_error("There is no QIDI Studio change to redo");
        plater->redo();
        return json{{"redone", true}, {"can_undo", plater->can_undo()}, {"can_redo", plater->can_redo()},
                    {"object_count", plater->model().objects.size()}};
    });
}

json get_object_settings(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const json keys = args.value("keys", json::array());
    if (!keys.is_array())
        return {{"error", "keys must be an array when provided"}};
    return on_gui_thread([object_id, keys]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        DynamicPrintConfig* global = require_tab(Preset::TYPE_PRINT)->get_config();
        auto* object_tab = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
        if (object_tab == nullptr)
            throw std::runtime_error("QIDI object settings tab is not available");
        const DynamicPrintConfig& local = object->config.get();
        std::vector<std::string> requested;
        const bool enumerating_overrides = keys.empty();
        if (enumerating_overrides)
            requested = local.keys();
        else {
            for (const json& key : keys) {
                if (!key.is_string())
                    throw std::runtime_error("Every setting key must be a string");
                requested.push_back(key.get<std::string>());
            }
        }
        std::sort(requested.begin(), requested.end());
        json values = json::array();
        for (const std::string& key : requested) {
            if (!object_tab->has_key(key)) {
                if (enumerating_overrides)
                    continue;
                throw std::runtime_error("Setting is not available at object scope: " + key);
            }
            const ConfigOption* global_option = global->option(key);
            if (global_option == nullptr) {
                if (enumerating_overrides)
                    continue;
                throw std::runtime_error("Unknown print setting: " + key);
            }
            const ConfigOption* local_option = local.option(key);
            values.push_back({{"key", key}, {"value", serialize_config_option(
                                  key, local_option != nullptr ? local_option : global_option)},
                              {"overridden", local_option != nullptr}});
        }
        return json{{"object_id", object_id}, {"settings", std::move(values)}};
    });
}

json set_object_settings(const json& args)
{
    const int object_id = required_int(args, "object_id");
    if (!args.contains("values") || !args["values"].is_object() || args["values"].empty())
        return {{"error", "values must be a non-empty object of serialized setting strings"}};
    const json values = args["values"];
    return on_gui_thread([object_id, values]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        DynamicPrintConfig* global = require_tab(Preset::TYPE_PRINT)->get_config();
        auto* object_tab = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
        if (object_tab == nullptr)
            throw std::runtime_error("QIDI object settings tab is not available");
        DynamicPrintConfig updated(object->config.get());
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.value().is_string())
                throw std::runtime_error("Object setting values must use QIDI's serialized string format");
            if (!object_tab->has_key(it.key()))
                throw std::runtime_error("Setting is not available at object scope: " + it.key());
            const ConfigOption* global_option = global->option(it.key());
            if (global_option == nullptr)
                throw std::runtime_error("Unknown print setting: " + it.key());
            if (updated.option(it.key()) == nullptr)
                updated.set_key_value(it.key(), global_option->clone());
            repair_config_option_enum_map(it.key(), updated.option(it.key()));
            updated.set_deserialize_strict(it.key(), it.value().get<std::string>());
        }

        plater->take_snapshot("MCP Set Object Settings");
        object->config.assign_config(std::move(updated));
        plater->object_list_changed();
        plater->schedule_background_process();
        json applied = json::object();
        for (auto it = values.begin(); it != values.end(); ++it)
            applied[it.key()] = serialize_config_option(
                it.key(), object->config.get().option(it.key()));
        return json{{"object_id", object_id}, {"applied", std::move(applied)}};
    });
}

json reset_object_settings(const json& args)
{
    const int object_id = required_int(args, "object_id");
    if (!args.contains("keys") || !args["keys"].is_array() || args["keys"].empty())
        return {{"error", "keys must be a non-empty array"}};
    const json keys = args["keys"];
    return on_gui_thread([object_id, keys]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        std::vector<std::string> requested;
        for (const json& key : keys) {
            if (!key.is_string())
                throw std::runtime_error("Every setting key must be a string");
            requested.push_back(key.get<std::string>());
        }
        plater->take_snapshot("MCP Reset Object Settings");
        json reset = json::array();
        for (const std::string& key : requested)
            if (object->config.erase(key)) reset.push_back(key);
        plater->object_list_changed();
        plater->schedule_background_process();
        return json{{"object_id", object_id}, {"reset", std::move(reset)},
                    {"override_count", object->config.get().keys().size()}};
    });
}

json get_volume_settings(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    const json keys = args.value("keys", json::array());
    if (!keys.is_array())
        return {{"error", "keys must be an array when provided"}};
    return on_gui_thread([object_id, volume_id, keys]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        DynamicPrintConfig* global = require_tab(Preset::TYPE_PRINT)->get_config();
        auto* object_tab = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
        if (object_tab == nullptr)
            throw std::runtime_error("QIDI object settings tab is not available");

        const DynamicPrintConfig& object_local = object->config.get();
        const DynamicPrintConfig& local = volume->config.get();
        std::vector<std::string> requested;
        const bool enumerating_overrides = keys.empty();
        if (enumerating_overrides)
            requested = local.keys();
        else {
            for (const json& key : keys) {
                if (!key.is_string())
                    throw std::runtime_error("Every setting key must be a string");
                requested.push_back(key.get<std::string>());
            }
        }
        std::sort(requested.begin(), requested.end());

        json settings = json::array();
        for (const std::string& key : requested) {
            if (!object_tab->has_key(key)) {
                if (enumerating_overrides)
                    continue;
                throw std::runtime_error("Setting is not available at volume scope: " + key);
            }
            const ConfigOption* global_option = global->option(key);
            if (global_option == nullptr) {
                if (enumerating_overrides)
                    continue;
                throw std::runtime_error("Unknown print setting: " + key);
            }
            const ConfigOption* object_option = object_local.option(key);
            const ConfigOption* local_option = local.option(key);
            const ConfigOption* effective = local_option != nullptr ? local_option :
                                            (object_option != nullptr ? object_option : global_option);
            const char* source = local_option != nullptr ? "volume" :
                                 (object_option != nullptr ? "object" : "print_preset");
            settings.push_back({{"key", key}, {"value", serialize_config_option(key, effective)},
                                {"overridden", local_option != nullptr}, {"source", source}});
        }
        return json{{"object_id", object_id}, {"volume_id", volume_id},
                    {"settings", std::move(settings)}};
    });
}

json set_volume_settings(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    if (!args.contains("values") || !args["values"].is_object() || args["values"].empty())
        return {{"error", "values must be a non-empty object of serialized setting strings"}};
    const json values = args["values"];
    return on_gui_thread([object_id, volume_id, values]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        DynamicPrintConfig* global = require_tab(Preset::TYPE_PRINT)->get_config();
        auto* object_tab = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
        if (object_tab == nullptr)
            throw std::runtime_error("QIDI object settings tab is not available");

        const DynamicPrintConfig& object_local = object->config.get();
        DynamicPrintConfig updated(volume->config.get());
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.value().is_string())
                throw std::runtime_error("Volume setting values must use QIDI's serialized string format");
            if (!object_tab->has_key(it.key()))
                throw std::runtime_error("Setting is not available at volume scope: " + it.key());
            const ConfigOption* global_option = global->option(it.key());
            if (global_option == nullptr)
                throw std::runtime_error("Unknown print setting: " + it.key());
            const ConfigOption* object_option = object_local.option(it.key());
            const ConfigOption* inherited = object_option != nullptr ? object_option : global_option;
            if (updated.option(it.key()) == nullptr)
                updated.set_key_value(it.key(), inherited->clone());
            repair_config_option_enum_map(it.key(), updated.option(it.key()));
            updated.set_deserialize_strict(it.key(), it.value().get<std::string>());
        }

        plater->take_snapshot("MCP Set Volume Settings");
        volume->config.assign_config(std::move(updated));
        plater->object_list_changed();
        plater->schedule_background_process();
        json applied = json::object();
        for (auto it = values.begin(); it != values.end(); ++it)
            applied[it.key()] = serialize_config_option(
                it.key(), volume->config.get().option(it.key()));
        return json{{"object_id", object_id}, {"volume_id", volume_id},
                    {"applied", std::move(applied)}, {"undo_available", plater->can_undo()}};
    });
}

json reset_volume_settings(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    if (!args.contains("keys") || !args["keys"].is_array() || args["keys"].empty())
        return {{"error", "keys must be a non-empty array"}};
    const json keys = args["keys"];
    return on_gui_thread([object_id, volume_id, keys]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        std::vector<std::string> requested;
        for (const json& key : keys) {
            if (!key.is_string())
                throw std::runtime_error("Every setting key must be a string");
            requested.push_back(key.get<std::string>());
        }

        plater->take_snapshot("MCP Reset Volume Settings");
        json reset = json::array();
        for (const std::string& key : requested)
            if (volume->config.erase(key))
                reset.push_back(key);
        plater->object_list_changed();
        plater->schedule_background_process();
        return json{{"object_id", object_id}, {"volume_id", volume_id},
                    {"reset", std::move(reset)},
                    {"override_count", volume->config.get().keys().size()},
                    {"undo_available", plater->can_undo()}};
    });
}

json list_setting_definitions(const json& args)
{
    const std::string scope = args.value("scope", "print");
    if (scope != "print" && scope != "filament" && scope != "printer" &&
        scope != "object" && scope != "volume")
        return {{"error", "scope must be print, filament, printer, object, or volume"}};

    const std::string query = lower_copy(args.value("query", ""));
    const std::string category = lower_copy(args.value("category", ""));
    const int offset_value = args.value("offset", 0);
    const int limit_value = args.value("limit", 100);
    if (offset_value < 0)
        return {{"error", "offset must be non-negative"}};
    if (limit_value < 1 || limit_value > 500)
        return {{"error", "limit must be between 1 and 500"}};

    int filament_index = -1;
    int object_id = -1;
    int volume_id = -1;
    for (const auto& item : std::array<std::pair<const char*, int*>, 3>{{
            {"filament_index", &filament_index}, {"object_id", &object_id}, {"volume_id", &volume_id}}}) {
        if (!args.contains(item.first))
            continue;
        if (!args[item.first].is_number_integer() || args[item.first].get<int>() < 0)
            return {{"error", std::string(item.first) + " must be a non-negative integer"}};
        *item.second = args[item.first].get<int>();
    }
    if (scope != "filament" && filament_index >= 0)
        return {{"error", "filament_index is only valid for filament scope"}};
    if (scope != "object" && scope != "volume" && object_id >= 0)
        return {{"error", "object_id is only valid for object or volume scope"}};
    if (scope != "volume" && volume_id >= 0)
        return {{"error", "volume_id is only valid for volume scope"}};
    if (scope == "volume" && ((object_id >= 0) != (volume_id >= 0)))
        return {{"error", "object_id and volume_id must be provided together for volume context"}};

    const size_t offset = static_cast<size_t>(offset_value);
    const size_t limit = static_cast<size_t>(limit_value);
    return on_gui_thread([scope, query, category, offset, limit,
                          filament_index, object_id, volume_id]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");

        const DynamicPrintConfig* print_config = require_tab(Preset::TYPE_PRINT)->get_config();
        const DynamicPrintConfig* filament_config = require_tab(Preset::TYPE_FILAMENT)->get_config();
        const DynamicPrintConfig* printer_config = require_tab(Preset::TYPE_PRINTER)->get_config();
        auto* model_tab = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
        if (model_tab == nullptr)
            throw std::runtime_error("QIDI object settings tab is not available");

        std::string context_name;
        if (scope == "filament" && filament_index >= 0) {
            const size_t index = static_cast<size_t>(filament_index);
            if (index >= bundle->filament_presets.size())
                throw std::runtime_error("filament_index is out of range");
            context_name = bundle->filament_presets[index];
            const Preset* preset = bundle->filaments.find_preset(context_name, false);
            if (preset == nullptr)
                throw std::runtime_error("Assigned filament preset was not found");
            filament_config = &preset->config;
        }

        const DynamicPrintConfig* object_config = nullptr;
        const DynamicPrintConfig* volume_config = nullptr;
        if (object_id >= 0) {
            ModelObject* object = require_model_object(require_plater(), object_id);
            object_config = &object->config.get();
            if (volume_id >= 0)
                volume_config = &require_model_volume(object, volume_id)->config.get();
        }

        json definitions = json::array();
        size_t matched = 0;
        for (const auto& [key, definition] : print_config_def.options) {
            const bool print_supported = print_config->option(key) != nullptr;
            const bool filament_supported = filament_config->option(key) != nullptr;
            const bool printer_supported = printer_config->option(key) != nullptr;
            const bool model_supported = model_tab->has_key(key);
            const bool in_scope =
                (scope == "print" && print_supported) ||
                (scope == "filament" && filament_supported) ||
                (scope == "printer" && printer_supported) ||
                (scope == "object" && model_supported) ||
                (scope == "volume" && model_supported);
            if (!in_scope || !setting_text_matches(definition, query) ||
                (!category.empty() && lower_copy(definition.category) != category))
                continue;

            const size_t match_index = matched++;
            if (match_index < offset || definitions.size() >= limit)
                continue;

            json supported_scopes = json::array();
            if (print_supported) supported_scopes.push_back("print");
            if (filament_supported) supported_scopes.push_back("filament");
            if (printer_supported) supported_scopes.push_back("printer");
            if (model_supported) {
                supported_scopes.push_back("object");
                supported_scopes.push_back("volume");
            }

            const ConfigOption* local = nullptr;
            const ConfigOption* effective = nullptr;
            std::string source;
            if (scope == "print") {
                local = effective = print_config->option(key);
                source = "active_print_settings";
            } else if (scope == "filament") {
                local = effective = filament_config->option(key);
                source = filament_index >= 0 ? "project_filament_preset" : "active_filament_settings";
            } else if (scope == "printer") {
                local = effective = printer_config->option(key);
                source = "active_printer_settings";
            } else if (scope == "object" && object_config != nullptr) {
                local = object_config->option(key);
                effective = local != nullptr ? local : print_config->option(key);
                source = local != nullptr ? "object_override" : "print_preset";
            } else if (scope == "volume" && volume_config != nullptr) {
                local = volume_config->option(key);
                const ConfigOption* object_option = object_config != nullptr ? object_config->option(key) : nullptr;
                effective = local != nullptr ? local :
                            (object_option != nullptr ? object_option : print_config->option(key));
                source = local != nullptr ? "volume_override" :
                         (object_option != nullptr ? "object_override" : "print_preset");
            }
            definitions.push_back(setting_definition_to_json(
                definition, supported_scopes, effective, local, source));
        }

        const size_t returned = definitions.size();
        const size_t next = offset + returned;
        return json{
            {"scope", scope},
            {"query", query},
            {"category", category.empty() ? json(nullptr) : json(category)},
            {"offset", offset},
            {"limit", limit},
            {"returned", returned},
            {"total_matching", matched},
            {"next_offset", next < matched ? json(next) : json(nullptr)},
            {"context", {
                {"filament_index", filament_index >= 0 ? json(filament_index) : json(nullptr)},
                {"filament_preset", context_name.empty() ? json(nullptr) : json(context_name)},
                {"object_id", object_id >= 0 ? json(object_id) : json(nullptr)},
                {"volume_id", volume_id >= 0 ? json(volume_id) : json(nullptr)}
            }},
            {"definitions", std::move(definitions)},
            {"mutated", false}
        };
    });
}

json preview_settings_update(const json& args)
{
    const std::string scope = args.value("scope", "print");
    if (scope != "print" && scope != "filament" && scope != "printer" &&
        scope != "object" && scope != "volume")
        return {{"error", "scope must be print, filament, printer, object, or volume"}};
    if (!args.contains("values") || !args["values"].is_object() || args["values"].empty())
        return {{"error", "values must be a non-empty object of serialized setting strings"}};

    int filament_index = -1;
    int object_id = -1;
    int volume_id = -1;
    for (const auto& item : std::array<std::pair<const char*, int*>, 3>{{
            {"filament_index", &filament_index}, {"object_id", &object_id}, {"volume_id", &volume_id}}}) {
        if (!args.contains(item.first))
            continue;
        if (!args[item.first].is_number_integer() || args[item.first].get<int>() < 0)
            return {{"error", std::string(item.first) + " must be a non-negative integer"}};
        *item.second = args[item.first].get<int>();
    }
    if (scope == "object" && object_id < 0)
        return {{"error", "object_id is required for object scope"}};
    if (scope == "volume" && (object_id < 0 || volume_id < 0))
        return {{"error", "object_id and volume_id are required for volume scope"}};
    if (scope != "filament" && filament_index >= 0)
        return {{"error", "filament_index is only valid for filament scope"}};
    if (scope != "object" && scope != "volume" && object_id >= 0)
        return {{"error", "object_id is only valid for object or volume scope"}};
    if (scope != "volume" && volume_id >= 0)
        return {{"error", "volume_id is only valid for volume scope"}};

    const json values = args["values"];
    return on_gui_thread([scope, values, filament_index, object_id, volume_id]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");

        DynamicPrintConfig* print_config = require_tab(Preset::TYPE_PRINT)->get_config();
        const DynamicPrintConfig* local_config = nullptr;
        const DynamicPrintConfig* inherited_config = nullptr;
        const DynamicPrintConfig* second_inherited_config = nullptr;
        DynamicPrintConfig candidate;
        std::string context_name;
        int resolved_filament_index = filament_index;

        if (scope == "print" || scope == "printer") {
            Tab* tab = require_tab(preset_type(scope));
            local_config = tab->get_config();
            candidate = DynamicPrintConfig(*local_config);
            if (tab->get_presets() != nullptr)
                context_name = tab->get_presets()->get_edited_preset().name;
        } else if (scope == "filament") {
            if (bundle->filament_presets.empty())
                throw std::runtime_error("Project has no filament slots");
            if (resolved_filament_index < 0 && bundle->filament_presets.size() != 1)
                throw std::runtime_error("Project has multiple filament slots; provide filament_index");
            if (resolved_filament_index < 0)
                resolved_filament_index = 0;
            const size_t index = static_cast<size_t>(resolved_filament_index);
            if (index >= bundle->filament_presets.size())
                throw std::runtime_error("filament_index is out of range");
            context_name = bundle->filament_presets[index];
            const Preset* preset = bundle->filaments.find_preset(context_name, false);
            if (preset == nullptr)
                throw std::runtime_error("Assigned filament preset was not found");
            local_config = &preset->config;
            candidate = DynamicPrintConfig(*local_config);
        } else {
            Plater* plater = require_plater();
            ModelObject* object = require_model_object(plater, object_id);
            auto* model_tab = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
            if (model_tab == nullptr)
                throw std::runtime_error("QIDI object settings tab is not available");
            if (scope == "object") {
                local_config = &object->config.get();
                inherited_config = print_config;
            } else {
                local_config = &require_model_volume(object, volume_id)->config.get();
                inherited_config = &object->config.get();
                second_inherited_config = print_config;
            }
            candidate = DynamicPrintConfig(*local_config);
        }

        auto effective_before = [local_config, inherited_config, second_inherited_config](
                                    const std::string& key) -> const ConfigOption* {
            if (const ConfigOption* option = local_config->option(key); option != nullptr)
                return option;
            if (inherited_config != nullptr) {
                if (const ConfigOption* option = inherited_config->option(key); option != nullptr)
                    return option;
            }
            return second_inherited_config != nullptr ? second_inherited_config->option(key) : nullptr;
        };

        auto source_before = [local_config, inherited_config, scope](const std::string& key) {
            if (scope == "object")
                return local_config->option(key) != nullptr ? std::string("object_override") : std::string("print_preset");
            if (scope == "volume") {
                if (local_config->option(key) != nullptr) return std::string("volume_override");
                if (inherited_config != nullptr && inherited_config->option(key) != nullptr)
                    return std::string("object_override");
                return std::string("print_preset");
            }
            return std::string("active_") + scope + "_settings";
        };

        auto* model_tab = dynamic_cast<TabPrintModel*>(wxGetApp().get_model_tab());
        json parse_errors = json::array();
        json normalized = json::object();
        json changes = json::array();
        std::vector<std::string> changed_keys;
        bool would_change = false;

        for (auto it = values.begin(); it != values.end(); ++it) {
            const std::string key = it.key();
            if (!it.value().is_string()) {
                parse_errors.push_back({{"key", key}, {"message", "Value must use QIDI's serialized string format"}});
                continue;
            }
            const ConfigOptionDef* definition = print_config_def.get(key);
            const ConfigOption* before = effective_before(key);
            if (definition == nullptr || before == nullptr) {
                parse_errors.push_back({{"key", key}, {"message", "Setting is not available in the requested scope"}});
                continue;
            }
            if ((scope == "object" || scope == "volume") &&
                (model_tab == nullptr || !model_tab->has_key(key))) {
                parse_errors.push_back({{"key", key}, {"message", "Setting is not available at object or volume scope"}});
                continue;
            }
            if (definition->readonly) {
                parse_errors.push_back({{"key", key}, {"message", "Setting is read-only"}});
                continue;
            }

            const bool overridden_before = (scope == "object" || scope == "volume") &&
                                           local_config->option(key) != nullptr;
            try {
                if (candidate.option(key) == nullptr)
                    candidate.set_key_value(key, before->clone());
                repair_config_option_enum_map(key, candidate.option(key));
                candidate.set_deserialize_strict(key, it.value().get<std::string>());
                const std::string before_value = serialize_config_option(key, before);
                const std::string after_value = serialize_config_option(key, candidate.option(key));
                const bool key_would_change = before_value != after_value ||
                                              ((scope == "object" || scope == "volume") && !overridden_before);
                would_change = would_change || key_would_change;
                normalized[key] = after_value;
                changed_keys.push_back(key);
                changes.push_back({
                    {"key", key},
                    {"before", before_value},
                    {"after", after_value},
                    {"changed", key_would_change},
                    {"source_before", source_before(key)},
                    {"source_after", scope == "object" ? "object_override" :
                                     (scope == "volume" ? "volume_override" : "active_" + scope + "_settings")},
                    {"overridden_before", scope == "object" || scope == "volume" ? json(overridden_before) : json(nullptr)},
                    {"overridden_after", scope == "object" || scope == "volume" ? json(true) : json(nullptr)}
                });
            } catch (const std::exception& error) {
                parse_errors.push_back({{"key", key}, {"message", error.what()}});
            }
        }

        DynamicPrintConfig baseline_full = bundle->full_config();
        const auto baseline_errors = baseline_full.validate(false);
        DynamicPrintConfig candidate_full(baseline_full);
        if (parse_errors.empty()) {
            for (const std::string& key : changed_keys) {
                const ConfigOption* source = candidate.option(key);
                if (scope == "filament" && resolved_filament_index >= 0) {
                    ConfigOption* target = candidate_full.option_throw(key);
                    if (target->is_vector()) {
                        auto* target_vector = dynamic_cast<ConfigOptionVectorBase*>(target);
                        if (target_vector == nullptr)
                            throw std::runtime_error("QIDI vector setting could not be indexed: " + key);
                        target_vector->set_at(source, static_cast<size_t>(resolved_filament_index), 0);
                    } else {
                        target->set(source);
                    }
                } else {
                    candidate_full.set_key_value(key, source->clone());
                }
            }
        }
        const auto candidate_errors = parse_errors.empty() ? candidate_full.validate(false) : baseline_errors;
        const json introduced_errors = introduced_validation_errors(baseline_errors, candidate_errors);
        const bool valid = parse_errors.empty() && candidate_errors.empty();

        return json{
            {"scope", scope},
            {"context", {
                {"preset", context_name.empty() ? json(nullptr) : json(context_name)},
                {"filament_index", resolved_filament_index >= 0 ? json(resolved_filament_index) : json(nullptr)},
                {"object_id", object_id >= 0 ? json(object_id) : json(nullptr)},
                {"volume_id", volume_id >= 0 ? json(volume_id) : json(nullptr)}
            }},
            {"valid", valid},
            {"mutated", false},
            {"would_change", would_change},
            {"would_invalidate_slice", would_change},
            {"would_mark_project_or_preset_dirty", would_change},
            {"normalized_values", std::move(normalized)},
            {"changes", std::move(changes)},
            {"parse_errors", std::move(parse_errors)},
            {"validation", {
                {"baseline_valid", baseline_errors.empty()},
                {"baseline_error_count", baseline_errors.size()},
                {"candidate_valid", candidate_errors.empty()},
                {"candidate_error_count", candidate_errors.size()},
                {"candidate_errors", validation_errors_to_json(candidate_errors)},
                {"introduced_errors", introduced_errors},
                {"introduced_error_count", introduced_errors.size()}
            }}
        };
    });
}

json layer_height_profile_to_json(const std::vector<double>& profile)
{
    constexpr size_t max_returned_points = 256;
    const size_t total_points = profile.size() / 2;
    json points = json::array();
    double min_height = std::numeric_limits<double>::infinity();
    double max_height = 0.0;
    for (size_t index = 0, point_index = 0;
         index + 1 < profile.size();
         index += 2, ++point_index) {
        const double z = profile[index];
        const double height = profile[index + 1];
        min_height = std::min(min_height, height);
        max_height = std::max(max_height, height);

        const bool include_point =
            total_points <= max_returned_points ||
            point_index < max_returned_points - 1 ||
            point_index + 1 == total_points;
        if (include_point)
            points.push_back({{"z_mm", z}, {"layer_height_mm", height}});
    }
    if (total_points == 0)
        min_height = 0.0;
    return {
        {"point_count", total_points},
        {"points_returned", points.size()},
        {"points_truncated", points.size() < total_points},
        {"variable", total_points > 2},
        {"min_layer_height_mm", min_height},
        {"max_layer_height_mm", max_height},
        {"points", std::move(points)}
    };
}

SlicingParameters layer_height_slicing_parameters(ModelObject* object)
{
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr)
        throw std::runtime_error("Preset bundle is not available");
    const float object_max_z = static_cast<float>(object->bounding_box().max.z());
    if (!(object_max_z > 0.0f))
        throw std::runtime_error("The object has no positive printable height");
    return PrintObject::slicing_parameters(bundle->full_config(), *object, object_max_z);
}

std::vector<double> effective_layer_height_profile(ModelObject* object,
                                                    const SlicingParameters& parameters,
                                                    bool& nozzle_range_reset)
{
    std::vector<double> profile = object->layer_height_profile.get();
    PrintObject::update_layer_height_profile(*object, parameters, profile, nozzle_range_reset);
    return profile;
}

json get_layer_height_profile(const json& args)
{
    const int object_id = required_int(args, "object_id");
    return on_gui_thread([object_id]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            return json{
                {"busy", true},
                {"retry_after_ms", 1000},
                {"error", "Wait for the current QIDI Studio job to finish"}
            };
        ModelObject* object = require_model_object(plater, object_id);
        const SlicingParameters parameters = layer_height_slicing_parameters(object);
        bool nozzle_range_reset = false;
        const std::vector<double> effective =
            effective_layer_height_profile(object, parameters, nozzle_range_reset);
        return json{
            {"object_id", object_id},
            {"name", object->name},
            {"stored_profile", layer_height_profile_to_json(object->layer_height_profile.get())},
            {"effective_profile", layer_height_profile_to_json(effective)},
            {"stored_profile_present", !object->layer_height_profile.empty()},
            {"would_reset_for_nozzle_range", nozzle_range_reset},
            {"source", object->layer_height_profile.empty() ? "active_uniform_settings" : "object_variable_profile"}
        };
    });
}

json preview_adaptive_layer_height(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const double quality_factor = args.value("quality_factor", 0.5);
    if (!std::isfinite(quality_factor) || quality_factor < 0.0 || quality_factor > 1.0)
        return {{"error", "quality_factor must be between 0.0 (speed) and 1.0 (quality)"}};
    return on_gui_thread([object_id, quality_factor]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            return json{
                {"busy", true},
                {"retry_after_ms", 1000},
                {"error", "Wait for the current QIDI Studio job to finish"}
            };
        ModelObject* object = require_model_object(plater, object_id);
        const SlicingParameters parameters = layer_height_slicing_parameters(object);
        bool nozzle_range_reset = false;
        const std::vector<double> current =
            effective_layer_height_profile(object, parameters, nozzle_range_reset);
        const std::vector<double> proposed =
            layer_height_profile_adaptive(parameters, *object, static_cast<float>(quality_factor));
        return json{
            {"object_id", object_id},
            {"name", object->name},
            {"quality_factor", quality_factor},
            {"quality_direction", "0.0 favors speed; 1.0 favors surface quality"},
            {"current", layer_height_profile_to_json(current)},
            {"proposed", layer_height_profile_to_json(proposed)},
            {"would_change", proposed != object->layer_height_profile.get()},
            {"current_profile_would_reset_for_nozzle_range", nozzle_range_reset},
            {"mutated", false}
        };
    });
}

json apply_adaptive_layer_height(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const double quality_factor = args.value("quality_factor", 0.5);
    if (!std::isfinite(quality_factor) || quality_factor < 0.0 || quality_factor > 1.0)
        return {{"error", "quality_factor must be between 0.0 (speed) and 1.0 (quality)"}};
    return on_gui_thread([object_id, quality_factor]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        const SlicingParameters parameters = layer_height_slicing_parameters(object);
        std::vector<double> proposed =
            layer_height_profile_adaptive(parameters, *object, static_cast<float>(quality_factor));
        if (proposed == object->layer_height_profile.get())
            return json{{"changed", false}, {"object_id", object_id},
                        {"quality_factor", quality_factor},
                        {"profile", layer_height_profile_to_json(proposed)}};

        plater->take_snapshot("MCP Apply Adaptive Layer Height");
        object->layer_height_profile.set(proposed);
        if (ObjectList* object_list = wxGetApp().obj_list(); object_list != nullptr)
            object_list->update_info_items(static_cast<size_t>(object_id));
        plater->changed_object(object_id);
        plater->schedule_background_process();
        return json{{"changed", true}, {"object_id", object_id},
                    {"quality_factor", quality_factor},
                    {"profile", layer_height_profile_to_json(proposed)},
                    {"undo_available", plater->can_undo()}};
    });
}

json smooth_layer_height_profile(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int radius = args.value("radius", 5);
    const bool keep_min = args.value("keep_min", false);
    if (radius < 1 || radius > 10)
        return {{"error", "radius must be between 1 and 10"}};
    return on_gui_thread([object_id, radius, keep_min]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        const std::vector<double>& current = object->layer_height_profile.get();
        if (current.size() <= 4)
            throw std::runtime_error("The object does not have a variable layer-height profile to smooth");
        const SlicingParameters parameters = layer_height_slicing_parameters(object);
        HeightProfileSmoothingParams smoothing;
        smoothing.radius = static_cast<unsigned int>(radius);
        smoothing.keep_min = keep_min;
        std::vector<double> proposed = smooth_height_profile(current, parameters, smoothing);
        if (proposed == current)
            return json{{"changed", false}, {"object_id", object_id}, {"radius", radius},
                        {"keep_min", keep_min}, {"profile", layer_height_profile_to_json(proposed)}};

        plater->take_snapshot("MCP Smooth Layer Height Profile");
        object->layer_height_profile.set(proposed);
        if (ObjectList* object_list = wxGetApp().obj_list(); object_list != nullptr)
            object_list->update_info_items(static_cast<size_t>(object_id));
        plater->changed_object(object_id);
        plater->schedule_background_process();
        return json{{"changed", true}, {"object_id", object_id}, {"radius", radius},
                    {"keep_min", keep_min}, {"profile", layer_height_profile_to_json(proposed)},
                    {"undo_available", plater->can_undo()}};
    });
}

json reset_layer_height_profile(const json& args)
{
    const int object_id = required_int(args, "object_id");
    return on_gui_thread([object_id]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        if (object->layer_height_profile.empty())
            return json{{"changed", false}, {"object_id", object_id},
                        {"source", "active_uniform_settings"}};

        plater->take_snapshot("MCP Reset Layer Height Profile");
        object->layer_height_profile.clear();
        if (ObjectList* object_list = wxGetApp().obj_list(); object_list != nullptr)
            object_list->update_info_items(static_cast<size_t>(object_id));
        plater->changed_object(object_id);
        plater->schedule_background_process();
        return json{{"changed", true}, {"object_id", object_id},
                    {"source", "active_uniform_settings"},
                    {"undo_available", plater->can_undo()}};
    });
}

struct SurfaceSelectionResult {
    std::vector<int> facet_ids;
    double surface_area_mm2{0.0};
    Vec3d bounds_min = Vec3d::Zero();
    Vec3d bounds_max = Vec3d::Zero();
    bool has_bounds{false};
};

ModelInstance* require_object_instance(ModelObject* object, int instance_id)
{
    if (instance_id < 0 || static_cast<size_t>(instance_id) >= object->instances.size() ||
        object->instances[instance_id] == nullptr)
        throw std::runtime_error("Model instance was not found");
    return object->instances[instance_id];
}

double required_selector_number(const json& selector, const char* key)
{
    if (!selector.contains(key) || !selector[key].is_number())
        throw std::runtime_error(std::string("Surface selector requires numeric ") + key);
    const double value = selector[key].get<double>();
    if (!std::isfinite(value))
        throw std::runtime_error(std::string("Surface selector value must be finite: ") + key);
    return value;
}

Vec3d required_selector_vec3(const json& selector, const char* key)
{
    if (!selector.contains(key) || !selector[key].is_object())
        throw std::runtime_error(std::string("Surface selector requires vector ") + key);
    const json& value = selector[key];
    Vec3d result;
    const std::array<const char*, 3> axes{{"x", "y", "z"}};
    for (int axis = 0; axis < 3; ++axis) {
        if (!value.contains(axes[axis]) || !value[axes[axis]].is_number())
            throw std::runtime_error(std::string("Surface selector vector requires ") + key + "." + axes[axis]);
        result[axis] = value[axes[axis]].get<double>();
        if (!std::isfinite(result[axis]))
            throw std::runtime_error(std::string("Surface selector vector must be finite: ") + key);
    }
    return result;
}

bool match_range(const std::array<Vec3d, 3>& points, double min_value, double max_value,
                 int axis, const std::string& match)
{
    if (match == "centroid") {
        const double center = (points[0][axis] + points[1][axis] + points[2][axis]) / 3.0;
        return center >= min_value && center <= max_value;
    }
    const auto inside = [axis, min_value, max_value](const Vec3d& point) {
        return point[axis] >= min_value && point[axis] <= max_value;
    };
    if (match == "any_vertex")
        return inside(points[0]) || inside(points[1]) || inside(points[2]);
    return inside(points[0]) && inside(points[1]) && inside(points[2]);
}

bool match_box(const std::array<Vec3d, 3>& points, const Vec3d& min_point,
               const Vec3d& max_point, const std::string& match)
{
    const auto inside = [&min_point, &max_point](const Vec3d& point) {
        return (point.array() >= min_point.array()).all() &&
               (point.array() <= max_point.array()).all();
    };
    if (match == "centroid")
        return inside((points[0] + points[1] + points[2]) / 3.0);
    if (match == "any_vertex")
        return inside(points[0]) || inside(points[1]) || inside(points[2]);
    return inside(points[0]) && inside(points[1]) && inside(points[2]);
}

SurfaceSelectionResult select_surface_facets(ModelVolume* volume, ModelInstance* instance,
                                             const json& selector)
{
    if (!selector.is_object())
        throw std::runtime_error("selector must be an object");
    const std::string type = selector.value("type", std::string());
    if (type.empty())
        throw std::runtime_error("Surface selector requires type");
    const std::string match = selector.value("match", "centroid");
    if (match != "centroid" && match != "any_vertex" && match != "all_vertices")
        throw std::runtime_error("selector.match must be centroid, any_vertex, or all_vertices");

    const indexed_triangle_set& its = volume->mesh().its;
    const Transform3d transform = instance->get_matrix() * volume->get_matrix();
    std::set<int> explicit_facets;
    double range_min = 0.0;
    double range_max = 0.0;
    Vec3d box_min = Vec3d::Zero();
    Vec3d box_max = Vec3d::Zero();
    Vec3d direction = Vec3d::Zero();
    double dot_limit = -1.0;

    if (type == "facet_ids") {
        if (!selector.contains("facet_ids") || !selector["facet_ids"].is_array() ||
            selector["facet_ids"].empty())
            throw std::runtime_error("facet_ids selector requires a non-empty facet_ids array");
        for (const json& value : selector["facet_ids"]) {
            if (!value.is_number_integer())
                throw std::runtime_error("Every facet_id must be an integer");
            const int facet_id = value.get<int>();
            if (facet_id < 0 || static_cast<size_t>(facet_id) >= its.indices.size())
                throw std::runtime_error("A facet_id is invalid or out of range");
            if (!explicit_facets.insert(facet_id).second)
                throw std::runtime_error("facet_ids must be unique");
        }
    } else if (type == "height_range") {
        range_min = required_selector_number(selector, "min_z_mm");
        range_max = required_selector_number(selector, "max_z_mm");
        if (range_min > range_max)
            throw std::runtime_error("min_z_mm must be less than or equal to max_z_mm");
    } else if (type == "bounding_box") {
        box_min = required_selector_vec3(selector, "min_mm");
        box_max = required_selector_vec3(selector, "max_mm");
        if ((box_min.array() > box_max.array()).any())
            throw std::runtime_error("Every min_mm component must be less than or equal to max_mm");
    } else if (type == "normal") {
        direction = required_selector_vec3(selector, "direction");
        const double magnitude = direction.norm();
        if (!(magnitude > std::numeric_limits<double>::epsilon()))
            throw std::runtime_error("direction must be non-zero");
        direction /= magnitude;
        const double max_angle = selector.value("max_angle_deg", 15.0);
        if (!std::isfinite(max_angle) || max_angle < 0.0 || max_angle > 180.0)
            throw std::runtime_error("max_angle_deg must be between 0 and 180 for a normal selector");
        dot_limit = std::cos(max_angle * MCP_PI / 180.0);
    } else if (type == "overhang") {
        const double max_angle = selector.value("max_angle_deg", 45.0);
        if (!std::isfinite(max_angle) || max_angle < 0.0 || max_angle > 90.0)
            throw std::runtime_error("max_angle_deg must be between 0 and 90 for an overhang selector");
        direction = -Vec3d::UnitZ();
        dot_limit = std::cos(max_angle * MCP_PI / 180.0);
    } else if (type != "all") {
        throw std::runtime_error("selector.type must be all, facet_ids, height_range, bounding_box, normal, or overhang");
    }

    SurfaceSelectionResult result;
    result.facet_ids.reserve(its.indices.size());
    for (size_t face_index = 0; face_index < its.indices.size(); ++face_index) {
        const stl_triangle_vertex_indices& face = its.indices[face_index];
        const std::array<Vec3d, 3> points{{
            transform * its.vertices[face[0]].cast<double>(),
            transform * its.vertices[face[1]].cast<double>(),
            transform * its.vertices[face[2]].cast<double>()
        }};
        const Vec3d cross = (points[1] - points[0]).cross(points[2] - points[0]);
        const double twice_area = cross.norm();

        bool selected = type == "all";
        if (type == "facet_ids")
            selected = explicit_facets.count(static_cast<int>(face_index)) != 0;
        else if (type == "height_range")
            selected = match_range(points, range_min, range_max, 2, match);
        else if (type == "bounding_box")
            selected = match_box(points, box_min, box_max, match);
        else if (type == "normal" || type == "overhang")
            selected = twice_area > std::numeric_limits<double>::epsilon() &&
                       (cross / twice_area).dot(direction) >= dot_limit;

        if (!selected)
            continue;
        result.facet_ids.push_back(static_cast<int>(face_index));
        result.surface_area_mm2 += 0.5 * twice_area;
        for (const Vec3d& point : points) {
            if (!result.has_bounds) {
                result.bounds_min = point;
                result.bounds_max = point;
                result.has_bounds = true;
            } else {
                result.bounds_min = result.bounds_min.cwiseMin(point);
                result.bounds_max = result.bounds_max.cwiseMax(point);
            }
        }
    }
    return result;
}

json surface_selection_to_json(const SurfaceSelectionResult& selection, size_t max_facet_ids)
{
    json facet_ids = json::array();
    const size_t count = std::min(max_facet_ids, selection.facet_ids.size());
    for (size_t index = 0; index < count; ++index)
        facet_ids.push_back(selection.facet_ids[index]);
    json bounds = nullptr;
    if (selection.has_bounds) {
        bounds = {
            {"min_mm", {{"x", selection.bounds_min.x()}, {"y", selection.bounds_min.y()}, {"z", selection.bounds_min.z()}}},
            {"max_mm", {{"x", selection.bounds_max.x()}, {"y", selection.bounds_max.y()}, {"z", selection.bounds_max.z()}}}
        };
    }
    return {
        {"selected_facet_count", selection.facet_ids.size()},
        {"selected_surface_area_mm2", selection.surface_area_mm2},
        {"selected_bounds", std::move(bounds)},
        {"facet_ids", std::move(facet_ids)},
        {"facet_ids_truncated", count < selection.facet_ids.size()}
    };
}

json paint_state_to_json(TriangleSelector& selector, size_t original_facet_count)
{
    const int enforcer_count = selector.num_facets(EnforcerBlockerType::ENFORCER);
    const int blocker_count = selector.num_facets(EnforcerBlockerType::BLOCKER);
    return {
        {"original_facet_count", original_facet_count},
        {"painted_leaf_facet_count", enforcer_count + blocker_count},
        {"enforcer_leaf_facet_count", enforcer_count},
        {"blocker_leaf_facet_count", blocker_count},
        {"subdivision_definitely_present", enforcer_count + blocker_count > static_cast<int>(original_facet_count)},
        {"painted", enforcer_count > 0 || blocker_count > 0}
    };
}

json preview_surface_selection(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    const int instance_id = args.value("instance_id", 0);
    const int requested_max_facet_ids = args.value("max_facet_ids", 1000);
    if (requested_max_facet_ids < 0 || requested_max_facet_ids > 5000)
        return {{"error", "max_facet_ids must be between 0 and 5000"}};
    const size_t max_facet_ids = static_cast<size_t>(requested_max_facet_ids);
    if (!args.contains("selector"))
        return {{"error", "Missing parameter: selector"}};
    const json selector = args["selector"];
    return on_gui_thread([object_id, volume_id, instance_id, selector, max_facet_ids]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            return json{
                {"busy", true},
                {"retry_after_ms", 1000},
                {"error", "Wait for the current QIDI Studio job to finish"}
            };
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        if (!volume->is_model_part())
            throw std::runtime_error("Surface selection requires a model-part volume");
        ModelInstance* instance = require_object_instance(object, instance_id);
        const SurfaceSelectionResult selected = select_surface_facets(volume, instance, selector);
        return json{{"object_id", object_id}, {"volume_id", volume_id}, {"instance_id", instance_id},
                    {"coordinate_space", "build_plate"}, {"selector", selector},
                    {"facet_id_space", "volume_source_mesh"},
                    {"selection_applies_to_all_object_instances", true},
                    {"object_instance_count", object->instances.size()},
                    {"selection", surface_selection_to_json(selected, max_facet_ids)},
                    {"mutated", false}};
    });
}

json get_surface_paint_state(const json& args, bool support)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    return on_gui_thread([object_id, volume_id, support]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            return json{
                {"busy", true},
                {"retry_after_ms", 1000},
                {"error", "Wait for the current QIDI Studio job to finish"}
            };
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        if (!volume->is_model_part())
            throw std::runtime_error("Surface painting requires a model-part volume");
        TriangleSelector selector(volume->mesh());
        if (support)
            selector.deserialize(volume->supported_facets.get_data(), false);
        else
            selector.deserialize(volume->seam_facets.get_data(), false);
        return json{{"object_id", object_id}, {"volume_id", volume_id},
                    {"paint_type", support ? "support" : "seam"},
                    {"facet_id_space", "volume_source_mesh"},
                    {"applies_to_all_object_instances", true},
                    {"object_instance_count", object->instances.size()},
                    {"state", paint_state_to_json(selector, volume->mesh().facets_count())}};
    });
}

json set_surface_paint(const json& args, bool support)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    const int instance_id = args.value("instance_id", 0);
    const std::string state_name = args.value("state", std::string());
    if (!args.contains("selector"))
        return {{"error", "Missing parameter: selector"}};
    if (state_name != "enforcer" && state_name != "blocker" && state_name != "erase")
        return {{"error", "state must be enforcer, blocker, or erase"}};
    const json selector_json = args["selector"];
    const EnforcerBlockerType state = state_name == "enforcer" ? EnforcerBlockerType::ENFORCER :
                                      state_name == "blocker" ? EnforcerBlockerType::BLOCKER :
                                                               EnforcerBlockerType::NONE;
    return on_gui_thread([object_id, volume_id, instance_id, selector_json, state_name, state, support]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        if (!volume->is_model_part())
            throw std::runtime_error("Surface painting requires a model-part volume");
        ModelInstance* instance = require_object_instance(object, instance_id);
        const SurfaceSelectionResult selected =
            select_surface_facets(volume, instance, selector_json);
        if (selected.facet_ids.empty())
            return json{{"changed", false}, {"object_id", object_id}, {"volume_id", volume_id},
                        {"instance_id", instance_id}, {"paint_type", support ? "support" : "seam"},
                        {"state", state_name}, {"selection", surface_selection_to_json(selected, 1000)}};

        TriangleSelector paint(volume->mesh());
        if (support)
            paint.deserialize(volume->supported_facets.get_data(), false);
        else
            paint.deserialize(volume->seam_facets.get_data(), false);
        const json before = paint_state_to_json(paint, volume->mesh().facets_count());
        const auto serialized_before = paint.serialize();
        for (int facet_id : selected.facet_ids)
            paint.set_facet(facet_id, state);
        if (paint.serialize() == serialized_before)
            return json{{"changed", false}, {"object_id", object_id}, {"volume_id", volume_id},
                        {"instance_id", instance_id}, {"paint_type", support ? "support" : "seam"},
                        {"state", state_name}, {"selection", surface_selection_to_json(selected, 1000)},
                        {"before", before}, {"after", before}};

        plater->take_snapshot(support ? "MCP Paint Support Facets" : "MCP Paint Seam Facets");
        const bool changed = support ? volume->supported_facets.set(paint) : volume->seam_facets.set(paint);
        if (ObjectList* object_list = wxGetApp().obj_list(); object_list != nullptr)
            object_list->update_info_items(static_cast<size_t>(object_id));
        plater->schedule_background_process();
        const json after = paint_state_to_json(paint, volume->mesh().facets_count());
        return json{{"changed", changed}, {"object_id", object_id}, {"volume_id", volume_id},
                    {"instance_id", instance_id}, {"paint_type", support ? "support" : "seam"},
                    {"state", state_name}, {"coordinate_space", "build_plate"},
                    {"facet_id_space", "volume_source_mesh"},
                    {"applies_to_all_object_instances", true},
                    {"object_instance_count", object->instances.size()},
                    {"selection", surface_selection_to_json(selected, 1000)},
                    {"before", before}, {"after", after}, {"undo_available", plater->can_undo()}};
    });
}

json get_support_paint_state(const json& args) { return get_surface_paint_state(args, true); }
json get_seam_paint_state(const json& args) { return get_surface_paint_state(args, false); }
json set_support_paint(const json& args) { return set_surface_paint(args, true); }
json set_seam_paint(const json& args) { return set_surface_paint(args, false); }

bool string_ends_with(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string attached_file_host(const std::string& url)
{
    const std::string lowered = lower_copy(url);
    constexpr const char* prefix = "https://";
    if (lowered.rfind(prefix, 0) != 0)
        return {};

    const std::size_t authority_start = std::strlen(prefix);
    const std::size_t authority_end = lowered.find_first_of("/?#", authority_start);
    std::string authority = lowered.substr(authority_start, authority_end - authority_start);
    if (authority.empty() || authority.find('@') != std::string::npos || authority.front() == '[')
        return {};

    const std::size_t port = authority.find(':');
    if (port != std::string::npos) {
        if (authority.substr(port + 1) != "443")
            return {};
        authority.erase(port);
    }
    if (authority.empty())
        return {};
    return authority;
}

bool trusted_attached_file_url(const std::string& url)
{
    const std::string host = attached_file_host(url);
    if (host.empty())
        return false;
    if (host == "chatgpt.com" || string_ends_with(host, ".chatgpt.com") ||
        host == "openai.com" || string_ends_with(host, ".openai.com") ||
        host == "oaiusercontent.com" || string_ends_with(host, ".oaiusercontent.com"))
        return true;
    if (string_ends_with(host, ".blob.core.windows.net") &&
        (host.rfind("oaisdmntpr", 0) == 0 || host.rfind("oaisdsorpr", 0) == 0))
        return true;
    if (string_ends_with(host, ".amazonaws.com") && host.rfind("oaisdmntpr", 0) == 0)
        return true;
    return false;
}

std::string attached_model_extension(const json& file)
{
    std::string source = file.value("file_name", "");
    if (source.empty()) {
        const std::string url = file.value("download_url", "");
        const std::size_t query = url.find_first_of("?#");
        const std::string path = url.substr(0, query);
        const std::size_t slash = path.find_last_of('/');
        source = slash == std::string::npos ? path : path.substr(slash + 1);
    }

    std::string extension = lower_copy(boost::filesystem::path(source).extension().string());
    static const std::set<std::string> supported{
        ".stl", ".3mf", ".obj", ".amf", ".step", ".stp", ".ply"
    };
    return supported.count(extension) > 0 ? extension : std::string();
}

std::string safe_attached_model_name(const json& file, std::size_t index,
                                     const std::string& extension)
{
    std::string name = boost::filesystem::path(file.value("file_name", "")).filename().string();
    if (name.empty())
        name = "attached_model_" + std::to_string(index + 1) + extension;

    std::string safe;
    safe.reserve(std::min<std::size_t>(name.size(), 120));
    for (unsigned char character : name) {
        if (safe.size() >= 120)
            break;
        if (std::isalnum(character) || character == ' ' || character == '-' ||
            character == '_' || character == '.' || character == '(' || character == ')')
            safe.push_back(static_cast<char>(character));
        else
            safe.push_back('_');
    }
    if (safe.empty())
        safe = "attached_model_" + std::to_string(index + 1) + extension;
    if (lower_copy(boost::filesystem::path(safe).extension().string()) != extension)
        safe = boost::filesystem::path(safe).stem().string() + extension;
    return safe;
}

boost::filesystem::path attached_model_inbox()
{
#ifdef __WXMSW__
    wxString local_app_data;
    if (!wxGetEnv("LOCALAPPDATA", &local_app_data) || local_app_data.empty())
        throw std::runtime_error("Windows local application-data directory is unavailable");
    return boost::filesystem::path(into_u8(local_app_data)) / "QIDIStudio-MCP" / "model-inbox";
#else
    return boost::filesystem::temp_directory_path() / "QIDIStudio-MCP" / "model-inbox";
#endif
}

void cleanup_stale_attached_model_directories(const boost::filesystem::path& inbox)
{
    boost::system::error_code ec;
    if (!boost::filesystem::is_directory(inbox, ec))
        return;
    const std::time_t cutoff = std::time(nullptr) - 24 * 60 * 60;
    for (boost::filesystem::directory_iterator iterator(inbox, ec), end;
         !ec && iterator != end; iterator.increment(ec)) {
        boost::system::error_code entry_error;
        const boost::filesystem::path path = iterator->path();
        if (!boost::filesystem::is_directory(path, entry_error))
            continue;
        entry_error.clear();
        const std::time_t modified = boost::filesystem::last_write_time(path, entry_error);
        if (!entry_error && modified < cutoff)
            boost::filesystem::remove_all(path, entry_error);
    }
}

struct AttachedDownloadSink {
    boost::nowide::ofstream* output{nullptr};
    std::size_t bytes{0};
    std::size_t limit{0};
    bool limit_exceeded{false};
    bool write_failed{false};
};

std::size_t write_attached_model_data(char* data, std::size_t size, std::size_t count, void* context)
{
    AttachedDownloadSink* sink = static_cast<AttachedDownloadSink*>(context);
    if (sink == nullptr || sink->output == nullptr || size == 0)
        return 0;
    if (count > std::numeric_limits<std::size_t>::max() / size) {
        sink->limit_exceeded = true;
        return 0;
    }
    const std::size_t bytes = size * count;
    if (sink->bytes > sink->limit || bytes > sink->limit - sink->bytes) {
        sink->limit_exceeded = true;
        return 0;
    }
    sink->output->write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    if (!*sink->output) {
        sink->write_failed = true;
        return 0;
    }
    sink->bytes += bytes;
    return bytes;
}

struct AttachedDownloadResult {
    bool downloaded{false};
    std::size_t bytes{0};
    unsigned http_status{0};
    std::string error;
};

AttachedDownloadResult download_attached_model(const std::string& url,
                                               const boost::filesystem::path& destination)
{
    AttachedDownloadResult result;
    boost::nowide::ofstream output(destination.string(), std::ios::binary | std::ios::trunc);
    if (!output) {
        result.error = "QIDI Studio could not create the temporary attachment file";
        return result;
    }

    Http::tls_global_init();
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error = "QIDI Studio could not initialize the attachment download";
        return result;
    }

    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    AttachedDownloadSink sink{&output, 0, MAX_ATTACHED_MODEL_FILE_BYTES};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "QIDIStudio-MCP/1.10.0");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_attached_model_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE,
                     static_cast<curl_off_t>(MAX_ATTACHED_MODEL_FILE_BYTES));
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    output.close();

    result.bytes = sink.bytes;
    result.http_status = status > 0 ? static_cast<unsigned>(status) : 0;
    if (sink.limit_exceeded) {
        result.error = "The attached model exceeds the 256 MiB per-file limit";
    } else if (sink.write_failed) {
        result.error = "QIDI Studio could not write the attached model";
    } else if (code != CURLE_OK) {
        result.error = "The attached model download failed";
    } else if (status < 200 || status >= 300) {
        result.error = status >= 300 && status < 400
            ? "The attachment URL redirected; request a fresh direct ChatGPT file URL"
            : "The attachment host rejected the download";
    } else if (sink.bytes == 0) {
        result.error = "The attached model is empty";
    } else {
        result.downloaded = true;
    }
    return result;
}

struct ScopedAttachedModelDirectory {
    explicit ScopedAttachedModelDirectory(boost::filesystem::path value) : path(std::move(value)) {}
    ~ScopedAttachedModelDirectory() { remove(); }
    bool remove()
    {
        if (path.empty())
            return true;
        boost::system::error_code ec;
        boost::filesystem::remove_all(path, ec);
        if (!ec)
            path.clear();
        return !ec;
    }
    boost::filesystem::path path;
};

json import_model(const json& args);

json import_attached_models(const json& args)
{
    if (!args.contains("files") || !args["files"].is_array() || args["files"].empty())
        return {{"error", "files must be a non-empty array of ChatGPT file objects"}};
    if (args["files"].size() > MAX_ATTACHED_MODEL_FILES)
        return {{"error", "At most 8 attached model files may be imported at once"}};

    json ready = on_gui_thread([]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            return json{{"ready", false}, {"error", "Wait for the current QIDI Studio job to finish"}};
        return json{{"ready", true}};
    });
    if (!ready.value("ready", false))
        return ready;

    struct InputFile {
        std::string file_id;
        std::string download_url;
        std::string file_name;
        std::string mime_type;
        std::string extension;
    };
    std::vector<InputFile> inputs;
    inputs.reserve(args["files"].size());
    for (const json& file : args["files"]) {
        if (!file.is_object())
            return {{"error", "Every files entry must be a ChatGPT file object"}};
        const std::string file_id = file.value("file_id", "");
        const std::string download_url = file.value("download_url", "");
        if (file_id.empty() || download_url.empty())
            return {{"error", "Every attached file requires file_id and download_url"}};
        if (file_id.size() > 256 || download_url.size() > 16384)
            return {{"error", "An attached file identifier or URL is unexpectedly long"}};
        if (!trusted_attached_file_url(download_url))
            return {{"error", "The attached model URL is not a trusted ChatGPT/OpenAI HTTPS file URL"}};
        const std::string extension = attached_model_extension(file);
        if (extension.empty())
            return {{"error", "Unsupported or missing model extension; use STL, 3MF, OBJ, AMF, STEP, STP, or PLY"},
                    {"file_id", file_id}};
        inputs.push_back({file_id, download_url, file.value("file_name", ""),
                          file.value("mime_type", ""), extension});
    }

    const boost::filesystem::path inbox = attached_model_inbox();
    boost::system::error_code ec;
    boost::filesystem::create_directories(inbox, ec);
    if (ec)
        return {{"error", "QIDI Studio could not create its local model-inbox directory"}};
    cleanup_stale_attached_model_directories(inbox);

    const boost::filesystem::path request_directory = inbox / ("request-" + random_hex_id(16));
    boost::filesystem::create_directory(request_directory, ec);
    if (ec)
        return {{"error", "QIDI Studio could not create a temporary attachment directory"}};
    ScopedAttachedModelDirectory cleanup(request_directory);

    std::vector<std::string> local_paths;
    local_paths.reserve(inputs.size());
    json downloaded_files = json::array();
    std::size_t total_bytes = 0;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const InputFile& input = inputs[index];
        std::string safe_name = safe_attached_model_name(args["files"][index], index, input.extension);
        boost::filesystem::path final_path = request_directory / safe_name;
        if (boost::filesystem::exists(final_path)) {
            safe_name = std::to_string(index + 1) + "_" + safe_name;
            final_path = request_directory / safe_name;
        }
        const boost::filesystem::path partial_path(final_path.string() + ".partial");
        const AttachedDownloadResult download = download_attached_model(input.download_url, partial_path);
        if (!download.downloaded) {
            boost::filesystem::remove(partial_path, ec);
            json error = {{"error", download.error}, {"file_id", input.file_id},
                          {"http_status", download.http_status > 0 ? json(download.http_status) : json(nullptr)}};
            return error;
        }
        if (total_bytes > MAX_ATTACHED_MODEL_TOTAL_BYTES ||
            download.bytes > MAX_ATTACHED_MODEL_TOTAL_BYTES - total_bytes) {
            boost::filesystem::remove(partial_path, ec);
            return {{"error", "Attached models exceed the 512 MiB total limit"}};
        }
        total_bytes += download.bytes;
        ec.clear();
        boost::filesystem::rename(partial_path, final_path, ec);
        if (ec)
            return {{"error", "QIDI Studio could not finalize a downloaded attachment"},
                    {"file_id", input.file_id}};
        local_paths.push_back(final_path.string());
        downloaded_files.push_back({{"file_id", input.file_id},
                                    {"file_name", input.file_name.empty() ? json(safe_name) : json(input.file_name)},
                                    {"mime_type", input.mime_type.empty() ? json(nullptr) : json(input.mime_type)},
                                    {"bytes", download.bytes}, {"extension", input.extension}});
    }

    json import = import_model({{"paths", local_paths}, {"arrange", args.value("arrange", false)}});
    const bool temporary_files_removed = cleanup.remove();
    if (import.contains("error")) {
        import["downloaded_files"] = std::move(downloaded_files);
        import["temporary_files_removed"] = temporary_files_removed;
        return import;
    }
    import["source"] = "chatgpt_file_params";
    import["downloaded_files"] = std::move(downloaded_files);
    import["downloaded_bytes"] = total_bytes;
    import["temporary_files_removed"] = temporary_files_removed;
    import["next_recommended_tools"] = json::array({"list_objects", "get_model_diagnostics",
                                                     "analyze_printability"});
    return import;
}

json import_model(const json& args)
{
    if (!args.contains("paths") || !args["paths"].is_array() || args["paths"].empty())
        return {{"error", "paths must be a non-empty array of file paths"}};

    std::vector<std::string> paths;
    for (const json& path : args["paths"]) {
        if (!path.is_string() || path.get<std::string>().empty())
            return {{"error", "Every path must be a non-empty string"}};
        paths.push_back(path.get<std::string>());
    }
    const bool arrange_after = args.value("arrange", false);

    return on_gui_thread([paths = std::move(paths), arrange_after]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        const std::vector<size_t> object_ids =
            plater->load_files(paths, LoadStrategy::LoadModel, false);
        if (object_ids.empty())
            return json{{"error", "No model geometry was imported"}};
        if (arrange_after)
            plater->arrange();
        return json{
            {"imported_object_ids", object_ids},
            {"arrange_started", arrange_after}
        };
    });
}

json list_presets(const json& args)
{
    const std::string requested_scope = args.value("scope", "all");
    const size_t offset = args.value("offset", size_t{0});
    const size_t limit = std::min(args.value("limit", size_t{25}), size_t{500});
    return on_gui_thread([requested_scope, offset, limit]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");

        json result = json::object();
        const std::array<std::pair<const char*, Preset::Type>, 3> scopes{{
            {"print", Preset::TYPE_PRINT},
            {"filament", Preset::TYPE_FILAMENT},
            {"printer", Preset::TYPE_PRINTER}
        }};
        bool matched = false;
        for (const auto& scope : scopes) {
            if (requested_scope != "all" && requested_scope != scope.first)
                continue;
            matched = true;
            PresetCollection& collection = preset_collection(*bundle, scope.second);
            const auto& collection_presets = collection.get_presets();

            if (scope.second == Preset::TYPE_FILAMENT) {
                const size_t begin = std::min(offset, collection_presets.size());
                const size_t end = std::min(begin + std::min(limit, collection_presets.size() - begin),
                                            collection_presets.size());
                const std::string active = collection.get_selected_preset_name();
                json presets = json::array();
                for (size_t i = begin; i < end; ++i)
                    presets.push_back(preset_to_json(collection_presets[i], active));

                result[scope.first] = {
                    {"count", collection_presets.size()},
                    {"offset", begin},
                    {"returned", end - begin},
                    {"presets", std::move(presets)}
                };
                continue;
            }

            const std::string active = collection.get_selected_preset_name();
            json presets = json::array();
            for (const Preset& preset : collection_presets)
                presets.push_back(preset_to_json(preset, active));
            result[scope.first] = std::move(presets);
        }
        if (!matched)
            throw std::runtime_error("scope must be all, print, filament, or printer");
        return json{{"presets", std::move(result)}};
    });
}

json get_active_presets()
{
    return on_gui_thread([]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        return json{{"active", {
            {"printer", bundle->printers.get_selected_preset_name()},
            {"print", bundle->prints.get_selected_preset_name()},
            {"filaments", bundle->filament_presets}
        }}};
    });
}

json get_project_state()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        auto& plates = plater->get_partplate_list();
        return json{{"name", into_u8(plater->get_project_name())},
                    {"filename", into_u8(plater->get_project_filename())},
                    {"project_dirty", plater->is_project_dirty()},
                    {"presets_dirty", plater->is_presets_dirty()},
                    {"object_count", plater->model().objects.size()},
                    {"plate_count", plates.get_plate_count()},
                    {"active_plate", plates.get_curr_plate_index()}};
    });
}

json new_project(const json& args)
{
    const std::string name = args.value("name", "");
    return on_gui_thread([name]() {
        Plater* plater = require_plater();
        if (plater->is_project_dirty() || plater->is_presets_dirty())
            throw std::runtime_error("Refusing to replace a dirty project; save or discard changes in QIDI Studio first");
        const int result = plater->new_project(true, true, name.empty() ? wxString() : from_u8(name));
        return json{{"created", result == wxID_YES}, {"result", result},
                    {"name", into_u8(plater->get_project_name())}};
    });
}

json load_project(const json& args)
{
    const std::string path = required_string(args, "path");
    return on_gui_thread([path]() {
        Plater* plater = require_plater();
        if (plater->is_project_dirty() || plater->is_presets_dirty())
            throw std::runtime_error("Refusing to replace a dirty project; save or discard changes in QIDI Studio first");
        const int result = plater->load_project(from_u8(path), "<silence>");
        if (result == wxID_CANCEL)
            throw std::runtime_error("QIDI Studio cancelled the project load");
        return json{{"loaded", true}, {"path", path},
                    {"name", into_u8(plater->get_project_name())},
                    {"object_count", plater->model().objects.size()}};
    });
}

json export_project_3mf(const json& args)
{
    const std::string path = required_string(args, "path");
    return on_gui_thread([path]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        const auto strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::ShareMesh;
        const int result = plater->export_3mf(boost::filesystem::path(path), strategy);
        if (result < 0)
            throw std::runtime_error("QIDI Studio failed to export the 3MF project");
        return json{{"exported", true}, {"path", path}};
    });
}

json list_plates()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        auto& plate_list = plater->get_partplate_list();
        json plates = json::array();
        for (PartPlate* plate : plate_list.get_plate_list()) {
            if (plate == nullptr) continue;
            plates.push_back({{"index", plate->get_index()}, {"name", plate->get_plate_name()},
                {"active", plate->get_index() == plate_list.get_curr_plate_index()},
                {"empty", plate->empty()}, {"locked", plate->is_locked()},
                {"can_slice", plate->can_slice()}, {"slice_valid", plate->is_slice_result_valid()},
                {"instance_count", plate->get_obj_and_inst_set().size()}});
        }
        return json{{"count", plate_list.get_plate_count()}, {"plates", std::move(plates)}};
    });
}

json add_plate()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        auto& plates = plater->get_partplate_list();
        plates.add_plate();
        plater->update();
        return json{{"added", true}, {"plate_index", plates.get_curr_plate_index()},
                    {"plate_count", plates.get_plate_count()}};
    });
}

json select_plate(const json& args)
{
    const int index = required_int(args, "plate_index");
    return on_gui_thread([index]() {
        Plater* plater = require_plater();
        auto& plates = plater->get_partplate_list();
        if (index < 0 || index >= plates.get_plate_count())
            throw std::runtime_error("plate_index is out of range");
        if (plater->select_plate(index) != 0)
            throw std::runtime_error("QIDI Studio could not select the plate");
        return json{{"selected", true}, {"plate_index", index}};
    });
}

json rename_plate(const json& args)
{
    const int index = required_int(args, "plate_index");
    const std::string name = required_string(args, "name");
    return on_gui_thread([index, name]() {
        Plater* plater = require_plater();
        auto& plates = plater->get_partplate_list();
        if (index < 0 || index >= plates.get_plate_count())
            throw std::runtime_error("plate_index is out of range");
        PartPlate* plate = plates.get_plate(index);
        if (plate == nullptr) throw std::runtime_error("Plate was not found");
        plate->set_plate_name(name);
        plater->update();
        return json{{"renamed", true}, {"plate_index", index}, {"name", plate->get_plate_name()}};
    });
}

json delete_plate(const json& args)
{
    const int index = required_int(args, "plate_index");
    if (!args.value("confirm", false))
        return {{"error", "Set confirm=true to delete a plate"}};
    return on_gui_thread([index]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        auto& plates = plater->get_partplate_list();
        if (index < 0 || index >= plates.get_plate_count())
            throw std::runtime_error("plate_index is out of range");
        if (plates.get_plate_count() <= 1)
            throw std::runtime_error("QIDI Studio requires at least one plate");
        if (plater->delete_plate(index) != 0)
            throw std::runtime_error("QIDI Studio could not delete the plate");
        return json{{"deleted", true}, {"plate_index", index}, {"plate_count", plates.get_plate_count()}};
    });
}

json list_slice_settings(const json& args)
{
    const std::string scope = args.value("scope", "print");
    const std::string query = lower_copy(args.value("query", ""));
    const size_t offset = args.value("offset", size_t{0});
    const size_t limit = std::min(args.value("limit", size_t{100}), size_t{500});
    return on_gui_thread([scope, query, offset, limit]() {
        DynamicPrintConfig* config = require_tab(preset_type(scope))->get_config();
        std::vector<std::string> keys;
        for (const std::string& key : config->keys())
            if (query.empty() || lower_copy(key).find(query) != std::string::npos) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        const size_t begin = std::min(offset, keys.size());
        const size_t end = std::min(begin + limit, keys.size());
        json settings = json::array();
        for (size_t i = begin; i < end; ++i) {
            const ConfigOption* option = config->option(keys[i]);
            if (option != nullptr)
                settings.push_back({{"key", keys[i]},
                                    {"value", serialize_config_option(keys[i], option)}});
        }
        return json{{"scope", scope}, {"count", keys.size()}, {"offset", begin},
                    {"returned", end - begin}, {"settings", std::move(settings)}};
    });
}

json set_object_printable(const json& args)
{
    const int object_id = required_int(args, "object_id");
    if (!args.contains("printable") || !args["printable"].is_boolean())
        return {{"error", "printable must be a boolean"}};
    const bool printable = args["printable"].get<bool>();
    return on_gui_thread([object_id, printable]() {
        Plater* plater = require_plater();
        if (object_id < 0 || static_cast<size_t>(object_id) >= plater->model().objects.size())
            throw std::runtime_error("Object was not found");
        ModelObject* object = plater->model().objects[object_id];
        if (object == nullptr) throw std::runtime_error("Object was not found");
        plater->take_snapshot(printable ? "MCP Set Object Printable" : "MCP Set Object Unprintable");
        object->printable = printable;
        for (ModelInstance* instance : object->instances)
            if (instance != nullptr) instance->printable = printable;
        plater->changed_object(object_id);
        plater->object_list_changed();
        return json{{"object_id", object_id}, {"printable", printable}};
    });
}

json set_instance_printable(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = required_int(args, "instance_id");
    if (!args.contains("printable") || !args["printable"].is_boolean())
        return {{"error", "printable must be a boolean"}};
    const bool printable = args["printable"].get<bool>();
    return on_gui_thread([object_id, instance_id, printable]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        ModelObject* object = require_model_object(plater, object_id);
        plater->take_snapshot(printable ? "MCP Set Instance Printable" : "MCP Set Instance Unprintable");
        instance->printable = printable;
        object->printable = std::any_of(object->instances.begin(), object->instances.end(),
            [](const ModelInstance* candidate) { return candidate != nullptr && candidate->is_printable(); });
        plater->get_partplate_list().notify_instance_update(object_id, instance_id);
        plater->changed_object(object_id);
        plater->object_list_changed();
        return json{{"object_id", object_id}, {"instance_id", instance_id},
                    {"printable", printable}, {"object_printable", object->printable}};
    });
}

json open_send_to_printer()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (!plate->is_slice_result_valid() || !plate->is_slice_result_ready_for_print())
            throw std::runtime_error("Slice the active plate and wait for a print-ready result first");
        plater->send_to_printer(false);
        return json{{"opened", true}, {"plate_index", plate->get_index()}};
    });
}

json control_printer(QDSDeviceManager* manager, const json& args)
{
    if (manager == nullptr) return {{"error", "QIDI device manager is not available"}};
    const std::string device_id = required_string(args, "device_id");
    const std::string action = args.value("action", "");
    if (action != "pause" && action != "resume" && action != "cancel")
        return {{"error", "action must be pause, resume, or cancel"}};
    if (action == "cancel" && !args.value("confirm", false))
        return {{"error", "Set confirm=true to cancel an active print"}};
    std::shared_ptr<QDSDevice> device = manager->getDevice(device_id);
    if (!device) return {{"error", "Printer was not found"}};
    if (!device->is_online()) return {{"error", "Printer is offline"}};
    manager->sendCommand(device_id, action);
    manager->sendActionCommand(device_id, action);
    return {{"accepted", true}, {"device_id", device_id}, {"action", action}};
}

json select_presets(const json& args)
{
    if (!args.is_object())
        return {{"error", "Arguments must be an object"}};
    return on_gui_thread([args]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        json selected = json::object();
        struct PendingPreset { const char* key; Preset::Type type; std::string name; };
        std::vector<PendingPreset> pending;
        for (const auto& item : std::array<std::pair<const char*, Preset::Type>, 3>{{
                 {"printer", Preset::TYPE_PRINTER},
                 {"print", Preset::TYPE_PRINT},
                 {"filament", Preset::TYPE_FILAMENT}}}) {
            if (!args.contains(item.first))
                continue;
            if (!args[item.first].is_string() || args[item.first].get<std::string>().empty())
                throw std::runtime_error(std::string(item.first) + " must be a non-empty preset name");
            const std::string name = args[item.first].get<std::string>();
            Tab* tab = require_tab(item.second);
            if (tab->get_presets()->find_preset(name) == nullptr)
                throw std::runtime_error(std::string("Preset not found: ") + name);
            if (tab->current_preset_is_dirty())
                throw std::runtime_error(std::string("Save or discard modified ") + item.first +
                                         " settings before selecting another preset");
            pending.push_back({item.first, item.second, name});
        }
        if (pending.empty())
            throw std::runtime_error("Provide at least one of: printer, print, filament");
        for (const PendingPreset& change : pending) {
            if (!require_tab(change.type)->select_preset(change.name, false, "", true, true))
                throw std::runtime_error("Could not select preset: " + change.name);
            selected[change.key] = change.name;
        }
        return json{{"selected", std::move(selected)}};
    });
}

json set_project_filament(const json& args)
{
    if (!args.contains("filament_index") || !args["filament_index"].is_number_integer())
        return {{"error", "filament_index must be a non-negative integer"}};
    const int filament_index = args["filament_index"].get<int>();
    if (filament_index < 0)
        return {{"error", "filament_index must be a non-negative integer"}};
    if (!args.contains("preset") || !args["preset"].is_string() ||
        args["preset"].get<std::string>().empty())
        return {{"error", "preset must be a non-empty filament preset name"}};
    const std::string preset_name = args["preset"].get<std::string>();

    return on_gui_thread([filament_index, preset_name]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        const size_t index = static_cast<size_t>(filament_index);
        if (index >= bundle->filament_presets.size())
            throw std::runtime_error("Project filament slot is out of range");
        const Preset* preset = bundle->filaments.find_preset(preset_name, false);
        if (preset == nullptr)
            throw std::runtime_error("Filament preset not found: " + preset_name);
        if (!preset->is_compatible)
            throw std::runtime_error("Filament preset is not compatible with the active printer: " + preset_name);

        const std::string old_name = bundle->filament_presets[index];
        if (old_name == preset_name)
            return json{{"filament_index", index}, {"old_preset", old_name},
                        {"preset", preset_name}, {"changed", false}};

        bundle->set_filament_preset(index, preset_name);
        if (!plater->on_filament_change(index)) {
            bundle->set_filament_preset(index, old_name);
            throw std::runtime_error("QIDI Studio rejected the filament change");
        }
        plater->update_project_dirty_from_presets();
        bundle->export_selections(*wxGetApp().app_config);
        plater->on_config_change(bundle->full_config());
        plater->sidebar().refresh_filament_presets();
        return json{{"filament_index", index}, {"old_preset", old_name},
                    {"preset", bundle->filament_presets[index]}, {"changed", true},
                    {"slot_count", bundle->filament_presets.size()}};
    });
}

json get_slice_settings(const json& args)
{
    const std::string scope = args.value("scope", "print");
    if (!args.contains("keys") || !args["keys"].is_array() || args["keys"].empty())
        return {{"error", "keys must be a non-empty array"}};

    int filament_index = -1;
    if (args.contains("filament_index")) {
        if (!args["filament_index"].is_number_integer())
            return {{"error", "filament_index must be a non-negative integer"}};
        filament_index = args["filament_index"].get<int>();
        if (filament_index < 0)
            return {{"error", "filament_index must be a non-negative integer"}};
        if (scope != "filament")
            return {{"error", "filament_index is only valid for filament scope"}};
    }

    const json keys = args["keys"];
    return on_gui_thread([scope, keys, filament_index]() {
        json values = json::object();

        if (scope == "filament") {
            PresetBundle* bundle = wxGetApp().preset_bundle;
            if (bundle == nullptr)
                throw std::runtime_error("Preset bundle is not available");

            const size_t count = bundle->filament_presets.size();
            if (count == 0)
                throw std::runtime_error("Project has no filament slots");

            size_t index = 0;
            if (filament_index >= 0) {
                index = static_cast<size_t>(filament_index);
                if (index >= count)
                    throw std::runtime_error(
                        "filament_index is out of range; project has " +
                        std::to_string(count) + " filament slots");
            } else if (count != 1) {
                throw std::runtime_error(
                    "Project has multiple filament slots; provide filament_index");
            }

            const std::string& preset_name = bundle->filament_presets[index];
            const Preset* preset = bundle->filaments.find_preset(preset_name, false);
            if (preset == nullptr)
                throw std::runtime_error(
                    "Assigned filament preset not found: " + preset_name);

            for (const json& key_json : keys) {
                if (!key_json.is_string())
                    throw std::runtime_error("Every setting key must be a string");
                const std::string key = key_json.get<std::string>();
                const ConfigOption* option = preset->config.option(key);
                if (option == nullptr)
                    throw std::runtime_error("Unknown setting for filament: " + key);
                values[key] = serialize_config_option(key, option);
            }

            return json{
                {"scope", "filament"},
                {"filament_index", index},
                {"preset", preset_name},
                {"values", std::move(values)}
            };
        }

        DynamicPrintConfig* config = require_tab(preset_type(scope))->get_config();
        for (const json& key_json : keys) {
            if (!key_json.is_string())
                throw std::runtime_error("Every setting key must be a string");
            const std::string key = key_json.get<std::string>();
            const ConfigOption* option = config->option(key);
            if (option == nullptr)
                throw std::runtime_error("Unknown setting for " + scope + ": " + key);
            values[key] = serialize_config_option(key, option);
        }
        return json{{"scope", scope}, {"values", std::move(values)}};
    });
}

json set_slice_settings(const json& args)
{
    const std::string scope = args.value("scope", "print");
    if (!args.contains("values") || !args["values"].is_object() || args["values"].empty())
        return {{"error", "values must be a non-empty object of serialized setting strings"}};
    const json values = args["values"];
    return on_gui_thread([scope, values]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        Tab* tab = require_tab(preset_type(scope));
        DynamicPrintConfig updated(*tab->get_config());
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.value().is_string())
                throw std::runtime_error("Setting values must use QIDI's serialized string format");
            if (updated.option(it.key()) == nullptr)
                throw std::runtime_error("Unknown setting for " + scope + ": " + it.key());
            repair_config_option_enum_map(it.key(), updated.option(it.key()));
            updated.set_deserialize_strict(it.key(), it.value().get<std::string>());
        }
        tab->load_config(updated);

        json applied = json::object();
        for (auto it = values.begin(); it != values.end(); ++it)
            applied[it.key()] = serialize_config_option(
                it.key(), tab->get_config()->option(it.key()));
        return json{{"scope", scope}, {"applied", std::move(applied)}};
    });
}

json reset_slice_settings(const json& args)
{
    const std::string scope = args.value("scope", "print");
    if (!args.contains("keys") || !args["keys"].is_array() || args["keys"].empty())
        return {{"error", "keys must be a non-empty array"}};
    const json keys = args["keys"];
    return on_gui_thread([scope, keys]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        Tab* tab = require_tab(preset_type(scope));
        PresetCollection* presets = tab->get_presets();
        if (presets == nullptr)
            throw std::runtime_error("Preset collection is not available");
        const Preset& saved = presets->get_selected_preset();
        DynamicPrintConfig updated(*tab->get_config());
        json reset = json::object();
        for (const json& key_json : keys) {
            if (!key_json.is_string())
                throw std::runtime_error("Every setting key must be a string");
            const std::string key = key_json.get<std::string>();
            if (updated.option(key) == nullptr)
                throw std::runtime_error("Unknown setting for " + scope + ": " + key);
            const ConfigOption* saved_option = saved.config.option(key);
            if (saved_option == nullptr)
                throw std::runtime_error("The saved preset has no value for " + scope + " setting: " + key);
            updated.set_key_value(key, saved_option->clone());
            reset[key] = serialize_config_option(key, saved_option);
        }
        tab->load_config(updated);
        return json{{"scope", scope}, {"preset", saved.name}, {"reset", std::move(reset)}};
    });
}

json save_preset_as(const json& args)
{
    const std::string scope = args.value("scope", "print");
    const std::string name = required_string(args, "name");
    const bool overwrite = args.value("overwrite", false);
    const bool save_to_project = args.value("save_to_project", false);
    if (name.find_first_not_of(" \t\r\n") == std::string::npos)
        return {{"error", "name must contain a non-whitespace character"}};
    if (Plater::has_illegal_filename_characters(name))
        return {{"error", "name contains characters QIDI Studio does not allow in preset names"}};

    return on_gui_thread([scope, name, overwrite, save_to_project]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        Tab* tab = require_tab(preset_type(scope));
        PresetCollection* presets = tab->get_presets();
        if (presets == nullptr)
            throw std::runtime_error("Preset collection is not available");
        Preset* existing = presets->find_preset(name, false);
        if (existing != nullptr && !overwrite)
            throw std::runtime_error("Preset already exists; pass overwrite=true to replace a user preset");
        if (existing != nullptr && (existing->is_system || existing->is_default))
            throw std::runtime_error("Refusing to overwrite a system or default preset; choose a new name");

        tab->save_preset(name, false, save_to_project);
        Preset* saved = presets->find_preset(name, false, true);
        if (saved == nullptr)
            throw std::runtime_error("QIDI Studio did not create the requested preset");
        return json{{"saved", true}, {"scope", scope}, {"name", saved->name},
                    {"project_embedded", saved->is_project_embedded},
                    {"active", presets->get_edited_preset().name == saved->name}};
    });
}

void apply_vec3_fields(Vec3d& value, const json& fields)
{
    if (!fields.is_object())
        throw std::runtime_error("Transform component must be an object");
    if (fields.contains("x")) value.x() = fields["x"].get<double>();
    if (fields.contains("y")) value.y() = fields["y"].get<double>();
    if (fields.contains("z")) value.z() = fields["z"].get<double>();
}

json transform_object(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = required_int(args, "instance_id");
    return on_gui_thread([args, object_id, instance_id]() {
        Plater* plater = require_plater();
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        ModelObject* object = instance->get_object();
        if (object == nullptr)
            throw std::runtime_error("Object was not found");
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");

        Vec3d position = instance->get_offset();
        Vec3d rotation = instance->get_rotation();
        Vec3d scale = instance->get_scaling_factor();
        if (args.contains("position_mm")) {
            apply_vec3_fields(position, args["position_mm"]);
        }
        if (args.contains("rotation_rad")) {
            apply_vec3_fields(rotation, args["rotation_rad"]);
        }
        if (args.contains("scale")) {
            apply_vec3_fields(scale, args["scale"]);
            if ((scale.array() <= 0.0).any())
                throw std::runtime_error("Scale values must be greater than zero");
        }
        if (!args.contains("position_mm") && !args.contains("rotation_rad") && !args.contains("scale"))
            throw std::runtime_error("Provide position_mm, rotation_rad, and/or scale");

        plater->take_snapshot("MCP Transform Object");
        instance->set_offset(position);
        instance->set_rotation(rotation);
        instance->set_scaling_factor(scale);
        plater->get_partplate_list().notify_instance_update(object_id, instance_id);
        const bool explicit_z = args.contains("position_mm") && args["position_mm"].is_object() &&
                                args["position_mm"].contains("z");
        if (explicit_z) {
            // changed_object() always calls ensure_on_bed(), which would silently
            // discard an explicitly requested positive Z translation.  Explicit Z
            // is authoritative, so refresh QIDI without the auto-drop step.
            object->invalidate_bounding_box();
            plater->update();
        }
        else {
            plater->changed_object(object_id);
        }
        position = instance->get_offset();
        rotation = instance->get_rotation();
        scale = instance->get_scaling_factor();
        return json{{"object_id", object_id}, {"instance_id", instance_id},
            {"position_mm", {{"x", position.x()}, {"y", position.y()}, {"z", position.z()}}},
            {"rotation_rad", {{"x", rotation.x()}, {"y", rotation.y()}, {"z", rotation.z()}}},
            {"scale", {{"x", scale.x()}, {"y", scale.y()}, {"z", scale.z()}}}};
    });
}

json duplicate_object(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    const int copies = args.value("copies", 1);
    if (copies < 1 || copies > 100)
        return {{"error", "copies must be between 1 and 100"}};

    return on_gui_thread([args, object_id, instance_id, copies]() {
        Plater* plater = require_plater();
        ModelInstance* source = require_instance(require_plate(plater), object_id, instance_id);
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = source->get_object();
        if (object == nullptr)
            throw std::runtime_error("Source object is not available");

        Vec3d step = Vec3d::Zero();
        if (args.contains("offset_step_mm"))
            apply_vec3_fields(step, args["offset_step_mm"]);
        json instance_ids = json::array();
        plater->take_snapshot("MCP Duplicate Object");
        for (int copy = 1; copy <= copies; ++copy) {
            ModelInstance* added = object->add_instance(*source);
            added->set_offset(source->get_offset() + step * static_cast<double>(copy));
            const int new_instance_id = static_cast<int>(object->instances.size() - 1);
            plater->get_partplate_list().notify_instance_update(object_id, new_instance_id, true);
            instance_ids.push_back(new_instance_id);
        }
        plater->changed_object(object_id);
        plater->object_list_changed();
        return json{{"object_id", object_id}, {"new_instance_ids", std::move(instance_ids)}};
    });
}

json rename_object(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const std::string name = required_string(args, "name");
    if (name.find_first_not_of(" \t\r\n") == std::string::npos)
        return {{"error", "name must contain a non-whitespace character"}};
    if (Plater::has_illegal_filename_characters(name))
        return {{"error", "name contains characters QIDI Studio does not allow in object names"}};
    return on_gui_thread([object_id, name]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        plater->take_snapshot("MCP Rename Object");
        object->name = name;
        if (object->volumes.size() == 1 && object->volumes.front() != nullptr)
            object->volumes.front()->name = name;
        ObjectList* object_list = wxGetApp().obj_list();
        if (object_list != nullptr)
            object_list->update_name_for_items();
        plater->object_list_changed();
        return json{{"renamed", true}, {"object_id", object_id}, {"name", object->name}};
    });
}

json rename_volume(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    const std::string name = required_string(args, "name");
    if (name.find_first_not_of(" \t\r\n") == std::string::npos)
        return {{"error", "name must contain a non-whitespace character"}};
    if (Plater::has_illegal_filename_characters(name))
        return {{"error", "name contains characters QIDI Studio does not allow in volume names"}};
    return on_gui_thread([object_id, volume_id, name]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        plater->take_snapshot("MCP Rename Volume");
        volume->name = name;
        if (ObjectList* object_list = wxGetApp().obj_list(); object_list != nullptr)
            object_list->update_name_for_items();
        plater->object_list_changed();
        return json{{"renamed", true}, {"object_id", object_id},
                    {"volume_id", volume_id}, {"name", volume->name}};
    });
}

json delete_instance(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = required_int(args, "instance_id");
    if (!args.value("confirm", false))
        return {{"error", "Set confirm=true to delete an object instance"}};
    return on_gui_thread([object_id, instance_id]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        require_instance(require_plate(plater), object_id, instance_id);
        if (object->instances.size() <= 1)
            throw std::runtime_error("The last instance cannot be deleted; use delete_object instead");
        ObjectList* object_list = wxGetApp().obj_list();
        if (object_list == nullptr)
            throw std::runtime_error("QIDI object list is not available");
        const size_t before_count = object->instances.size();
        object_list->delete_from_model_and_list(itInstance, object_id, instance_id);
        if (object->instances.size() + 1 != before_count)
            throw std::runtime_error("QIDI Studio did not delete the requested instance");
        plater->object_list_changed();
        return json{{"deleted", true}, {"object_id", object_id}, {"instance_id", instance_id},
                    {"remaining_instances", object->instances.size()}};
    });
}

json delete_volume(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    if (!args.value("confirm", false))
        return {{"error", "Set confirm=true to delete a model volume"}};
    return on_gui_thread([object_id, volume_id]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        if (object->volumes.size() <= 1)
            throw std::runtime_error("The last volume cannot be deleted; use delete_object instead");
        if (volume->is_model_part()) {
            const size_t solid_parts = static_cast<size_t>(std::count_if(
                object->volumes.begin(), object->volumes.end(),
                [](const ModelVolume* candidate) { return candidate != nullptr && candidate->is_model_part(); }));
            if (solid_parts <= 1)
                throw std::runtime_error("The last solid model part cannot be deleted while modifier volumes remain");
        }
        ObjectList* object_list = wxGetApp().obj_list();
        if (object_list == nullptr)
            throw std::runtime_error("QIDI object list is not available");
        const size_t before_count = object->volumes.size();
        object_list->delete_from_model_and_list(itVolume, object_id, volume_id);
        object = require_model_object(plater, object_id);
        if (object->volumes.size() + 1 != before_count)
            throw std::runtime_error("QIDI Studio did not delete the requested volume");
        plater->object_list_changed();
        return json{{"deleted", true}, {"object_id", object_id}, {"volume_id", volume_id},
                    {"remaining_volumes", object->volumes.size()}};
    });
}

json set_object_extruder(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int extruder = required_int(args, "extruder");
    return on_gui_thread([object_id, extruder]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        const size_t filament_count = bundle->filament_presets.size();
        if (extruder < 1 || static_cast<size_t>(extruder) > filament_count)
            throw std::runtime_error("extruder must be a one-based index of a filament loaded in the project");
        ModelObject* object = require_model_object(plater, object_id);
        plater->take_snapshot("MCP Set Object Extruder");
        object->config.set_key_value("extruder", new ConfigOptionInt(extruder));
        for (ModelVolume* volume : object->volumes)
            if (volume != nullptr && volume->config.has("extruder"))
                volume->config.erase("extruder");
        ObjectList* object_list = wxGetApp().obj_list();
        if (object_list != nullptr)
            object_list->update_filament_values_for_items(filament_count);
        plater->changed_object(object_id);
        plater->object_list_changed();
        return json{{"object_id", object_id}, {"extruder", object->config.extruder()},
                    {"project_filament_index", extruder - 1},
                    {"filament_preset", bundle->filament_presets[static_cast<size_t>(extruder - 1)]},
                    {"reslice_required", true}};
    });
}

json set_volume_extruder(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    const int extruder = required_int(args, "extruder");
    return on_gui_thread([object_id, volume_id, extruder]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        const size_t filament_count = bundle->filament_presets.size();
        if (extruder < 1 || static_cast<size_t>(extruder) > filament_count)
            throw std::runtime_error("extruder must be a one-based index of a filament loaded in the project");
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        plater->take_snapshot("MCP Set Volume Extruder");
        volume->config.set_key_value("extruder", new ConfigOptionInt(extruder));
        if (ObjectList* object_list = wxGetApp().obj_list(); object_list != nullptr)
            object_list->update_filament_values_for_items(filament_count);
        plater->changed_object(object_id);
        plater->object_list_changed();
        return json{{"object_id", object_id}, {"volume_id", volume_id}, {"extruder", extruder},
                    {"project_filament_index", extruder - 1},
                    {"filament_preset", bundle->filament_presets[static_cast<size_t>(extruder - 1)]},
                    {"reslice_required", true}};
    });
}

json set_volume_type(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int volume_id = required_int(args, "volume_id");
    const std::string type_name = required_string(args, "type");
    if (type_name != "normal_part" && type_name != "negative_part" &&
        type_name != "modifier_part" && type_name != "support_blocker" &&
        type_name != "support_enforcer")
        return {{"error", "type must be normal_part, negative_part, modifier_part, support_blocker, or support_enforcer"}};
    const ModelVolumeType new_type = ModelVolume::type_from_string(type_name);

    return on_gui_thread([object_id, volume_id, type_name, new_type]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ModelVolume* volume = require_model_volume(object, volume_id);
        if (volume->is_cut_connector() && new_type != ModelVolumeType::MODEL_PART &&
            new_type != ModelVolumeType::NEGATIVE_VOLUME)
            throw std::runtime_error("Cut connectors can only be normal or negative parts");
        if (volume->is_svg() && (new_type == ModelVolumeType::SUPPORT_BLOCKER ||
                                 new_type == ModelVolumeType::SUPPORT_ENFORCER))
            throw std::runtime_error("SVG volumes cannot be support blockers or enforcers");
        if (volume->is_model_part() && new_type != ModelVolumeType::MODEL_PART) {
            const size_t solid_parts = static_cast<size_t>(std::count_if(
                object->volumes.begin(), object->volumes.end(),
                [](const ModelVolume* candidate) { return candidate != nullptr && candidate->is_model_part(); }));
            if (solid_parts <= 1)
                throw std::runtime_error("The type of the last solid model part cannot be changed");
        }
        if (volume->type() == new_type)
            return json{{"changed", false}, {"object_id", object_id},
                        {"volume_id", volume_id}, {"type", type_name}};

        ObjectList* object_list = wxGetApp().obj_list();
        if (object_list == nullptr)
            throw std::runtime_error("QIDI object list is not available");
        ObjectVolumeID selection;
        selection.object = object;
        selection.volume = volume;
        object_list->select_item(selection);
        object_list->set_volume_type(new_type);

        int new_volume_id = -1;
        for (size_t index = 0; index < object->volumes.size(); ++index)
            if (object->volumes[index] == volume) new_volume_id = static_cast<int>(index);
        if (new_volume_id < 0 || volume->type() != new_type)
            throw std::runtime_error("QIDI Studio did not change the requested volume type");
        plater->changed_mesh(object_id);
        plater->object_list_changed();
        return json{{"changed", true}, {"object_id", object_id},
                    {"volume_id", new_volume_id}, {"type", ModelVolume::type_to_string(volume->type())}};
    });
}

json delete_object(const json& args)
{
    const int object_id = required_int(args, "object_id");
    return on_gui_thread([object_id]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        if (object_id < 0 || static_cast<size_t>(object_id) >= plater->model().objects.size())
            throw std::runtime_error("Object was not found");
        if (plater->model().objects[object_id]->is_cut())
            throw std::runtime_error("Refusing zero-click deletion of a cut object because QIDI requires confirmation");
        if (!plater->delete_object_from_model(static_cast<size_t>(object_id), true))
            throw std::runtime_error("QIDI Studio did not delete the object");
        return json{{"deleted", true}, {"object_id", object_id}};
    });
}

json start_ui_job(bool orient)
{
    return on_gui_thread([orient]() {
        Plater* plater = require_plater();
        require_plate(plater);
        if (plater->model().objects.empty())
            throw std::runtime_error("The project has no objects");
        if (!plater->can_do_ui_job())
            throw std::runtime_error("QIDI Studio is busy and cannot start this job");
        // Auto-orient matches QIDI Studio's global toolbar action. Arrange is
        // intentionally scoped to the active plate: PREPARE_STATE_MENU makes
        // ArrangeJob::prepare_partplate() honor a plate-level print-sequence
        // override instead of prepare_all() locking that plate as incompatible.
        plater->set_prepare_state(orient ? Job::PREPARE_STATE_DEFAULT : Job::PREPARE_STATE_MENU);
        if (orient)
            plater->orient();
        else
            plater->arrange();
        return json{{"accepted", true}, {"job", orient ? "auto_orient" : "arrange"}};
    });
}

json slice_plate()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (!plate->can_slice())
            throw std::runtime_error("The active plate cannot be sliced");
        if (plater->is_any_job_running())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        if (plater->is_background_process_slicing())
            throw std::runtime_error("A slice is already running");
        plater->reslice();
        return json{{"accepted", true}, {"plate_index", plate->get_index()}};
    });
}

json get_slice_status()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        const bool background_slicing = plater->is_background_process_slicing();
        const bool job_running = plater->is_any_job_running();
        const bool studio_busy = background_slicing || job_running;
        GCodeProcessorResult* result = plate->get_slice_result();
        return json{
            {"plate_index", plate->get_index()},
            {"slicing", background_slicing},
            {"background_slicing", background_slicing},
            {"job_running", job_running},
            {"studio_busy", studio_busy},
            {"retry_after_ms", studio_busy ? 1000 : 0},
            {"progress_percent", plate->get_slicing_percent()},
            {"valid", plate->is_slice_result_valid()},
            {"ready_for_print", plate->is_slice_result_ready_for_print()},
            {"can_slice", plate->can_slice()},
            {"gcode_file", result != nullptr ? result->filename : ""}
        };
    });
}

json get_slice_result()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (!plate->is_slice_result_valid())
            throw std::runtime_error("The active plate does not have a valid slice result");
        GCodeProcessorResult* result = plate->get_slice_result();
        if (result == nullptr)
            throw std::runtime_error("Slice result is not available");

        json estimated;
        json warnings = json::array();
        std::string filename;
        {
            std::lock_guard<std::mutex> lock(result->result_mutex);
            const auto& stats = result->print_statistics;
            const auto& normal = stats.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
            const auto& stealth = stats.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)];
            estimated = {
                {"normal_time_s", normal.time},
                {"normal_prepare_time_s", normal.prepare_time},
                {"stealth_time_s", stealth.time},
                {"stealth_prepare_time_s", stealth.prepare_time},
                {"initial_layer_time_s", result->initial_layer_time},
                {"layer_times_s", normal.layers_times},
                {"model_volume_mm3_by_extruder", map_to_json(stats.model_volumes_per_extruder)},
                {"support_volume_mm3_by_extruder", map_to_json(stats.support_volumes_per_extruder)},
                {"wipe_tower_volume_mm3_by_extruder", map_to_json(stats.wipe_tower_volumes_per_extruder)},
                {"total_volume_mm3_by_extruder", map_to_json(stats.total_volumes_per_extruder)},
                {"total_filament_changes", stats.total_filament_changes},
                {"total_flush_filament_changes", stats.total_flush_filament_changes}
            };
            for (const auto& warning : result->warnings)
                warnings.push_back({{"level", warning.level}, {"message", warning.msg},
                                    {"error_code", warning.error_code}, {"params", warning.params}});
            filename = result->filename;
        }

        const PrintStatistics& stats = plater->get_partplate_list().get_current_fff_print().print_statistics();
        return json{
            {"plate_index", plate->get_index()},
            {"gcode_file", filename},
            {"estimated", std::move(estimated)},
            {"totals", {
                {"estimated_normal_print_time", stats.estimated_normal_print_time},
                {"estimated_silent_print_time", stats.estimated_silent_print_time},
                {"used_filament_mm", stats.total_used_filament},
                {"extruded_volume_mm3", stats.total_extruded_volume},
                {"weight_g", stats.total_weight},
                {"cost", stats.total_cost},
                {"toolchanges", stats.total_toolchanges},
                {"wipe_tower_filament_mm", stats.total_wipe_tower_filament},
                {"wipe_tower_cost", stats.total_wipe_tower_cost}
            }},
            {"warnings", std::move(warnings)}
        };
    });
}

json export_object_stl(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    const std::string path = required_string(args, "path");
    return on_gui_thread([object_id, instance_id, path]() {
        Plater* plater = require_plater();
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        ModelObject* object = require_model_object(plater, object_id);
        TriangleMesh mesh = object->raw_mesh();
        if (mesh.facets_count() == 0)
            throw std::runtime_error("The selected object has no printable solid mesh to export");
        instance->transform_mesh(&mesh);
        if (!mesh.write_binary(path.c_str()))
            throw std::runtime_error("QIDI Studio could not write the STL to the requested path");
        return json{{"exported", true}, {"object_id", object_id}, {"instance_id", instance_id},
                    {"path", path}, {"facet_count", mesh.facets_count()},
                    {"bounding_box", bounding_box_to_json(mesh.bounding_box())}};
    });
}

json export_gcode(const json& args)
{
    const std::string path = required_string(args, "path");
    return on_gui_thread([path]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (!plate->is_slice_result_valid())
            throw std::runtime_error("Slice the active plate before exporting G-code");
        if (plater->is_export_gcode_scheduled())
            throw std::runtime_error("Another G-code export is already scheduled");
        plater->export_gcode(boost::filesystem::path(path));
        return json{{"accepted", true}, {"path", path}};
    });
}

json get_suite_capabilities()
{
    return {
        {"suite_version", "1.11.0"},
        {"qidi_target", "2.7.2.10"},
        {"capability_tiers", {
            {"native", json::array({
                "project_and_plate_management", "indexed_project_filament_replacement", "object_and_volume_transforms", "volume_scope_settings", "native_mesh_repair",
                "split_to_objects", "merge_selected_volumes", "support_blocker_and_enforcer_volumes",
                "preset_read_write_clone", "slice_and_gcode_export", "printer_status_and_control",
                "printer_camera_capture_with_light", "studio_window_capture",
                "modal_ui_state", "native_print_by_object_validation_and_order",
                "confirmation_gated_local_print_start", "tunnel_health_reporting",
                "printer_monitoring_snapshot", "printer_case_light_control",
                "adaptive_layer_height_profiles", "geometric_surface_selection",
                "painted_support_facets", "painted_seam_facets",
                "native_setting_definitions", "non_mutating_settings_update_preview",
                "chatgpt_attachment_model_import"
            })},
            {"computed", json::array({
                "mesh_diagnostics", "overhang_and_contact_estimates", "orientation_candidates",
                "build_volume_fit", "bounding_box_fit_and_alignment", "toolpath_and_first_layer_analysis",
                "configuration_validation", "print_preflight", "filament_quantity_estimate"
            })},
            {"agent_orchestrated", json::array({
                "comparative_orientation_slicing", "process_variant_comparison",
                "filament_recommendation", "guided_calibration", "prepare_for_print_workflow",
                "end_to_end_conversational_print_pipeline"
            })},
            {"not_safely_exposed", json::array({
                "freehand_painter_brush_stroke_replay", "surface_distance_clearance_map",
                "automatic_anatomical_mesh_deformation", "cloud_only_print_start",
                "closed_loop_visual_failure_detection", "embedded_tunnel_credentials"
            })}
        }},
        {"safety", {
            {"undo_snapshots_for_model_mutations", true},
            {"explicit_confirmation_for_geometry_replacement", true},
            {"explicit_confirmation_for_cancel_or_delete", true},
            {"start_print_requires_confirmation_token", true},
            {"print_confirmation_tokens_single_use", true},
            {"direct_print_requires_explicit_physical_filament_source", true},
            {"direct_print_verifies_slice_filament_matches_source", true},
            {"direct_print_locks_qidi_box_slot_telemetry", true},
            {"direct_print_confirms_reported_physical_slot", true},
            {"camera_light_enabled_before_capture", true},
            {"monitoring_never_controls_print", true},
            {"surface_selection_previews_never_mutate", true},
            {"settings_update_previews_never_mutate", true},
            {"surface_paint_mutations_are_undoable", true},
            {"attachment_download_https_only", true},
            {"attachment_downloads_use_no_qidi_auth_headers", true},
            {"attachment_files_are_ephemeral", true},
            {"attachment_file_limit_mib", 256},
            {"attachment_total_limit_mib", 512},
            {"tunnel_credentials_never_returned", true},
            {"gui_call_timeout_seconds", 30}
        }}
    };
}

json get_machine_capabilities()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        const Preset& printer = bundle->printers.get_selected_preset();
        const BuildVolume& volume = plater->build_volume();
        json printable_area = json::array();
        for (const Vec2d& point : volume.printable_area())
            printable_area.push_back({{"x", point.x()}, {"y", point.y()}});
        static const std::vector<std::string> keys{
            "printer_model", "printer_variant", "printer_structure", "printer_technology",
            "nozzle_diameter", "nozzle_type", "nozzle_hrc", "min_layer_height", "max_layer_height",
            "printable_height", "extruder_clearance_radius", "extruder_clearance_height_to_rod",
            "extruder_clearance_height_to_lid", "machine_max_acceleration_extruding",
            "machine_max_acceleration_travel", "machine_max_speed_x", "machine_max_speed_y",
            "machine_max_speed_z", "machine_max_speed_e", "chamber_temperature",
            "default_filament_profile", "default_print_profile"
        };
        return json{
            {"preset", printer.name}, {"model", first_string_option(printer.config, "printer_model")},
            {"variant", first_string_option(printer.config, "printer_variant")},
            {"build_volume", {
                {"type", std::string(volume.type_name())}, {"valid", volume.valid()},
                {"printable_height_mm", volume.printable_height()},
                {"bounding_box", bounding_box_to_json(volume.bounding_volume())},
                {"printable_area", std::move(printable_area)},
                {"extruder_area_count", volume.get_extruder_area_count()}
            }},
            {"settings", config_snapshot(printer.config, keys)}
        };
    });
}

json get_nozzle_capabilities(QDSDeviceManager* manager, const json& args)
{
    const std::string device_id = args.value("device_id", "");
    json device = nullptr;
    if (!device_id.empty()) {
        if (manager == nullptr)
            return {{"error", "QIDI device manager is not available"}};
        std::shared_ptr<QDSDevice> selected = manager->getDevice(device_id);
        if (!selected)
            return {{"error", "Printer was not found"}};
        device = {{"device_id", selected->m_id}, {"name", selected->m_name},
                  {"online", selected->is_online()}, {"reported_diameters_mm", selected->m_nozzle_diameter}};
    }
    json preset = on_gui_thread([]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        const Preset& printer = bundle->printers.get_selected_preset();
        static const std::vector<std::string> keys{
            "nozzle_diameter", "nozzle_type", "nozzle_hrc", "min_layer_height", "max_layer_height",
            "default_nozzle_volume_type", "can_switch_nozzle_type", "extruder_type"
        };
        return json{{"preset", printer.name},
                    {"diameter_mm", first_numeric_option(printer.config, "nozzle_diameter")},
                    {"settings", config_snapshot(printer.config, keys)}};
    });
    preset["physical_printer"] = std::move(device);
    preset["physical_match_known"] = !device_id.empty();
    return preset;
}

json list_project_filaments()
{
    return on_gui_thread([]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        json filaments = json::array();
        for (size_t index = 0; index < bundle->filament_presets.size(); ++index) {
            const std::string& name = bundle->filament_presets[index];
            const Preset* preset = bundle->filaments.find_preset(name, false);
            if (preset != nullptr)
                filaments.push_back(filament_profile_to_json(*preset, index));
            else
                filaments.push_back({{"filament_index", index}, {"preset", name},
                                     {"error", "Assigned preset was not found"}});
        }
        return json{{"count", filaments.size()}, {"filaments", std::move(filaments)}};
    });
}

json get_filament_capabilities(const json& args)
{
    const int index = args.value("filament_index", 0);
    if (index < 0)
        return {{"error", "filament_index must be non-negative"}};
    return on_gui_thread([index]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        if (static_cast<size_t>(index) >= bundle->filament_presets.size())
            throw std::runtime_error("filament_index is out of range");
        const std::string& name = bundle->filament_presets[static_cast<size_t>(index)];
        const Preset* preset = bundle->filaments.find_preset(name, false);
        if (preset == nullptr)
            throw std::runtime_error("Assigned filament preset was not found");
        return json{{"filament", filament_profile_to_json(*preset, static_cast<size_t>(index))}};
    });
}

json compare_filament_profiles(const json& args)
{
    const json requested = args.value("filament_indices", json::array());
    if (!requested.is_array())
        return {{"error", "filament_indices must be an array"}};
    return on_gui_thread([requested]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        std::vector<size_t> indices;
        if (requested.empty()) {
            for (size_t i = 0; i < bundle->filament_presets.size(); ++i) indices.push_back(i);
        } else {
            for (const json& value : requested) {
                if (!value.is_number_integer() || value.get<int>() < 0)
                    throw std::runtime_error("Every filament index must be a non-negative integer");
                indices.push_back(static_cast<size_t>(value.get<int>()));
            }
        }
        json filaments = json::array();
        for (size_t index : indices) {
            if (index >= bundle->filament_presets.size())
                throw std::runtime_error("A filament index is out of range");
            const Preset* preset = bundle->filaments.find_preset(bundle->filament_presets[index], false);
            if (preset == nullptr)
                throw std::runtime_error("An assigned filament preset was not found");
            filaments.push_back(filament_profile_to_json(*preset, index));
        }
        return json{{"filaments", std::move(filaments)},
                    {"ranking_performed", false},
                    {"note", "Engineering ranking is intentionally left to the agent and requested project priorities."}};
    });
}

json validate_active_configuration()
{
    return on_gui_thread([]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        DynamicPrintConfig full = bundle->full_config();
        const auto errors = full.validate(false);
        return json{{"valid", errors.empty()}, {"error_count", errors.size()},
                    {"errors", validation_errors_to_json(errors)},
                    {"active", {{"printer", bundle->printers.get_selected_preset_name()},
                                {"print", bundle->prints.get_selected_preset_name()},
                                {"filaments", bundle->filament_presets}}}};
    });
}

json preview_profile_changes(const json& args)
{
    const std::string scope = args.value("scope", "print");
    const std::string base_name = args.value("base_name", "");
    if (!args.contains("values") || !args["values"].is_object() || args["values"].empty())
        return {{"error", "values must be a non-empty object of serialized setting strings"}};
    const json values = args["values"];
    return on_gui_thread([scope, base_name, values]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        PresetCollection& collection = preset_collection(*bundle, preset_type(scope));
        const Preset* base = base_name.empty() ? &collection.get_selected_preset()
                                                : collection.find_preset(base_name, false);
        if (base == nullptr)
            throw std::runtime_error("Base preset was not found");
        DynamicPrintConfig updated(base->config);
        json normalized = json::object();
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.value().is_string())
                throw std::runtime_error("Setting values must use QIDI's serialized string format");
            if (updated.option(it.key()) == nullptr)
                throw std::runtime_error("Unknown setting for " + scope + ": " + it.key());
            repair_config_option_enum_map(it.key(), updated.option(it.key()));
            updated.set_deserialize_strict(it.key(), it.value().get<std::string>());
            normalized[it.key()] = serialize_config_option(
                it.key(), updated.option(it.key()));
        }
        return json{{"scope", scope}, {"base_preset", base->name},
                    {"normalized_values", std::move(normalized)}, {"mutated", false}};
    });
}

json create_profile_variant(const json& args)
{
    const std::string scope = args.value("scope", "print");
    const std::string name = required_string(args, "name");
    const std::string base_name = args.value("base_name", "");
    const bool overwrite = args.value("overwrite", false);
    const bool save_to_project = args.value("save_to_project", false);
    const json values = args.value("values", json::object());
    if (!values.is_object())
        return {{"error", "values must be an object"}};
    return on_gui_thread([scope, name, base_name, overwrite, save_to_project, values]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        Tab* tab = require_tab(preset_type(scope));
        PresetCollection* collection = tab->get_presets();
        if (collection == nullptr)
            throw std::runtime_error("Preset collection is not available");
        if (tab->current_preset_is_dirty())
            throw std::runtime_error("Save or discard the current modified preset before creating a variant");
        if (!base_name.empty() && !tab->select_preset(base_name, false, "", true, true))
            throw std::runtime_error("Base preset was not found or could not be selected");
        Preset* existing = collection->find_preset(name, false);
        if (existing != nullptr && !overwrite)
            throw std::runtime_error("Preset already exists; pass overwrite=true to replace a user preset");
        if (existing != nullptr && (existing->is_system || existing->is_default))
            throw std::runtime_error("Refusing to overwrite a system or default preset");
        DynamicPrintConfig updated(*tab->get_config());
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.value().is_string())
                throw std::runtime_error("Setting values must use QIDI's serialized string format");
            if (updated.option(it.key()) == nullptr)
                throw std::runtime_error("Unknown setting for " + scope + ": " + it.key());
            repair_config_option_enum_map(it.key(), updated.option(it.key()));
            updated.set_deserialize_strict(it.key(), it.value().get<std::string>());
        }
        tab->load_config(updated);
        tab->save_preset(name, false, save_to_project);
        Preset* saved = collection->find_preset(name, false, true);
        if (saved == nullptr)
            throw std::runtime_error("QIDI Studio did not create the requested preset");
        return json{{"created", true}, {"scope", scope}, {"name", saved->name},
                    {"base_preset", base_name.empty() ? json(nullptr) : json(base_name)},
                    {"project_embedded", saved->is_project_embedded},
                    {"applied_values", values}};
    });
}

json measure_model(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    return on_gui_thread([object_id, instance_id]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        TriangleMesh mesh = transformed_instance_mesh(object, instance);
        const TriangleMeshStats& stats = mesh.stats();
        return json{{"object_id", object_id}, {"instance_id", instance_id}, {"name", object->name},
                    {"bounding_box", bounding_box_to_json(mesh.bounding_box())},
                    {"volume_mm3", std::abs(static_cast<double>(stats.volume))},
                    {"facet_count", mesh.facets_count()}, {"part_count", stats.number_of_parts},
                    {"average_edge_length_mm", its_average_edge_length(mesh.its)}};
    });
}

json compare_model_to_build_volume(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    return on_gui_thread([object_id, instance_id]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        const BuildVolume& build = plater->build_volume();
        const BoundingBoxf3 object_box = object->instance_bounding_box(static_cast<size_t>(instance_id));
        const BoundingBoxf3 build_box = build.bounding_volume();
        const BuildVolume::ObjectState state = build.object_state(
            object->raw_indexed_triangle_set(), instance->get_matrix().cast<float>(), true, true);
        const Vec3d remaining = build_box.size() - object_box.size();
        return json{{"object_id", object_id}, {"instance_id", instance_id},
                    {"state", build_volume_state_name(state)},
                    {"fits", state == BuildVolume::ObjectState::Inside},
                    {"object_bounding_box", bounding_box_to_json(object_box)},
                    {"build_bounding_box", bounding_box_to_json(build_box)},
                    {"remaining_span_mm", {{"x", remaining.x()}, {"y", remaining.y()}, {"z", remaining.z()}}},
                    {"exact_polygon_test", true}};
    });
}

json analyze_overhangs(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    const double angle = args.value("angle_deg", 45.0);
    const size_t max_facets = std::min(args.value("max_sample_facets", size_t{250000}), size_t{2000000});
    if (angle < 0.0 || angle > 90.0)
        return {{"error", "angle_deg must be between 0 and 90"}};
    return on_gui_thread([object_id, instance_id, angle, max_facets]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        TriangleMesh mesh = transformed_instance_mesh(object, instance);
        const MeshGeometryMetrics metrics = calculate_mesh_metrics(mesh, angle, 0.05, max_facets);
        return json{{"object_id", object_id}, {"instance_id", instance_id},
                    {"analysis", mesh_metrics_to_json(metrics, angle, 0.05)},
                    {"interpretation", "Smaller angle-from-downward-vertical values are more horizontal undersides and generally more support-sensitive."}};
    });
}

json analyze_bed_contact(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    const double tolerance = args.value("tolerance_mm", 0.10);
    const size_t max_facets = std::min(args.value("max_sample_facets", size_t{500000}), size_t{2000000});
    if (tolerance <= 0.0 || tolerance > 5.0)
        return {{"error", "tolerance_mm must be greater than 0 and no more than 5"}};
    return on_gui_thread([object_id, instance_id, tolerance, max_facets]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        TriangleMesh mesh = transformed_instance_mesh(object, instance);
        const MeshGeometryMetrics metrics = calculate_mesh_metrics(mesh, 45.0, tolerance, max_facets);
        const BoundingBoxf3 box = mesh.bounding_box();
        const double footprint = box.size().x() * box.size().y();
        return json{{"object_id", object_id}, {"instance_id", instance_id},
                    {"minimum_z_mm", box.min.z()}, {"contact_area_mm2", metrics.bed_contact_area_mm2},
                    {"bounding_footprint_mm2", footprint},
                    {"contact_to_footprint_ratio", footprint > 0.0 ? metrics.bed_contact_area_mm2 / footprint : 0.0},
                    {"sampling_stride", metrics.stride}, {"approximate", metrics.stride > 1},
                    {"tolerance_mm", tolerance}};
    });
}

json analyze_printability(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    const double angle = args.value("overhang_angle_deg", 45.0);
    return on_gui_thread([object_id, instance_id, angle]() {
        Plater* plater = require_plater();
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        ModelObject* object = require_model_object(plater, object_id);
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        TriangleMesh mesh = transformed_instance_mesh(object, instance);
        const TriangleMeshStats stats = object->get_object_stl_stats();
        const MeshGeometryMetrics geometry = calculate_mesh_metrics(mesh, angle, 0.10, 250000);
        const BuildVolume::ObjectState state = plater->build_volume().object_state(
            object->raw_indexed_triangle_set(), instance->get_matrix().cast<float>(), true, true);
        DynamicPrintConfig full = bundle->full_config();
        const auto config_errors = full.validate(false);
        json risks = json::array();
        if (!stats.manifold()) risks.push_back("non_manifold_mesh");
        if (stats.has_open_edges()) risks.push_back("open_edges");
        if (state != BuildVolume::ObjectState::Inside) risks.push_back("outside_or_colliding_with_build_volume");
        if (!config_errors.empty()) risks.push_back("invalid_active_configuration");
        if (geometry.bed_contact_area_mm2 <= 1.0) risks.push_back("very_small_bed_contact");
        if (geometry.severe_overhang_area_mm2 > 0.0) risks.push_back("support_sensitive_downward_surfaces");
        return json{{"object_id", object_id}, {"instance_id", instance_id},
                    {"ready_for_trial_slice", risks.empty()}, {"risk_count", risks.size()},
                    {"risks", std::move(risks)}, {"mesh", mesh_stats_to_json(stats, object->facets_count())},
                    {"build_volume_state", build_volume_state_name(state)},
                    {"geometry", mesh_metrics_to_json(geometry, angle, 0.10)},
                    {"configuration_errors", validation_errors_to_json(config_errors)}};
    });
}

json generate_orientation_candidates(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    const std::string priority = args.value("priority", "balanced");
    if (priority != "balanced" && priority != "speed" && priority != "support" && priority != "adhesion")
        return {{"error", "priority must be balanced, speed, support, or adhesion"}};
    return on_gui_thread([object_id, instance_id, priority]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        const Vec3d scale = instance->get_scaling_factor();
        const Vec3d mirror = instance->get_mirror();
        const Vec3d build_size = plater->build_volume().bounding_volume().size();
        const TriangleMesh mesh = object->raw_mesh();
        json candidates = json::array();
        for (const OrientationSpec& spec : orientation_specs()) {
            Eigen::Matrix3d rotation = Eigen::AngleAxisd(spec.rotation.x(), Vec3d::UnitX()).toRotationMatrix();
            rotation = Eigen::AngleAxisd(spec.rotation.y(), Vec3d::UnitY()).toRotationMatrix() * rotation;
            rotation = Eigen::AngleAxisd(spec.rotation.z(), Vec3d::UnitZ()).toRotationMatrix() * rotation;
            Transform3d transform = Transform3d::Identity();
            transform.linear() = rotation * scale.cwiseProduct(mirror).asDiagonal();
            const MeshGeometryMetrics metrics = calculate_transformed_mesh_metrics(
                mesh, transform, 45.0, 0.10, 200000);
            const Vec3d size = mesh.transformed_bounding_box(transform).size();
            const bool fits = size.x() <= build_size.x() && size.y() <= build_size.y() && size.z() <= build_size.z();
            double score = 0.0;
            if (priority == "speed")
                score = size.z() + metrics.severe_overhang_area_mm2 * 0.002 - metrics.bed_contact_area_mm2 * 0.0005;
            else if (priority == "support")
                score = metrics.severe_overhang_area_mm2 + size.z() * 0.1 - metrics.bed_contact_area_mm2 * 0.01;
            else if (priority == "adhesion")
                score = size.z() * 0.2 + metrics.severe_overhang_area_mm2 * 0.05 - metrics.bed_contact_area_mm2;
            else
                score = size.z() + metrics.severe_overhang_area_mm2 * 0.05 - metrics.bed_contact_area_mm2 * 0.05;
            if (!fits) score += 1.0e9;
            candidates.push_back({{"candidate_id", spec.id},
                {"rotation_rad", {{"x", spec.rotation.x()}, {"y", spec.rotation.y()}, {"z", spec.rotation.z()}}},
                {"size_mm", {{"x", size.x()}, {"y", size.y()}, {"z", size.z()}}},
                {"fits_build_bounds", fits}, {"estimated_score", score},
                {"geometry", mesh_metrics_to_json(metrics, 45.0, 0.10)}});
        }
        std::sort(candidates.begin(), candidates.end(), [](const json& a, const json& b) {
            return a["estimated_score"].get<double>() < b["estimated_score"].get<double>();
        });
        for (size_t rank = 0; rank < candidates.size(); ++rank) candidates[rank]["rank"] = rank + 1;
        return json{{"object_id", object_id}, {"instance_id", instance_id}, {"priority", priority},
                    {"candidate_count", candidates.size()}, {"candidates", std::move(candidates)},
                    {"requires_comparative_slicing_for_final_choice", true}};
    });
}

json apply_orientation_candidate(const json& args)
{
    const int object_id = required_int(args, "object_id");
    const int instance_id = args.value("instance_id", 0);
    const std::string candidate_id = required_string(args, "candidate_id");
    const OrientationSpec spec = require_orientation_spec(candidate_id);
    return on_gui_thread([object_id, instance_id, candidate_id, spec]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ModelInstance* instance = require_instance(require_plate(plater), object_id, instance_id);
        plater->take_snapshot("MCP Apply Orientation Candidate");
        instance->set_rotation(spec.rotation);
        object->ensure_on_bed(false);
        plater->get_partplate_list().notify_instance_update(object_id, instance_id);
        plater->changed_object(object_id);
        return json{{"applied", true}, {"object_id", object_id}, {"instance_id", instance_id},
                    {"candidate_id", candidate_id},
                    {"rotation_rad", {{"x", spec.rotation.x()}, {"y", spec.rotation.y()}, {"z", spec.rotation.z()}}},
                    {"bounding_box", bounding_box_to_json(object->instance_bounding_box(static_cast<size_t>(instance_id)))},
                    {"undo_available", plater->can_undo()}};
    });
}

json analyze_object_relationship(const json& args)
{
    const int first_id = required_int(args, "first_object_id");
    const int second_id = required_int(args, "second_object_id");
    const int first_instance = args.value("first_instance_id", 0);
    const int second_instance = args.value("second_instance_id", 0);
    return on_gui_thread([first_id, second_id, first_instance, second_instance]() {
        Plater* plater = require_plater();
        ModelObject* first = require_model_object(plater, first_id);
        ModelObject* second = require_model_object(plater, second_id);
        require_instance(require_plate(plater), first_id, first_instance);
        require_instance(require_plate(plater), second_id, second_instance);
        const BoundingBoxf3 first_box = first->instance_bounding_box(static_cast<size_t>(first_instance));
        const BoundingBoxf3 second_box = second->instance_bounding_box(static_cast<size_t>(second_instance));
        return json{{"first", {{"object_id", first_id}, {"instance_id", first_instance},
                                {"name", first->name}, {"bounding_box", bounding_box_to_json(first_box)}}},
                    {"second", {{"object_id", second_id}, {"instance_id", second_instance},
                                 {"name", second->name}, {"bounding_box", bounding_box_to_json(second_box)}}},
                    {"relationship", bounding_box_relationship(first_box, second_box)},
                    {"surface_clearance_computed", false}};
    });
}

json calculate_fit_scaling(const json& args)
{
    const int subject_id = required_int(args, "subject_object_id");
    const int container_id = required_int(args, "container_object_id");
    const int subject_instance = args.value("subject_instance_id", 0);
    const int container_instance = args.value("container_instance_id", 0);
    const double clearance = args.value("clearance_mm", 0.0);
    const bool uniform = args.value("uniform", false);
    return on_gui_thread([subject_id, container_id, subject_instance, container_instance, clearance, uniform]() {
        Plater* plater = require_plater();
        ModelObject* subject = require_model_object(plater, subject_id);
        ModelObject* container = require_model_object(plater, container_id);
        require_instance(require_plate(plater), subject_id, subject_instance);
        require_instance(require_plate(plater), container_id, container_instance);
        const Vec3d subject_size = subject->instance_bounding_box(static_cast<size_t>(subject_instance)).size();
        const Vec3d available = container->instance_bounding_box(static_cast<size_t>(container_instance)).size() -
                                Vec3d::Constant(2.0 * clearance);
        if ((subject_size.array() <= 0.0).any() || (available.array() <= 0.0).any())
            throw std::runtime_error("Objects or requested clearance produce a non-positive fit envelope");
        Vec3d factors = available.cwiseQuotient(subject_size);
        if (uniform) factors = Vec3d::Constant(factors.minCoeff());
        return json{{"subject_object_id", subject_id}, {"container_object_id", container_id},
                    {"clearance_mm", clearance}, {"uniform", uniform},
                    {"scale_multiplier", {{"x", factors.x()}, {"y", factors.y()}, {"z", factors.z()}}},
                    {"method", "outer_axis_aligned_bounding_boxes"},
                    {"suitable_for_anatomical_final_fit", false}};
    });
}

json align_objects(const json& args)
{
    const int moving_id = required_int(args, "moving_object_id");
    const int reference_id = required_int(args, "reference_object_id");
    const int moving_instance = args.value("moving_instance_id", 0);
    const int reference_instance = args.value("reference_instance_id", 0);
    const std::string anchor = args.value("anchor", "center");
    if (anchor != "center" && anchor != "min" && anchor != "max")
        return {{"error", "anchor must be center, min, or max"}};
    const json axes = args.value("axes", json::array({"x", "y", "z"}));
    if (!axes.is_array()) return {{"error", "axes must be an array"}};
    return on_gui_thread([moving_id, reference_id, moving_instance, reference_instance, anchor, axes]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* moving = require_model_object(plater, moving_id);
        ModelObject* reference = require_model_object(plater, reference_id);
        ModelInstance* moving_inst = require_instance(require_plate(plater), moving_id, moving_instance);
        require_instance(require_plate(plater), reference_id, reference_instance);
        const BoundingBoxf3 moving_box = moving->instance_bounding_box(static_cast<size_t>(moving_instance));
        const BoundingBoxf3 reference_box = reference->instance_bounding_box(static_cast<size_t>(reference_instance));
        Vec3d delta = Vec3d::Zero();
        std::set<std::string> selected;
        for (const json& axis : axes) {
            if (!axis.is_string()) throw std::runtime_error("Every axis must be a string");
            selected.insert(axis.get<std::string>());
        }
        const auto coordinate = [anchor](const BoundingBoxf3& box, int axis) {
            return anchor == "min" ? box.min[axis] : anchor == "max" ? box.max[axis] : box.center()[axis];
        };
        for (int axis = 0; axis < 3; ++axis) {
            const char* name = axis == 0 ? "x" : axis == 1 ? "y" : "z";
            if (selected.count(name)) delta[axis] = coordinate(reference_box, axis) - coordinate(moving_box, axis);
        }
        plater->take_snapshot("MCP Align Objects");
        moving_inst->set_offset(moving_inst->get_offset() + delta);
        moving->invalidate_bounding_box();
        plater->get_partplate_list().notify_instance_update(moving_id, moving_instance);
        plater->update();
        return json{{"aligned", true}, {"moving_object_id", moving_id}, {"reference_object_id", reference_id},
                    {"anchor", anchor}, {"translation_mm", {{"x", delta.x()}, {"y", delta.y()}, {"z", delta.z()}}},
                    {"bounding_box", bounding_box_to_json(moving->instance_bounding_box(static_cast<size_t>(moving_instance)))},
                    {"undo_available", plater->can_undo()}};
    });
}

json scale_object_to_fit(const json& args)
{
    const int subject_id = required_int(args, "subject_object_id");
    const int container_id = required_int(args, "container_object_id");
    const int subject_instance = args.value("subject_instance_id", 0);
    const int container_instance = args.value("container_instance_id", 0);
    const double clearance = args.value("clearance_mm", 0.0);
    const bool uniform = args.value("uniform", false);
    return on_gui_thread([subject_id, container_id, subject_instance, container_instance, clearance, uniform]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* subject = require_model_object(plater, subject_id);
        ModelObject* container = require_model_object(plater, container_id);
        ModelInstance* instance = require_instance(require_plate(plater), subject_id, subject_instance);
        require_instance(require_plate(plater), container_id, container_instance);
        const Vec3d subject_size = subject->instance_bounding_box(static_cast<size_t>(subject_instance)).size();
        const Vec3d available = container->instance_bounding_box(static_cast<size_t>(container_instance)).size() -
                                Vec3d::Constant(2.0 * clearance);
        if ((subject_size.array() <= 0.0).any() || (available.array() <= 0.0).any())
            throw std::runtime_error("Objects or requested clearance produce a non-positive fit envelope");
        Vec3d multiplier = available.cwiseQuotient(subject_size);
        if (uniform) multiplier = Vec3d::Constant(multiplier.minCoeff());
        plater->take_snapshot("MCP Scale Object To Fit");
        instance->set_scaling_factor(instance->get_scaling_factor().cwiseProduct(multiplier));
        subject->invalidate_bounding_box();
        plater->get_partplate_list().notify_instance_update(subject_id, subject_instance);
        plater->changed_object(subject_id);
        return json{{"scaled", true}, {"subject_object_id", subject_id}, {"container_object_id", container_id},
                    {"scale_multiplier", {{"x", multiplier.x()}, {"y", multiplier.y()}, {"z", multiplier.z()}}},
                    {"result_scale", {{"x", instance->get_scaling_factor().x()},
                                      {"y", instance->get_scaling_factor().y()},
                                      {"z", instance->get_scaling_factor().z()}}},
                    {"method", "outer_axis_aligned_bounding_boxes"},
                    {"suitable_for_anatomical_final_fit", false}, {"undo_available", plater->can_undo()}};
    });
}

json preview_mesh_repair(const json& args)
{
    const int object_id = required_int(args, "object_id");
    return on_gui_thread([object_id]() {
        Plater* plater = require_plater();
        ModelObject* object = require_model_object(plater, object_id);
        const TriangleMeshStats stats = object->get_object_stl_stats();
        return json{{"object_id", object_id}, {"name", object->name},
                    {"current", mesh_stats_to_json(stats, object->facets_count())},
                    {"repair_available", true}, {"platform", "windows_native"},
                    {"possible_side_effects", json::array({"geometry_changes", "painted_facets_may_be_removed"})},
                    {"mutated", false}};
    });
}

json split_object_to_parts(const json& args)
{
    const int object_id = required_int(args, "object_id");
    if (!args.value("confirm", false))
        return {{"error", "Splitting replaces the source object; pass confirm=true to continue"}};
    return on_gui_thread([object_id]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        ObjectList* object_list = wxGetApp().obj_list();
        if (object_list == nullptr)
            throw std::runtime_error("QIDI object list is not available");
        ObjectVolumeID selection; selection.object = object;
        object_list->select_item(selection);
        const size_t before = plater->model().objects.size();
        plater->split_object();
        json objects = json::array();
        for (size_t i = 0; i < plater->model().objects.size(); ++i)
            if (plater->model().objects[i] != nullptr)
                objects.push_back(object_state_to_json(plater->model().objects[i], static_cast<int>(i)));
        return json{{"split_requested", true}, {"source_object_id", object_id},
                    {"object_count_before", before}, {"object_count_after", plater->model().objects.size()},
                    {"objects", std::move(objects)}, {"undo_available", plater->can_undo()}};
    });
}

json merge_object_volumes(const json& args)
{
    const int object_id = required_int(args, "object_id");
    if (!args.contains("volume_ids") || !args["volume_ids"].is_array() || args["volume_ids"].size() < 2)
        return {{"error", "volume_ids must contain at least two volume indices"}};
    if (!args.value("confirm", false))
        return {{"error", "Merging replaces selected source volumes; pass confirm=true to continue"}};
    const json requested = args["volume_ids"];
    return on_gui_thread([object_id, requested]() {
        Plater* plater = require_plater();
        if (plater->is_any_job_running() || plater->is_background_process_slicing())
            throw std::runtime_error("Wait for the current QIDI Studio job to finish");
        ModelObject* object = require_model_object(plater, object_id);
        std::vector<int> ids;
        for (const json& value : requested) {
            if (!value.is_number_integer() || value.get<int>() < 0 ||
                static_cast<size_t>(value.get<int>()) >= object->volumes.size())
                throw std::runtime_error("A volume_id is invalid or out of range");
            ids.push_back(value.get<int>());
        }
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end())
            throw std::runtime_error("volume_ids must be unique");
        const size_t before = plater->model().objects.size();
        plater->merge(static_cast<size_t>(object_id), ids);
        json objects = json::array();
        for (size_t i = 0; i < plater->model().objects.size(); ++i)
            if (plater->model().objects[i] != nullptr)
                objects.push_back(object_state_to_json(plater->model().objects[i], static_cast<int>(i)));
        return json{{"merged", true}, {"source_object_id", object_id}, {"volume_ids", ids},
                    {"object_count_before", before}, {"object_count_after", plater->model().objects.size()},
                    {"objects", std::move(objects)}, {"undo_available", plater->can_undo()}};
    });
}

const char* move_type_name(EMoveType type)
{
    switch (type) {
    case EMoveType::Noop: return "noop";
    case EMoveType::Retract: return "retract";
    case EMoveType::Unretract: return "unretract";
    case EMoveType::Seam: return "seam";
    case EMoveType::Tool_change: return "tool_change";
    case EMoveType::Color_change: return "color_change";
    case EMoveType::Pause_Print: return "pause";
    case EMoveType::Custom_GCode: return "custom_gcode";
    case EMoveType::Travel: return "travel";
    case EMoveType::Wipe: return "wipe";
    case EMoveType::Extrude: return "extrude";
    case EMoveType::Count: return "count";
    }
    return "unknown";
}

json inspect_toolpath()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (!plate->is_slice_result_valid())
            throw std::runtime_error("Slice the active plate before inspecting toolpaths");
        GCodeProcessorResult* result = plate->get_slice_result();
        if (result == nullptr)
            throw std::runtime_error("Slice result is not available");
        json counts = json::object();
        json extrusion_by_role = json::object();
        double travel_distance = 0.0;
        double extrusion_path_distance = 0.0;
        double extrusion_volume = 0.0;
        double max_flow = 0.0;
        double max_feedrate = 0.0;
        size_t retractions = 0;
        size_t tool_changes = 0;
        Vec3f min_pos = Vec3f::Constant(std::numeric_limits<float>::max());
        Vec3f max_pos = Vec3f::Constant(std::numeric_limits<float>::lowest());
        bool have_extrusion = false;
        {
            std::lock_guard<std::mutex> lock(result->result_mutex);
            Vec3f previous = Vec3f::Zero();
            bool have_previous = false;
            for (const auto& move : result->moves) {
                const char* name = move_type_name(move.type);
                counts[name] = counts.value(name, size_t{0}) + 1;
                const double distance = have_previous ? (move.position - previous).norm() : 0.0;
                previous = move.position;
                have_previous = true;
                max_feedrate = std::max(max_feedrate, static_cast<double>(move.feedrate));
                if (move.type == EMoveType::Travel) travel_distance += distance;
                if (move.type == EMoveType::Retract) ++retractions;
                if (move.type == EMoveType::Tool_change) ++tool_changes;
                if (move.type == EMoveType::Extrude) {
                    have_extrusion = true;
                    extrusion_path_distance += distance;
                    extrusion_volume += distance * move.mm3_per_mm;
                    max_flow = std::max(max_flow, static_cast<double>(move.volumetric_rate()));
                    min_pos = min_pos.cwiseMin(move.position);
                    max_pos = max_pos.cwiseMax(move.position);
                    const std::string role = std::to_string(static_cast<int>(move.extrusion_role));
                    extrusion_by_role[role] = extrusion_by_role.value(role, 0.0) + distance * move.mm3_per_mm;
                }
            }
            return json{{"plate_index", plate->get_index()}, {"gcode_file", result->filename},
                        {"move_count", result->moves.size()}, {"move_counts", std::move(counts)},
                        {"travel_distance_mm", travel_distance},
                        {"extrusion_path_distance_mm", extrusion_path_distance},
                        {"estimated_extrusion_volume_mm3", extrusion_volume},
                        {"extrusion_volume_mm3_by_role_id", std::move(extrusion_by_role)},
                        {"retraction_count", retractions}, {"tool_change_count", tool_changes},
                        {"max_volumetric_rate_mm3_s", max_flow}, {"max_feedrate_mm_s", max_feedrate},
                        {"toolpath_outside", result->toolpath_outside},
                        {"gcode_check_error_code", result->gcode_check_result.error_code},
                        {"extrusion_bounds", have_extrusion ? json{{"min_mm", {{"x", min_pos.x()}, {"y", min_pos.y()}, {"z", min_pos.z()}}},
                                                                      {"max_mm", {{"x", max_pos.x()}, {"y", max_pos.y()}, {"z", max_pos.z()}}}}
                                                           : json(nullptr)}};
        }
    });
}

json get_layer_summary(const json& args)
{
    const size_t start = args.value("start_layer", size_t{0});
    const size_t limit = std::min(args.value("limit", size_t{100}), size_t{1000});
    return on_gui_thread([start, limit]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (!plate->is_slice_result_valid())
            throw std::runtime_error("Slice the active plate before reading layers");
        GCodeProcessorResult* result = plate->get_slice_result();
        if (result == nullptr)
            throw std::runtime_error("Slice result is not available");
        std::lock_guard<std::mutex> lock(result->result_mutex);
        const auto& times = result->print_statistics.modes[
            static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].layers_times;
        std::set<float> z_set;
        for (const auto& move : result->moves)
            if (move.type == EMoveType::Extrude && move.print_z > 0.0f) z_set.insert(move.print_z);
        std::vector<float> z_values(z_set.begin(), z_set.end());
        const size_t begin = std::min(start, times.size());
        const size_t end = std::min(begin + limit, times.size());
        json layers = json::array();
        double returned_time = 0.0;
        for (size_t layer = begin; layer < end; ++layer) {
            returned_time += times[layer];
            layers.push_back({{"layer", layer},
                              {"z_mm", layer < z_values.size() ? json(z_values[layer]) : json(nullptr)},
                              {"estimated_time_s", times[layer]}});
        }
        return json{{"plate_index", plate->get_index()}, {"layer_count", times.size()},
                    {"start_layer", begin}, {"returned", end - begin},
                    {"returned_time_s", returned_time}, {"layers", std::move(layers)}};
    });
}

json analyze_first_layer()
{
    return on_gui_thread([]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (!plate->is_slice_result_valid())
            throw std::runtime_error("Slice the active plate before inspecting the first layer");
        GCodeProcessorResult* result = plate->get_slice_result();
        if (result == nullptr)
            throw std::runtime_error("Slice result is not available");
        std::lock_guard<std::mutex> lock(result->result_mutex);
        float first_z = std::numeric_limits<float>::max();
        for (const auto& move : result->moves)
            if (move.type == EMoveType::Extrude && move.print_z > 0.0f)
                first_z = std::min(first_z, move.print_z);
        if (first_z == std::numeric_limits<float>::max())
            throw std::runtime_error("No first-layer extrusion moves were found");
        Vec3f min_pos = Vec3f::Constant(std::numeric_limits<float>::max());
        Vec3f max_pos = Vec3f::Constant(std::numeric_limits<float>::lowest());
        double distance = 0.0;
        double volume = 0.0;
        double max_flow = 0.0;
        double min_width = std::numeric_limits<double>::max();
        double max_width = 0.0;
        double fan_sum = 0.0;
        double temp_sum = 0.0;
        size_t count = 0;
        Vec3f previous = Vec3f::Zero();
        bool have_previous = false;
        for (const auto& move : result->moves) {
            if (move.type != EMoveType::Extrude || std::abs(move.print_z - first_z) > 0.0001f)
                continue;
            const double segment = have_previous ? (move.position - previous).norm() : 0.0;
            previous = move.position; have_previous = true;
            distance += segment;
            volume += segment * move.mm3_per_mm;
            max_flow = std::max(max_flow, static_cast<double>(move.volumetric_rate()));
            min_width = std::min(min_width, static_cast<double>(move.width));
            max_width = std::max(max_width, static_cast<double>(move.width));
            fan_sum += move.fan_speed;
            temp_sum += move.temperature;
            min_pos = min_pos.cwiseMin(move.position);
            max_pos = max_pos.cwiseMax(move.position);
            ++count;
        }
        return json{{"plate_index", plate->get_index()}, {"z_mm", first_z},
                    {"estimated_time_s", result->initial_layer_time}, {"extrusion_move_count", count},
                    {"extrusion_path_distance_mm", distance}, {"estimated_extrusion_volume_mm3", volume},
                    {"max_volumetric_rate_mm3_s", max_flow},
                    {"line_width_mm", {{"min", count ? min_width : 0.0}, {"max", max_width}}},
                    {"average_fan_percent", count ? fan_sum / count : 0.0},
                    {"average_nozzle_temperature_c", count ? temp_sum / count : 0.0},
                    {"bounds", {{"min_mm", {{"x", min_pos.x()}, {"y", min_pos.y()}}},
                                {"max_mm", {{"x", max_pos.x()}, {"y", max_pos.y()}}}}},
                    {"toolpath_outside", result->toolpath_outside}};
    });
}

json get_printer_details(QDSDeviceManager* manager, const json& args)
{
    if (manager == nullptr) return {{"error", "QIDI device manager is not available"}};
    const std::string device_id = required_string(args, "device_id");
    std::shared_ptr<QDSDevice> device = manager->getDevice(device_id);
    if (!device) return {{"error", "Printer was not found"}};
    json slots = json::array();
    const size_t slot_count = std::max({device->m_filament_type.size(), device->m_filament_colors.size(),
                                        device->m_slot_state.size(), device->m_slot_id.size()});
    for (size_t index = 0; index < slot_count; ++index) {
        const bool external = index == 16;
        slots.push_back({{"index", index},
            {"source", external ? "external" : "qidi_box"},
            {"box_index", !external ? json(index / 4) : json(nullptr)},
            {"box_slot_index", !external ? json(index % 4) : json(nullptr)},
            {"slot_id", index < device->m_slot_id.size() ? json(device->m_slot_id[index]) : json(nullptr)},
            {"state", index < device->m_slot_state.size() ? json(device->m_slot_state[index]) : json(nullptr)},
            {"type", index < device->m_filament_type.size() ? json(device->m_filament_type[index]) : json(nullptr)},
            {"color", index < device->m_filament_colors.size() ? json(device->m_filament_colors[index]) : json(nullptr)}});
    }
    return {{"device_id", device->m_id}, {"name", device->m_name}, {"ip", device->m_ip},
            {"online", device->is_online()}, {"selected", device->is_selected.load()},
            {"status", device->m_status}, {"print_state", device->m_print_state},
            {"filename", device->m_print_filename},
            {"progress_fraction", device->m_print_progress_float},
            {"layer", {{"current", device->m_print_cur_layer}, {"total", device->m_print_total_layer}}},
            {"temperatures", {{"nozzle", device->m_extruder_temperature}, {"nozzle_target", device->m_target_extruder},
                              {"bed", device->m_bed_temperature}, {"bed_target", device->m_target_bed},
                              {"chamber", device->m_chamber_temperature}, {"chamber_target", device->m_target_chamber}}},
            {"fans", {{"part", device->m_cooling_fan_speed}, {"auxiliary", device->m_auxiliary_fan_speed},
                      {"chamber", device->m_chamber_fan_speed}}},
            {"case_light", device->m_case_light}, {"filament_sensor", device->m_extruder_filament},
            {"reported_nozzle_diameters_mm", device->m_nozzle_diameter},
            {"qidi_box_count", device->m_box_count},
            {"current_physical_slot", device->m_cur_slot},
            {"external_physical_slot_id", 16},
            {"filament_slots", std::move(slots)},
            {"camera", {{"device_url", device->m_url}, {"current_print_image_url", device->m_print_png_url},
                        {"image_available", !device->m_print_png_url.empty()}}}};
}

json check_printer_readiness(QDSDeviceManager* manager, const json& args)
{
    if (manager == nullptr) return {{"error", "QIDI device manager is not available"}};
    const std::string device_id = required_string(args, "device_id");
    std::shared_ptr<QDSDevice> device = manager->getDevice(device_id);
    if (!device) return {{"error", "Printer was not found"}};
    const std::string state = lower_copy(device->m_print_state + " " + device->m_status);
    const bool busy = state.find("print") != std::string::npos || state.find("pause") != std::string::npos ||
                      state.find("calibrat") != std::string::npos;
    json blockers = json::array();
    if (!device->is_online()) blockers.push_back("printer_offline");
    if (busy) blockers.push_back("printer_busy");
    return {{"device_id", device->m_id}, {"name", device->m_name},
            {"ready_for_new_job", blockers.empty()}, {"online", device->is_online()},
            {"busy", busy}, {"status", device->m_status}, {"print_state", device->m_print_state},
            {"filament_sensor", device->m_extruder_filament}, {"blockers", std::move(blockers)},
            {"physical_checks_not_observable", json::array({"plate_cleanliness", "correct_build_plate_installed",
                                                             "spool_remaining", "nozzle_wear", "part_removed"})}};
}

json check_filament_quantity(const json& args)
{
    if (!args.contains("available_g") || !args["available_g"].is_object())
        return {{"error", "available_g must be an object keyed by zero-based filament/extruder index"}};
    const json available = args["available_g"];
    return on_gui_thread([available]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (!plate->is_slice_result_valid())
            throw std::runtime_error("Slice the active plate before checking filament quantity");
        GCodeProcessorResult* result = plate->get_slice_result();
        if (result == nullptr)
            throw std::runtime_error("Slice result is not available");
        std::lock_guard<std::mutex> lock(result->result_mutex);
        json filaments = json::array();
        bool sufficient = true;
        for (const auto& [index, volume] : result->print_statistics.total_volumes_per_extruder) {
            const double density = index < result->filament_densities.size() ? result->filament_densities[index] : 0.0;
            const double required = volume * density / 1000.0;
            const std::string key = std::to_string(index);
            const bool known = available.contains(key) && available[key].is_number();
            const double have = known ? available[key].get<double>() : 0.0;
            const bool enough = known && have >= required;
            sufficient = sufficient && enough;
            filaments.push_back({{"filament_index", index}, {"required_g", required},
                                 {"available_g", known ? json(have) : json(nullptr)},
                                 {"remaining_after_print_g", known ? json(have - required) : json(nullptr)},
                                 {"sufficient", enough}, {"density_g_cm3", density}});
        }
        return json{{"sufficient", sufficient}, {"filaments", std::move(filaments)},
                    {"includes_model_support_and_wipe_tower", true}};
    });
}

json get_calibration_recommendations(const json& args)
{
    const int filament_index = args.value("filament_index", 0);
    const bool new_filament = args.value("new_filament", true);
    const bool changed_nozzle = args.value("changed_nozzle", false);
    if (filament_index < 0) return {{"error", "filament_index must be non-negative"}};
    return on_gui_thread([filament_index, new_filament, changed_nozzle]() {
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr || static_cast<size_t>(filament_index) >= bundle->filament_presets.size())
            throw std::runtime_error("filament_index is out of range");
        const Preset* preset = bundle->filaments.find_preset(bundle->filament_presets[filament_index], false);
        if (preset == nullptr) throw std::runtime_error("Assigned filament preset was not found");
        const std::string type = first_string_option(preset->config, "filament_type");
        json sequence = json::array();
        if (new_filament) sequence.push_back({{"order", sequence.size() + 1}, {"test", "dry_and_condition"},
                                              {"reason", "Moisture can invalidate every later calibration."}});
        sequence.push_back({{"order", sequence.size() + 1}, {"test", "temperature"},
                            {"reason", "Establish bonding, surface finish, and stringing window."}});
        sequence.push_back({{"order", sequence.size() + 1}, {"test", "flow_ratio"},
                            {"reason", "Set dimensional extrusion after temperature is known."}});
        sequence.push_back({{"order", sequence.size() + 1}, {"test", "pressure_advance"},
                            {"reason", "Tune corners and transient flow after flow ratio."}});
        sequence.push_back({{"order", sequence.size() + 1}, {"test", "max_volumetric_speed"},
                            {"reason", "Find the safe speed ceiling for this filament/nozzle combination."}});
        if (changed_nozzle) sequence.push_back({{"order", sequence.size() + 1}, {"test", "dimensional_accuracy"},
                                                {"reason", "Confirm effective line width and XY compensation after nozzle change."}});
        if (lower_copy(type).find("tpu") != std::string::npos)
            sequence.push_back({{"order", sequence.size() + 1}, {"test", "retraction_and_speed"},
                                {"reason", "Flexible filament is especially sensitive to feed path compression and retraction."}});
        return json{{"filament", filament_profile_to_json(*preset, static_cast<size_t>(filament_index))},
                    {"changed_nozzle", changed_nozzle}, {"recommended_sequence", std::move(sequence)},
                    {"automatically_applied", false}};
    });
}

json build_print_preflight_report()
{
    Plater* plater = require_plater();
    PartPlate* plate = require_plate(plater);
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) throw std::runtime_error("Preset bundle is not available");
    DynamicPrintConfig full = bundle->full_config();
    const auto config_errors = full.validate(false);
    json objects = json::array();
    json blockers = json::array();
    size_t mesh_issue_count = 0;
    for (const auto& item : plate->get_obj_and_inst_set()) {
        ModelObject* object = require_model_object(plater, item.first);
        ModelInstance* instance = require_instance(plate, item.first, item.second);
        const TriangleMeshStats stats = object->get_object_stl_stats();
        const BuildVolume::ObjectState state = plater->build_volume().object_state(
            object->raw_indexed_triangle_set(), instance->get_matrix().cast<float>(), true, true);
        const bool mesh_ok = stats.manifold() && !stats.has_open_edges();
        if (!mesh_ok) ++mesh_issue_count;
        if (!mesh_ok) blockers.push_back("mesh_issue_object_" + std::to_string(item.first));
        if (state != BuildVolume::ObjectState::Inside)
            blockers.push_back("build_volume_object_" + std::to_string(item.first));
        objects.push_back({{"object_id", item.first}, {"instance_id", item.second}, {"name", object->name},
                           {"printable", instance->is_printable()}, {"mesh_ok", mesh_ok},
                           {"build_volume_state", build_volume_state_name(state)}});
    }
    if (!config_errors.empty()) blockers.push_back("active_configuration_invalid");
    if (plate->empty()) blockers.push_back("active_plate_empty");
    if (!plate->is_slice_result_valid()) blockers.push_back("slice_result_missing_or_stale");
    if (plate->is_slice_result_valid() && !plate->is_slice_result_ready_for_print())
        blockers.push_back("slice_not_ready_for_print");
    json warnings = json::array();
    if (GCodeProcessorResult* result = plate->get_slice_result(); result != nullptr) {
        std::lock_guard<std::mutex> lock(result->result_mutex);
        for (const auto& warning : result->warnings)
            warnings.push_back({{"level", warning.level}, {"message", warning.msg},
                                {"error_code", warning.error_code}});
        if (result->toolpath_outside) blockers.push_back("toolpath_outside_build_volume");
        if (result->gcode_check_result.error_code != 0) blockers.push_back("gcode_check_failed");
    }

    json sequential{{"applicable", false}, {"valid", true},
                    {"effective_mode", print_sequence_name(plate->get_real_print_seq())}};
    if (plate->get_real_print_seq() == PrintSequence::ByObject) {
        const bool plate_apply_invalid = plate->is_apply_result_invalid();
        Print* print = plate->fff_print();
        if (print == nullptr) {
            blockers.push_back("sequential_plate_invalid");
            sequential = {{"applicable", true}, {"valid", false},
                          {"effective_mode", "by_object"},
                          {"message", "The active plate has no sequential FFF print state"}};
        } else {
            Polygons collision_polygons;
            std::vector<std::pair<Polygon, float>> height_polygons;
            const StringObjectException issue =
                Print::sequential_print_clearance_valid(*print, &collision_polygons, &height_polygons);
            if (plate_apply_invalid) blockers.push_back("sequential_plate_invalid");
            if (!collision_polygons.empty()) blockers.push_back("sequential_horizontal_collision");
            if (!height_polygons.empty()) blockers.push_back("sequential_height_collision");
            if (!issue.string.empty() && collision_polygons.empty() && height_polygons.empty())
                blockers.push_back("sequential_clearance_invalid");
            const bool valid = !plate_apply_invalid && issue.string.empty() &&
                               collision_polygons.empty() && height_polygons.empty();
            sequential = {{"applicable", true}, {"valid", valid},
                          {"effective_mode", "by_object"},
                          {"plate_apply_invalid", plate_apply_invalid},
                          {"message", issue.string},
                          {"horizontal_collision_polygon_count", collision_polygons.size()},
                          {"height_collision_polygon_count", height_polygons.size()},
                          {"native_validator", "Print::sequential_print_clearance_valid"}};
        }
    }

    return json{{"plate_index", plate->get_index()}, {"objects", std::move(objects)},
                {"mesh_issue_count", mesh_issue_count},
                {"configuration", {{"valid", config_errors.empty()},
                                   {"errors", validation_errors_to_json(config_errors)}}},
                {"slice", {{"valid", plate->is_slice_result_valid()},
                           {"ready_for_print", plate->is_slice_result_ready_for_print()},
                           {"warnings", std::move(warnings)}}},
                {"sequential", std::move(sequential)},
                {"blockers", std::move(blockers)}};
}

json run_print_preflight(QDSDeviceManager* manager, const json& args)
{
    const std::string device_id = args.value("device_id", "");
    json report = on_gui_thread([]() { return build_print_preflight_report(); });
    if (!device_id.empty()) {
        json readiness = check_printer_readiness(manager, {{"device_id", device_id}});
        report["printer"] = readiness;
        if (readiness.contains("error") || !readiness.value("ready_for_new_job", false))
            report["blockers"].push_back("printer_not_ready");
    } else {
        report["printer"] = nullptr;
        report["unverified"] = json::array();
        report["unverified"].push_back("printer_readiness");
    }
    report["ready"] = report["blockers"].empty();
    report["requires_user_confirmation_to_send"] = true;
    return report;
}

json prepare_print_job(QDSDeviceManager* manager, const json& args)
{
    if (manager == nullptr)
        return {{"error", "QIDI device manager is not available"}};
    const std::string device_id = required_string(args, "device_id");
    if ((args.contains("bed_leveling") && !args["bed_leveling"].is_boolean()) ||
        (args.contains("timelapse") && !args["timelapse"].is_boolean()))
        return {{"error", "bed_leveling and timelapse must be booleans"}};
    const bool bed_leveling = args.value("bed_leveling", false);
    const bool timelapse = args.value("timelapse", false);
    if (!args.contains("filament_source") || !args["filament_source"].is_object())
        return {{"prepared", false}, {"error_code", "FILAMENT_SELECTION_REQUIRED"},
                {"error", "Choose the physical filament source before slicing and provide filament_source"}};
    const json& filament_source = args["filament_source"];
    if (!filament_source.contains("project_filament_index") ||
        !filament_source["project_filament_index"].is_number_integer())
        return {{"prepared", false}, {"error", "filament_source.project_filament_index must be a non-negative integer"}};
    const int project_filament_index = filament_source["project_filament_index"].get<int>();
    if (project_filament_index < 0)
        return {{"prepared", false}, {"error", "filament_source.project_filament_index must be non-negative"}};
    const std::string source = filament_source.value("source", "");
    if (source != "qidi_box" && source != "external")
        return {{"prepared", false}, {"error", "filament_source.source must be qidi_box or external"}};
    const bool use_qidi_box = source == "qidi_box";
    int physical_slot_id = 16;
    if (use_qidi_box) {
        if (!filament_source.contains("slot_id") || !filament_source["slot_id"].is_number_integer())
            return {{"prepared", false}, {"error", "filament_source.slot_id is required for qidi_box"}};
        physical_slot_id = filament_source["slot_id"].get<int>();
        if (physical_slot_id < 0 || physical_slot_id > 15)
            return {{"prepared", false}, {"error", "QIDI Box slot_id must be between 0 and 15"}};
    } else if (filament_source.contains("slot_id") &&
               (!filament_source["slot_id"].is_number_integer() ||
                filament_source["slot_id"].get<int>() != 16)) {
        return {{"prepared", false}, {"error", "The external source uses physical slot_id 16"}};
    }
    const int requested_ttl = args.value("expires_in_seconds", static_cast<int>(PRINT_TOKEN_DEFAULT_TTL.count()));
    if (requested_ttl < 60 || requested_ttl > static_cast<int>(PRINT_TOKEN_MAX_TTL.count()))
        return {{"error", "expires_in_seconds must be between 60 and 1800"}};

    std::shared_ptr<QDSDevice> device = manager->getDevice(device_id);
    if (!device)
        return {{"error", "Printer was not found"}};
    json readiness = check_printer_readiness(manager, {{"device_id", device_id}});
    if (readiness.contains("error") || !readiness.value("ready_for_new_job", false))
        return {{"prepared", false}, {"error", "Printer is not ready for a new job"},
                {"printer", std::move(readiness)}};
    if (device->m_ip.empty())
        return {{"prepared", false}, {"error", "The selected printer has no local/LAN address"},
                {"device_id", device_id}};

    if (use_qidi_box &&
        (static_cast<size_t>(physical_slot_id) >= device->m_slot_state.size() ||
         device->m_slot_state[static_cast<size_t>(physical_slot_id)] == 0))
        return {{"prepared", false}, {"error_code", "FILAMENT_SLOT_UNAVAILABLE"},
                {"error", "The selected QIDI Box slot is empty or unavailable"},
                {"slot_id", physical_slot_id}};
    const std::string physical_filament_type =
        static_cast<size_t>(physical_slot_id) < device->m_filament_type.size()
            ? device->m_filament_type[static_cast<size_t>(physical_slot_id)] : std::string();
    const std::string physical_filament_color =
        static_cast<size_t>(physical_slot_id) < device->m_filament_colors.size()
            ? device->m_filament_colors[static_cast<size_t>(physical_slot_id)] : std::string();

    json locked = on_gui_thread([bed_leveling, timelapse, project_filament_index,
                                 use_qidi_box, physical_slot_id,
                                 physical_filament_type, physical_filament_color]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        json preflight = build_print_preflight_report();
        if (!preflight["blockers"].empty())
            return json{{"prepared", false}, {"error", "Print preflight has blockers"},
                        {"preflight", std::move(preflight)}};
        PresetBundle* bundle = wxGetApp().preset_bundle;
        if (bundle == nullptr)
            throw std::runtime_error("Preset bundle is not available");
        if (!bundle->printers.get_edited_preset().config.opt_bool("is_support_3mf"))
            return json{{"prepared", false},
                        {"error", "v1.3 requires the selected QIDI printer preset to support native .gcode.3mf packaging"},
                        {"preflight", std::move(preflight)}};

        if (static_cast<size_t>(project_filament_index) >= bundle->filament_presets.size())
            return json{{"prepared", false}, {"error_code", "FILAMENT_SELECTION_INVALID"},
                        {"error", "The selected project filament index is out of range"},
                        {"project_filament_index", project_filament_index},
                        {"project_filament_count", bundle->filament_presets.size()}};
        GCodeProcessorResult* slice_result = plate->get_slice_result();
        if (slice_result == nullptr)
            return json{{"prepared", false}, {"error", "The active plate has no slice result"}};
        std::vector<size_t> used_filaments;
        {
            std::lock_guard<std::mutex> lock(slice_result->result_mutex);
            for (const auto& entry : slice_result->print_statistics.total_volumes_per_extruder)
                if (entry.second > 0.0)
                    used_filaments.push_back(entry.first);
        }
        if (used_filaments.size() != 1)
            return json{{"prepared", false}, {"error_code", "MULTI_FILAMENT_DIRECT_PRINT_UNSUPPORTED"},
                        {"error", "Guarded direct printing currently requires exactly one sliced project filament; use QIDI's native send dialog for multi-filament mapping"},
                        {"used_project_filament_indices", used_filaments}};
        if (used_filaments.front() != static_cast<size_t>(project_filament_index))
            return json{{"prepared", false}, {"error_code", "RESLICE_WITH_SELECTED_FILAMENT"},
                        {"error", "The active slice was not generated with the selected project filament; assign that filament to the model and slice again"},
                        {"selected_project_filament_index", project_filament_index},
                        {"sliced_project_filament_index", used_filaments.front()}};
        const std::string project_filament_preset =
            bundle->filament_presets[static_cast<size_t>(project_filament_index)];

        const PrintStatistics& stats =
            plater->get_partplate_list().get_current_fff_print().print_statistics();
        DynamicPrintConfig full = bundle->full_config();
        const auto setting = [&full](const char* key) -> json {
            const ConfigOption* option = full.option(key);
            return option != nullptr ? json(serialize_config_option(key, option)) : json(nullptr);
        };
        json summary{
            {"project", {{"name", into_u8(plater->get_project_name())},
                         {"filename", into_u8(plater->get_project_filename())}}},
            {"plate", {{"index", plate->get_index()}, {"name", plate->get_plate_name()}}},
            {"profiles", {{"printer", bundle->printers.get_selected_preset_name()},
                          {"print", bundle->prints.get_selected_preset_name()},
                          {"filaments", bundle->filament_presets}}},
            {"print_sequence", print_object_sequence_payload(plater, plate)},
            {"estimated", {{"normal_print_time", stats.estimated_normal_print_time},
                            {"silent_print_time", stats.estimated_silent_print_time},
                            {"used_filament_mm", stats.total_used_filament},
                            {"extruded_volume_mm3", stats.total_extruded_volume},
                            {"weight_g", stats.total_weight},
                            {"toolchanges", stats.total_toolchanges}}},
            {"settings", {{"support_enabled", setting("enable_support")},
                          {"support_type", setting("support_type")},
                          {"brim_type", setting("brim_type")},
                          {"raft_layers", setting("raft_layers")}}},
            {"printer_options", {{"bed_leveling", bed_leveling}, {"timelapse", timelapse}}},
            {"filament_source", {{"project_filament_index", project_filament_index},
                                 {"project_filament_preset", project_filament_preset},
                                 {"source", use_qidi_box ? "qidi_box" : "external"},
                                 {"physical_slot_id", physical_slot_id},
                                 {"physical_filament_type", physical_filament_type.empty() ? json(nullptr) : json(physical_filament_type)},
                                 {"physical_filament_color", physical_filament_color.empty() ? json(nullptr) : json(physical_filament_color)}}},
            {"physical_checks_required", json::array({"build_plate_installed_clean_and_empty",
                                                       "selected_physical_filament_matches_summary",
                                                       "sufficient_filament_remaining",
                                                       "no_obstruction_in_toolhead_path"})}
        };
        return json{{"prepared", true}, {"fingerprint", print_job_fingerprint(plater, plate)},
                    {"plate_index", plate->get_index()},
                    {"upload_name", packaged_upload_name(plater, plate->get_index())},
                    {"summary", std::move(summary)}, {"preflight", std::move(preflight)}};
    });
    if (!locked.value("prepared", false))
        return locked;

    const auto now = std::chrono::system_clock::now();
    PreparedPrintJob prepared;
    prepared.token = random_hex_id(24);
    prepared.fingerprint = locked.value("fingerprint", "");
    prepared.device_id = device_id;
    prepared.printer_name = device->m_name;
    prepared.upload_name = locked.value("upload_name", "");
    prepared.plate_index = locked.value("plate_index", -1);
    prepared.bed_leveling = bed_leveling;
    prepared.timelapse = timelapse;
    prepared.project_filament_index = project_filament_index;
    prepared.physical_slot_id = physical_slot_id;
    prepared.use_qidi_box = use_qidi_box;
    prepared.project_filament_preset =
        locked["summary"]["filament_source"].value("project_filament_preset", "");
    prepared.physical_filament_type = physical_filament_type;
    prepared.physical_filament_color = physical_filament_color;
    prepared.created_at = now;
    prepared.expires_at = now + std::chrono::seconds(requested_ttl);
    {
        std::lock_guard<std::mutex> lock(print_job_mutex());
        auto& jobs = prepared_print_jobs();
        for (auto iterator = jobs.begin(); iterator != jobs.end();) {
            if (iterator->second.expires_at <= now || iterator->second.used)
                iterator = jobs.erase(iterator);
            else
                ++iterator;
        }
        jobs[prepared.token] = prepared;
    }

    locked.erase("fingerprint");
    locked["device_id"] = device_id;
    locked["printer_name"] = device->m_name;
    locked["confirmation_token"] = prepared.token;
    locked["token_single_use"] = true;
    locked["expires_at_utc"] = utc_time_string(prepared.expires_at);
    locked["next_tool"] = "start_print_job";
    locked["requires_confirm_true"] = true;
    return locked;
}

json start_print_job(QDSDeviceManager* manager, const json& args)
{
    if (manager == nullptr)
        return {{"error", "QIDI device manager is not available"}};
    const std::string token = required_string(args, "confirmation_token");
    if (!args.value("confirm", false))
        return {{"accepted", false}, {"error", "Set confirm=true to upload and start this prepared print job"}};

    PreparedPrintJob prepared;
    std::shared_ptr<ActivePrintJob> active;
    const auto now = std::chrono::system_clock::now();
    {
        std::lock_guard<std::mutex> lock(print_job_mutex());
        auto found = prepared_print_jobs().find(token);
        if (found == prepared_print_jobs().end())
            return {{"accepted", false}, {"error", "Confirmation token was not found or has already been consumed"}};
        if (found->second.used)
            return {{"accepted", false}, {"error", "Confirmation token has already been used"}};
        if (found->second.expires_at <= now) {
            prepared_print_jobs().erase(found);
            return {{"accepted", false}, {"error", "Confirmation token has expired; prepare the job again"}};
        }
        found->second.used = true;
        prepared = found->second;
        active = std::make_shared<ActivePrintJob>();
        active->job_id = random_hex_id(16);
        active->device_id = prepared.device_id;
        active->printer_name = prepared.printer_name;
        active->upload_name = prepared.upload_name;
        active->bed_leveling = prepared.bed_leveling;
        active->timelapse = prepared.timelapse;
        active->project_filament_index = prepared.project_filament_index;
        active->physical_slot_id = prepared.physical_slot_id;
        active->use_qidi_box = prepared.use_qidi_box;
        active->project_filament_preset = prepared.project_filament_preset;
        active->physical_filament_type = prepared.physical_filament_type;
        active->physical_filament_color = prepared.physical_filament_color;
        active->created_at = active->updated_at = now;
        active_print_jobs()[active->job_id] = active;
    }

    std::shared_ptr<QDSDevice> device = manager->getDevice(prepared.device_id);
    if (!device || !device->is_online() || device->m_ip.empty()) {
        update_active_job(active, "failed", "Printer is offline or has no local/LAN address");
        return {{"accepted", false}, {"job_id", active->job_id}, {"error", active->error}};
    }
    json readiness = check_printer_readiness(manager, {{"device_id", prepared.device_id}});
    if (readiness.contains("error") || !readiness.value("ready_for_new_job", false)) {
        update_active_job(active, "failed", "Printer is not ready for a new job");
        return {{"accepted", false}, {"job_id", active->job_id}, {"error", active->error},
                {"printer", std::move(readiness)}};
    }
    if (prepared.use_qidi_box) {
        const size_t slot = static_cast<size_t>(prepared.physical_slot_id);
        if (slot >= device->m_slot_state.size() || device->m_slot_state[slot] == 0) {
            update_active_job(active, "failed", "The selected QIDI Box slot became empty or unavailable");
            return {{"accepted", false}, {"job_id", active->job_id},
                    {"error_code", "FILAMENT_SLOT_UNAVAILABLE"}, {"error", active->error}};
        }
        const std::string current_type = slot < device->m_filament_type.size()
            ? device->m_filament_type[slot] : std::string();
        const std::string current_color = slot < device->m_filament_colors.size()
            ? device->m_filament_colors[slot] : std::string();
        if ((!prepared.physical_filament_type.empty() &&
             current_type != prepared.physical_filament_type) ||
            (!prepared.physical_filament_color.empty() &&
             current_color != prepared.physical_filament_color)) {
            update_active_job(active, "failed", "The selected QIDI Box filament changed after print preparation");
            return {{"accepted", false}, {"job_id", active->job_id},
                    {"error_code", "FILAMENT_SLOT_CHANGED"}, {"error", active->error},
                    {"expected", {{"type", prepared.physical_filament_type},
                                  {"color", prepared.physical_filament_color}}},
                    {"current", {{"type", current_type}, {"color", current_color}}}};
        }
    }

    json package = on_gui_thread([prepared]() {
        Plater* plater = require_plater();
        PartPlate* plate = require_plate(plater);
        if (plate->get_index() != prepared.plate_index)
            throw std::runtime_error("The active plate changed after print preparation");
        if (print_job_fingerprint(plater, plate) != prepared.fingerprint)
            throw std::runtime_error("The model, settings, slice, or object order changed after print preparation");
        json preflight = build_print_preflight_report();
        if (!preflight["blockers"].empty())
            return json{{"packaged", false}, {"error", "Print preflight changed or has blockers"},
                        {"preflight", std::move(preflight)}};
        if (plater->send_gcode(prepared.plate_index) < 0)
            throw std::runtime_error("QIDI Studio could not package the active plate");
        PrintPrepareData data;
        plater->get_print_job_data(&data);
        if (data._3mf_path.empty() || !boost::filesystem::exists(data._3mf_path))
            throw std::runtime_error("QIDI Studio did not produce a packaged .gcode.3mf file");
        return json{{"packaged", true}, {"source_path", data._3mf_path.string()}};
    });
    if (!package.value("packaged", false)) {
        const std::string error = package.value("error", "QIDI Studio could not package the print job");
        update_active_job(active, "failed", error);
        package.erase("source_path");
        package["accepted"] = false;
        package["job_id"] = active->job_id;
        return package;
    }

    const std::string printer_ip = device->m_ip;
    const std::string source_path = package.value("source_path", "");
    update_active_job(active, "uploading", {}, 0);
    std::thread([active, printer_ip, source_path]() mutable {
        try {
            PrintHostJob upload_job(printer_ip, printer_ip);
            if (upload_job.empty()) {
                update_active_job(active, "failed", "QIDI could not create the native printer upload job");
                return;
            }
            wxString status_message;
            const auto status = upload_job.printhost->get_status_progress(status_message);
            const std::string printer_state = lower_copy(status.first);
            if (printer_state == "offline") {
                update_active_job(active, "failed", "Printer became offline before upload");
                return;
            }
            if (printer_state != "standby") {
                update_active_job(active, "failed", "Printer is no longer in standby: " + status.first);
                return;
            }
            const wxString box_mode_command = active->use_qidi_box
                ? "SAVE_VARIABLE VARIABLE=enable_box VALUE=1"
                : "SAVE_VARIABLE VARIABLE=enable_box VALUE=0";
            if (!upload_job.printhost->send_command_to_printer(status_message, box_mode_command)) {
                update_active_job(active, "failed", "Printer rejected the physical filament source mode");
                return;
            }
            if (active->use_qidi_box) {
                const wxString slot_command = wxString::Format(
                    "SAVE_VARIABLE VARIABLE=value_t%d VALUE=\\\"'slot%d'\\\"",
                    active->project_filament_index, active->physical_slot_id);
                if (!upload_job.printhost->send_command_to_printer(status_message, slot_command)) {
                    update_active_job(active, "failed", "Printer rejected the QIDI Box filament-slot mapping");
                    return;
                }
            }
            const std::string leveling_command = active->bed_leveling ? "G31" : "G32";
            if (!upload_job.printhost->send_command_to_printer(status_message, leveling_command)) {
                update_active_job(active, "failed", "Printer rejected the bed-leveling option");
                return;
            }
            if (!upload_job.printhost->send_timelapse_status(
                    status_message, printer_ip, active->timelapse)) {
                update_active_job(active, "failed", "Printer rejected the timelapse option");
                return;
            }

            upload_job.upload_data.upload_path = active->upload_name;
            upload_job.upload_data.post_action = PrintHostPostUploadAction::StartPrint;
            upload_job.upload_data.source_path = source_path;
            upload_job.upload_data.is_3mf = true;
            const bool uploaded = upload_job.printhost->upload(
                std::move(upload_job.upload_data),
                [active](Http::Progress progress, bool& cancel) {
                    cancel = false;
                    const int percent = progress.ultotal > 0
                        ? static_cast<int>(100 * progress.ulnow / progress.ultotal) : 0;
                    update_active_job(active, "uploading", {}, percent);
                },
                [active](wxString error) {
                    update_active_job(active, "failed", into_u8(error));
                });
            if (uploaded)
                update_active_job(active, "uploaded_awaiting_printer", {}, 100);
            else {
                std::lock_guard<std::mutex> lock(print_job_mutex());
                if (active->stage != "failed") {
                    active->stage = "failed";
                    active->error = "Native upload/start request failed";
                    active->updated_at = std::chrono::system_clock::now();
                }
            }
        } catch (const std::exception& error) {
            update_active_job(active, "failed", error.what());
        } catch (...) {
            update_active_job(active, "failed", "Unknown native upload/start failure");
        }
    }).detach();

    return {{"accepted", true}, {"job_id", active->job_id},
            {"device_id", active->device_id}, {"printer_name", active->printer_name},
            {"filament_source", {{"project_filament_index", active->project_filament_index},
                                 {"project_filament_preset", active->project_filament_preset},
                                 {"source", active->use_qidi_box ? "qidi_box" : "external"},
                                 {"physical_slot_id", active->physical_slot_id},
                                 {"physical_filament_type", active->physical_filament_type.empty() ? json(nullptr) : json(active->physical_filament_type)},
                                 {"physical_filament_color", active->physical_filament_color.empty() ? json(nullptr) : json(active->physical_filament_color)}}},
            {"expected_filename", active->upload_name}, {"stage", "uploading"},
            {"message", "The native upload/start job was accepted; use get_print_job_status to verify printer acceptance"}};
}

json get_print_job_status(QDSDeviceManager* manager, const json& args)
{
    if (manager == nullptr)
        return {{"error", "QIDI device manager is not available"}};
    const std::string job_id = required_string(args, "job_id");
    std::shared_ptr<ActivePrintJob> job;
    {
        std::lock_guard<std::mutex> lock(print_job_mutex());
        const auto found = active_print_jobs().find(job_id);
        if (found == active_print_jobs().end())
            return {{"error", "Print job was not found"}};
        job = found->second;
    }

    std::shared_ptr<QDSDevice> device = manager->getDevice(job->device_id);
    bool matching_job_reported = false;
    bool filament_source_confirmed = false;
    bool filament_source_verification_pending = false;
    bool filament_source_mismatch = false;
    std::string reported_slot;
    if (device) {
        const std::string state = lower_copy(device->m_print_state + " " + device->m_status);
        const bool printing_or_paused = state.find("print") != std::string::npos ||
                                        state.find("pause") != std::string::npos;
        const bool preparing = state.find("prepare") != std::string::npos;
        const bool busy = printing_or_paused || preparing;
        matching_job_reported = print_filename_matches(job->upload_name, device->m_print_filename);
        reported_slot = lower_copy(device->m_cur_slot);
        const std::string expected_slot = "slot" + std::to_string(job->physical_slot_id);
        const std::string expected_slot_with_separator = "slot-" + std::to_string(job->physical_slot_id);
        filament_source_confirmed = !reported_slot.empty() &&
            (reported_slot == expected_slot || reported_slot == expected_slot_with_separator);
        // m_cur_slot describes the filament physically loaded in the toolhead, not
        // merely the mapping selected for the queued job.  During heating, homing,
        // and Box unload/load it legitimately continues to report the prior slot.
        // Treat that as pending until the first print layer begins.
        const bool first_layer_started = device->m_print_cur_layer > 0;
        filament_source_verification_pending = matching_job_reported && busy &&
            !filament_source_confirmed && !first_layer_started;
        filament_source_mismatch = matching_job_reported && busy &&
            !filament_source_confirmed && first_layer_started;
        std::lock_guard<std::mutex> lock(print_job_mutex());
        if (job->stage != "failed") {
            std::string next_stage;
            if (filament_source_mismatch)
                next_stage = "filament_source_mismatch";
            else if (matching_job_reported && busy && filament_source_confirmed)
                next_stage = printing_or_paused ? "printing" : "printer_preparing";
            else if (filament_source_verification_pending)
                next_stage = "awaiting_filament_source";
            else if (matching_job_reported && job->stage == "uploaded_awaiting_printer")
                next_stage = "printer_accepted";
            else if ((job->stage == "printing" || job->stage == "printer_preparing" ||
                      job->stage == "awaiting_filament_source" ||
                      job->stage == "filament_source_mismatch") && !busy)
                next_stage = "completed_or_stopped";
            if (!next_stage.empty() && next_stage != job->stage) {
                job->stage = std::move(next_stage);
                job->updated_at = std::chrono::system_clock::now();
            } else if (job->stage == "uploaded_awaiting_printer" &&
                       std::chrono::system_clock::now() - job->updated_at > std::chrono::minutes(10)) {
                job->stage = "failed";
                job->error = "Timed out waiting for the printer to report the matching filename";
                job->updated_at = std::chrono::system_clock::now();
            }
        }
    }

    std::lock_guard<std::mutex> lock(print_job_mutex());
    return {{"job_id", job->job_id}, {"device_id", job->device_id},
            {"printer_name", job->printer_name}, {"stage", job->stage},
            {"filament_source", {{"project_filament_index", job->project_filament_index},
                                 {"project_filament_preset", job->project_filament_preset},
                                 {"source", job->use_qidi_box ? "qidi_box" : "external"},
                                 {"physical_slot_id", job->physical_slot_id},
                                 {"physical_filament_type", job->physical_filament_type.empty() ? json(nullptr) : json(job->physical_filament_type)},
                                 {"physical_filament_color", job->physical_filament_color.empty() ? json(nullptr) : json(job->physical_filament_color)}}},
            {"upload_progress_percent", job->upload_progress_percent},
            {"expected_filename", job->upload_name},
            {"reported_filename", device ? json(device->m_print_filename) : json(nullptr)},
            {"reported_physical_slot", device ? json(device->m_cur_slot) : json(nullptr)},
            {"matching_print_job_reported", matching_job_reported},
            {"physical_filament_source_confirmed", filament_source_confirmed},
            {"physical_filament_source_verification_pending", filament_source_verification_pending},
            {"physical_filament_source_mismatch", filament_source_mismatch},
            {"printer_acceptance_confirmed", matching_job_reported && filament_source_confirmed},
            {"printing_confirmed", job->stage == "printing" && filament_source_confirmed},
            {"cancel_recommended", filament_source_mismatch},
            {"filament_source_status", filament_source_confirmed ? "confirmed" :
                (filament_source_mismatch ? "mismatch" :
                 (filament_source_verification_pending ? "pending_physical_load" : "unavailable"))},
            {"filament_source_warning", filament_source_mismatch
                ? json("The first print layer began without confirmation of the selected physical filament slot; cancel the print")
                : json(nullptr)},
            {"error", job->error.empty() ? json(nullptr) : json(job->error)},
            {"created_at_utc", utc_time_string(job->created_at)},
            {"updated_at_utc", utc_time_string(job->updated_at)},
            {"printer", device ? json{{"online", device->is_online()}, {"status", device->m_status},
                                      {"print_state", device->m_print_state},
                                      {"progress_fraction", device->m_print_progress_float},
                                      {"layer", {{"current", device->m_print_cur_layer},
                                                 {"total", device->m_print_total_layer}}}} : json(nullptr)}};
}

json tool_definition(const char* name, const char* description, json properties = json::object(),
                     json required = json::array(), bool additional_properties = false)
{
    json schema = {
        {"type", "object"},
        {"properties", std::move(properties)},
        {"additionalProperties", additional_properties}
    };
    if (!required.empty())
        schema["required"] = std::move(required);
    return {{"name", name}, {"description", description}, {"inputSchema", std::move(schema)},
            {"outputSchema", {{"type", "object"}}}};
}

json capture_viewer_tool(json tool)
{
    tool["_meta"] = {
        {"ui", {{"resourceUri", CAPTURE_VIEWER_URI}}},
        {"openai/outputTemplate", CAPTURE_VIEWER_URI},
        {"openai/toolInvocation/invoking", "Capturing image..."},
        {"openai/toolInvocation/invoked", "Capture ready"}
    };
    return tool;
}

json tools_list()
{
    const json integer_id = {{"type", "integer"}, {"minimum", 0}};
    const json vec3 = {{"type", "object"}, {"properties", {
        {"x", {{"type", "number"}}}, {"y", {{"type", "number"}}}, {"z", {{"type", "number"}}}
    }}, {"additionalProperties", false}};
    const json scope = {{"type", "string"}, {"enum", json::array({"print", "filament", "printer"})}};
    const json setting_scope = {{"type", "string"},
                                {"enum", json::array({"print", "filament", "printer", "object", "volume"})}};
    const json surface_vec3 = {
        {"type", "object"},
        {"properties", {{"x", {{"type", "number"}}},
                        {"y", {{"type", "number"}}},
                        {"z", {{"type", "number"}}}}},
        {"required", json::array({"x", "y", "z"})},
        {"additionalProperties", false}
    };
    const json surface_selector = {
        {"type", "object"},
        {"properties", {
            {"type", {{"type", "string"},
                      {"enum", json::array({"all", "facet_ids", "height_range", "bounding_box", "normal", "overhang"})}}},
            {"facet_ids", {{"type", "array"}, {"minItems", 1}, {"uniqueItems", true}, {"items", integer_id}}},
            {"min_z_mm", {{"type", "number"}}},
            {"max_z_mm", {{"type", "number"}}},
            {"min_mm", surface_vec3},
            {"max_mm", surface_vec3},
            {"direction", surface_vec3},
            {"max_angle_deg", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 180.0}, {"default", 15.0}}},
            {"match", {{"type", "string"},
                       {"enum", json::array({"centroid", "any_vertex", "all_vertices"})},
                       {"default", "centroid"}}}
        }},
        {"required", json::array({"type"})},
        {"additionalProperties", false}
    };
    const json paint_state = {{"type", "string"},
                              {"enum", json::array({"enforcer", "blocker", "erase"})}};

    json tools = json::array();
    tools.push_back(tool_definition("list_printers",
        "List printers known to QIDI Studio and return their current read-only status."));
    tools.push_back(tool_definition("get_tunnel_status",
        "Report provider-neutral tunnel configuration, supervisor heartbeat freshness, and derived health without exposing executable arguments or credentials."));
    tools.push_back(tool_definition("get_recovery_state",
        "Report whether QIDI Studio is showing its startup project-recovery dialog and which decisions are available."));
    tools.push_back(tool_definition("resolve_project_recovery",
        "Choose Restore or Cancel in QIDI Studio's native project-recovery dialog. Cancel deletes pending recovery data and requires confirm=true.",
        {{"action", {{"type", "string"}, {"enum", json::array({"restore", "cancel"})}}},
         {"confirm", {{"type", "boolean"}, {"default", false}}}},
        {"action"}));
    tools.push_back(tool_definition("get_ui_state",
        "Report QIDI Studio's selected view, visible dialogs, modal blocking state, and project-recovery state."));
    tools.push_back(capture_viewer_tool(tool_definition("capture_studio_screenshot",
        "Return a PNG image for the QIDI Studio capture viewer plus short-lived local download coordinates. Display the returned image inline. If the client does not render MCP Apps, materialize the image content as a temporary PNG attachment and embed it in the response; do not claim it is displayed unless it is visibly attached. On Windows, background=true temporarily restores a minimized or hidden window outside the visible desktop without activation, captures it, composites the OpenGL build-plate canvas, and restores its prior tab and window state. The local download fallback is download.scheme + '://' + download.origin + download.path.",
        {{"target", {{"type", "string"}, {"enum", json::array({"current", "prepare", "preview"})},
                     {"default", "current"}}},
         {"background", {{"type", "boolean"}, {"default", true}}}})));
    tools.push_back(tool_definition("get_plate_state",
        "Return the active build plate and every object instance transform."));
    tools.push_back(tool_definition("get_print_object_sequence",
        "Return the active plate's effective print mode and native model-object print sequence, including stable identity values because native reordering can renumber object_id indices."));
    tools.push_back(tool_definition("set_print_object_sequence",
        "Set the native model-object order consumed by sequential printing using an exact permutation of the current printable active-plate object_id indices, with an undo snapshot and stable identity remapping; run validate_print_by_object afterward.",
        {{"object_ids", {{"type", "array"}, {"minItems", 1}, {"uniqueItems", true}, {"items", integer_id}}},
         {"enable_by_object", {{"type", "boolean"}, {"default", true}}}}, {"object_ids"}));
    tools.push_back(tool_definition("validate_print_by_object",
        "Run QIDI's native sequential-print horizontal and vertical toolhead-clearance validator on the active plate."));
    tools.push_back(tool_definition("list_objects",
        "List every model object with geometry counts, transformed bounds, and instance transforms."));
    tools.push_back(tool_definition("get_object_state",
        "Return detailed geometry, bounds, printable state, and transforms for one model object.",
        {{"object_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("get_model_diagnostics",
        "Return exact mesh errors, open edges, repair history, per-volume diagnostics, and active slice warnings such as floating regions. Omit object_id to inspect all objects.",
        {{"object_id", integer_id}}));
    tools.push_back(tool_definition("list_object_volumes",
        "List every volume in one model object with type, filament assignment, transforms, bounds, and mesh diagnostics.",
        {{"object_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("repair_object_mesh",
        "Repair one model object using QIDI Studio's native Windows mesh-repair service. Geometry or painted facets may change; requires confirm=true.",
        {{"object_id", integer_id},
         {"confirm", {{"type", "boolean"}, {"default", false}}}},
        {"object_id", "confirm"}));
    tools.push_back(tool_definition("center_object",
        "Center one object instance in XY on the active plate while preserving its orientation and scale.",
        {{"object_id", integer_id}, {"instance_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("drop_object_to_bed",
        "Move all instances of a model object in Z so the object's lowest point is on the build plate.",
        {{"object_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("mirror_object",
        "Mirror one object instance across its local X, Y, or Z axis.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"axis", {{"type", "string"}, {"enum", json::array({"x", "y", "z"})}, {"default", "x"}}}},
        {"object_id"}));
    tools.push_back(tool_definition("cut_object_horizontal",
        "Cut an object at an absolute build-plate Z height using QIDI Studio's native horizontal-cut engine. Requires confirm=true.",
        {{"object_id", integer_id}, {"instance_id", integer_id}, {"z_mm", {{"type", "number"}}},
         {"keep", {{"type", "string"}, {"enum", json::array({"upper", "lower", "both"})}, {"default", "both"}}},
         {"confirm", {{"type", "boolean"}, {"default", false}}}},
        {"object_id", "z_mm", "confirm"}));
    tools.push_back(tool_definition("undo",
        "Undo the most recent QIDI Studio project/model change."));
    tools.push_back(tool_definition("redo",
        "Redo the most recently undone QIDI Studio project/model change."));
    tools.push_back(tool_definition("get_object_settings",
        "Read effective object-scope settings. With no keys, return only current object overrides.",
        {{"object_id", integer_id}, {"keys", {{"type", "array"}, {"items", {{"type", "string"}}}}}},
        {"object_id"}));
    tools.push_back(tool_definition("set_object_settings",
        "Set object-scope overrides using QIDI's canonical serialized string values.",
        {{"object_id", integer_id}, {"values", {{"type", "object"}, {"minProperties", 1},
                                                  {"additionalProperties", {{"type", "string"}}}}}},
        {"object_id", "values"}));
    tools.push_back(tool_definition("reset_object_settings",
        "Remove selected object-scope overrides so those settings inherit from the active print preset.",
        {{"object_id", integer_id}, {"keys", {{"type", "array"}, {"minItems", 1},
                                                {"items", {{"type", "string"}}}}}},
        {"object_id", "keys"}));
    tools.push_back(tool_definition("get_volume_settings",
        "Read effective volume-scope settings. With no keys, return only current volume overrides; effective values inherit from object overrides before the print preset.",
        {{"object_id", integer_id}, {"volume_id", integer_id},
         {"keys", {{"type", "array"}, {"items", {{"type", "string"}}}}}},
        {"object_id", "volume_id"}));
    tools.push_back(tool_definition("set_volume_settings",
        "Set volume-scope overrides using QIDI's canonical serialized string values.",
        {{"object_id", integer_id}, {"volume_id", integer_id},
         {"values", {{"type", "object"}, {"minProperties", 1},
                     {"additionalProperties", {{"type", "string"}}}}}},
        {"object_id", "volume_id", "values"}));
    tools.push_back(tool_definition("reset_volume_settings",
        "Remove selected volume-scope overrides so those settings inherit from the object or active print preset.",
        {{"object_id", integer_id}, {"volume_id", integer_id},
         {"keys", {{"type", "array"}, {"minItems", 1},
                    {"items", {{"type", "string"}}}}}},
        {"object_id", "volume_id", "keys"}));
    tools.push_back(tool_definition("list_setting_definitions",
        "Search and paginate QIDI's native setting metadata, supported scopes, serialized defaults, limits, enum choices, and optional current/effective values.",
        {{"scope", setting_scope},
         {"query", {{"type", "string"}, {"default", ""}}},
         {"category", {{"type", "string"}, {"default", ""}}},
         {"filament_index", integer_id},
         {"object_id", integer_id},
         {"volume_id", integer_id},
         {"offset", {{"type", "integer"}, {"minimum", 0}, {"default", 0}}},
         {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 100}}}},
        {"scope"}));
    tools.push_back(tool_definition("preview_settings_update",
        "Strictly parse and normalize a proposed print, filament, printer, object, or volume settings update without mutation; report inheritance, slice invalidation, and native full-configuration validation.",
        {{"scope", setting_scope},
         {"filament_index", integer_id},
         {"object_id", integer_id},
         {"volume_id", integer_id},
         {"values", {{"type", "object"}, {"minProperties", 1},
                     {"additionalProperties", {{"type", "string"}}}}}},
        {"scope", "values"}));
    tools.push_back(tool_definition("get_layer_height_profile",
        "Inspect one object's stored variable layer-height profile and its effective profile under the active presets.",
        {{"object_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("preview_adaptive_layer_height",
        "Use QIDI's native adaptive layer-height engine to preview a speed-to-quality profile without changing the project.",
        {{"object_id", integer_id},
         {"quality_factor", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}, {"default", 0.5}}}},
        {"object_id"}));
    tools.push_back(tool_definition("apply_adaptive_layer_height",
        "Apply QIDI's native adaptive layer-height profile to one object with an undo snapshot.",
        {{"object_id", integer_id},
         {"quality_factor", {{"type", "number"}, {"minimum", 0.0}, {"maximum", 1.0}, {"default", 0.5}}}},
        {"object_id"}));
    tools.push_back(tool_definition("smooth_layer_height_profile",
        "Smooth one object's existing variable layer-height profile using QIDI's native radius and keep-min controls, with an undo snapshot.",
        {{"object_id", integer_id},
         {"radius", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10}, {"default", 5}}},
         {"keep_min", {{"type", "boolean"}, {"default", false}}}},
        {"object_id"}));
    tools.push_back(tool_definition("reset_layer_height_profile",
        "Remove one object's variable layer-height profile and return it to the active uniform layer settings, with an undo snapshot.",
        {{"object_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("preview_surface_selection",
        "Preview whole source-mesh facets without changing the project. Select by explicit facet IDs, plate-space height or bounds, facing direction, overhang angle, or all facets; instance_id defines the reference transform.",
        {{"object_id", integer_id}, {"volume_id", integer_id}, {"instance_id", integer_id},
         {"selector", surface_selector},
         {"max_facet_ids", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5000}, {"default", 1000}}}},
        {"object_id", "volume_id", "selector"}));
    tools.push_back(tool_definition("get_support_paint_state",
        "Inspect native support-enforcer and support-blocker facet painting on one model-part volume.",
        {{"object_id", integer_id}, {"volume_id", integer_id}}, {"object_id", "volume_id"}));
    tools.push_back(tool_definition("set_support_paint",
        "Apply support-enforcer, support-blocker, or erase state to whole source-mesh facets with an undo snapshot. Painting is shared by every instance of the object. Use selector type all with state erase to clear the volume.",
        {{"object_id", integer_id}, {"volume_id", integer_id}, {"instance_id", integer_id},
         {"selector", surface_selector}, {"state", paint_state}},
        {"object_id", "volume_id", "selector", "state"}));
    tools.push_back(tool_definition("get_seam_paint_state",
        "Inspect native preferred-seam and blocked-seam facet painting on one model-part volume.",
        {{"object_id", integer_id}, {"volume_id", integer_id}}, {"object_id", "volume_id"}));
    tools.push_back(tool_definition("set_seam_paint",
        "Apply preferred-seam (enforcer), blocked-seam (blocker), or erase state to whole source-mesh facets with an undo snapshot. Painting is shared by every instance of the object. Use selector type all with state erase to clear the volume.",
        {{"object_id", integer_id}, {"volume_id", integer_id}, {"instance_id", integer_id},
         {"selector", surface_selector}, {"state", paint_state}},
        {"object_id", "volume_id", "selector", "state"}));
    json import_attached_models_tool = tool_definition("import_attached_models",
        "Download one or more user-attached ChatGPT model files through authorized temporary file URLs, import them into the current project with QIDI's native loader, and remove the temporary local copies. Supports STL, 3MF, OBJ, AMF, STEP, STP, and PLY.",
        {{"files", {{"type", "array"}, {"minItems", 1}, {"maxItems", MAX_ATTACHED_MODEL_FILES},
                    {"items", {{"type", "object"},
                               {"properties", {{"download_url", {{"type", "string"}}},
                                               {"file_id", {{"type", "string"}}},
                                               {"mime_type", {{"type", "string"}}},
                                               {"file_name", {{"type", "string"}}}}},
                               {"required", json::array({"download_url", "file_id"})},
                               {"additionalProperties", false}}}}},
         {"arrange", {{"type", "boolean"}, {"default", false}}}}, {"files"});
    import_attached_models_tool["annotations"] = {
        {"readOnlyHint", false}, {"destructiveHint", false},
        {"openWorldHint", true}, {"idempotentHint", false}
    };
    import_attached_models_tool["_meta"] = {{"openai/fileParams", json::array({"files"})}};
    tools.push_back(std::move(import_attached_models_tool));
    tools.push_back(tool_definition("import_model",
        "Import one or more model files into the current project.",
        {{"paths", {{"type", "array"}, {"minItems", 1}, {"items", {{"type", "string"}}}}},
         {"arrange", {{"type", "boolean"}, {"default", false}}}}, {"paths"}));
    tools.push_back(tool_definition("list_presets",
        "List available print, filament, and printer presets with compatibility metadata.",
        {{"scope", {{"type", "string"}, {"enum", json::array({"all", "print", "filament", "printer"})},
                    {"default", "all"}}},
         {"offset", {{"type", "integer"}, {"minimum", 0}, {"default", 0}}},
         {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 25}}}}));
    tools.push_back(tool_definition("get_active_presets",
        "Return the active printer, print, and filament presets."));
    tools.push_back(tool_definition("select_presets",
        "Select one or more presets. Printer is applied before print and filament.",
        {{"printer", {{"type", "string"}}}, {"print", {{"type", "string"}}},
         {"filament", {{"type", "string"}}}}));
    tools.push_back(tool_definition("set_project_filament",
        "Replace one existing zero-based project filament slot with a compatible filament preset using QIDI's native validation and rollback path.",
        {{"filament_index", integer_id},
         {"preset", {{"type", "string"}, {"minLength", 1}}}},
        {"filament_index", "preset"}));
    tools.push_back(tool_definition("get_slice_settings",
        "Read settings in QIDI's canonical serialized format.",
        {{"scope", scope},
         {"filament_index", {{"type", "integer"}, {"minimum", 0}}},
         {"keys", {{"type", "array"}, {"minItems", 1},
                   {"items", {{"type", "string"}}}}}}, {"scope", "keys"}));
    tools.push_back(tool_definition("set_slice_settings",
        "Atomically change settings using QIDI's canonical serialized string values.",
        {{"scope", scope}, {"values", {{"type", "object"}, {"minProperties", 1},
                                        {"additionalProperties", {{"type", "string"}}}}}},
        {"scope", "values"}));
    tools.push_back(tool_definition("reset_slice_settings",
        "Reset selected settings to the values stored in the active saved preset.",
        {{"scope", scope}, {"keys", {{"type", "array"}, {"minItems", 1},
                                      {"items", {{"type", "string"}}}}}}, {"scope", "keys"}));
    tools.push_back(tool_definition("save_preset_as",
        "Save current print, filament, or printer settings as a named preset without opening a dialog.",
        {{"scope", scope}, {"name", {{"type", "string"}, {"minLength", 1}}},
         {"overwrite", {{"type", "boolean"}, {"default", false}}},
         {"save_to_project", {{"type", "boolean"}, {"default", false}}}}, {"scope", "name"}));
    tools.push_back(tool_definition("transform_object",
        "Set any subset of an object's position, rotation in radians, or scale.",
        {{"object_id", integer_id}, {"instance_id", integer_id}, {"position_mm", vec3},
         {"rotation_rad", vec3}, {"scale", vec3}}, {"object_id", "instance_id"}));
    tools.push_back(tool_definition("duplicate_object",
        "Duplicate an object instance, optionally stepping each copy by an XYZ offset.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"copies", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 1}}},
         {"offset_step_mm", vec3}}, {"object_id"}));
    tools.push_back(tool_definition("rename_object",
        "Rename a model object and its sole volume, when applicable.",
        {{"object_id", integer_id}, {"name", {{"type", "string"}, {"minLength", 1}}}},
        {"object_id", "name"}));
    tools.push_back(tool_definition("rename_volume",
        "Rename one volume within a multipart model object.",
        {{"object_id", integer_id}, {"volume_id", integer_id},
         {"name", {{"type", "string"}, {"minLength", 1}}}},
        {"object_id", "volume_id", "name"}));
    tools.push_back(tool_definition("delete_instance",
        "Delete one instance while preserving the model object and its other instances. Requires confirm=true.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"confirm", {{"type", "boolean"}, {"default", false}}}},
        {"object_id", "instance_id", "confirm"}));
    tools.push_back(tool_definition("delete_volume",
        "Delete one volume while preserving its multipart model object. Requires confirm=true.",
        {{"object_id", integer_id}, {"volume_id", integer_id},
         {"confirm", {{"type", "boolean"}, {"default", false}}}},
        {"object_id", "volume_id", "confirm"}));
    tools.push_back(tool_definition("set_object_extruder",
        "Assign a model object to a one-based project filament/extruder index. Before a print slice, ask which physical QIDI Box slot or external spool the user wants, assign its matching project filament here, then reslice.",
        {{"object_id", integer_id},
         {"extruder", {{"type", "integer"}, {"minimum", 1}}}}, {"object_id", "extruder"}));
    tools.push_back(tool_definition("set_volume_extruder",
        "Assign one multipart-model volume to a one-based project filament/extruder index. Before a print slice, ask which physical QIDI Box slot or external spool the user wants, assign its matching project filament here, then reslice.",
        {{"object_id", integer_id}, {"volume_id", integer_id},
         {"extruder", {{"type", "integer"}, {"minimum", 1}}}},
        {"object_id", "volume_id", "extruder"}));
    tools.push_back(tool_definition("set_volume_type",
        "Change a model volume between normal, negative, modifier, support-blocker, and support-enforcer roles.",
        {{"object_id", integer_id}, {"volume_id", integer_id},
         {"type", {{"type", "string"},
                   {"enum", json::array({"normal_part", "negative_part", "modifier_part", "support_blocker", "support_enforcer"})}}}},
        {"object_id", "volume_id", "type"}));
    tools.push_back(tool_definition("delete_object",
        "Delete a complete non-cut model object and refresh the plate.",
        {{"object_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("arrange_objects",
        "Start QIDI Studio's automatic plate arrangement job."));
    tools.push_back(tool_definition("auto_orient",
        "Start QIDI Studio's automatic orientation job."));
    tools.push_back(tool_definition("slice_plate",
        "Start slicing the active plate. For a print workflow, first ask the user which physical filament source to use and assign the matching project filament/extruder; prepare_print_job will reject a slice made with a different filament."));
    tools.push_back(tool_definition("get_slice_status",
        "Return active-plate slicing progress, result readiness, and broader QIDI Studio job/background activity state."));
    tools.push_back(tool_definition("get_slice_result",
        "Return valid slice timing, filament, weight, cost, volume, and warning data."));
    tools.push_back(tool_definition("export_object_stl",
        "Export one transformed object instance directly to a binary STL path without changing the project.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"path", {{"type", "string"}, {"minLength", 1}}}},
        {"object_id", "path"}));
    tools.push_back(tool_definition("export_gcode",
        "Schedule G-code export to an explicit local path.",
        {{"path", {{"type", "string"}, {"minLength", 1}}}}, {"path"}));
    tools.push_back(tool_definition("get_project_state", "Return project identity, dirty state, object count, and plate count."));
    tools.push_back(tool_definition("new_project", "Create an empty project only when the current project and presets are clean.",
        {{"name", {{"type", "string"}}}}));
    tools.push_back(tool_definition("load_project", "Load a project file only when the current project and presets are clean.",
        {{"path", {{"type", "string"}, {"minLength", 1}}}}, {"path"}));
    tools.push_back(tool_definition("export_project_3mf", "Export the current project to an explicit 3MF path without changing project identity.",
        {{"path", {{"type", "string"}, {"minLength", 1}}}}, {"path"}));
    tools.push_back(tool_definition("list_plates", "List all project plates and their current slice state."));
    tools.push_back(tool_definition("add_plate", "Add and select a new project plate."));
    tools.push_back(tool_definition("select_plate", "Select a project plate by zero-based index.",
        {{"plate_index", integer_id}}, {"plate_index"}));
    tools.push_back(tool_definition("rename_plate", "Rename a project plate.",
        {{"plate_index", integer_id}, {"name", {{"type", "string"}, {"minLength", 1}}}}, {"plate_index", "name"}));
    tools.push_back(tool_definition("delete_plate", "Delete a project plate. Requires confirm=true.",
        {{"plate_index", integer_id}, {"confirm", {{"type", "boolean"}, {"default", false}}}}, {"plate_index", "confirm"}));
    tools.push_back(tool_definition("list_slice_settings", "Discover setting keys and current serialized values with filtering and pagination.",
        {{"scope", scope}, {"query", {{"type", "string"}}},
         {"offset", {{"type", "integer"}, {"minimum", 0}, {"default", 0}}},
         {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 100}}}}));
    tools.push_back(tool_definition("set_object_printable", "Set every instance of a model object printable or unprintable.",
        {{"object_id", integer_id}, {"printable", {{"type", "boolean"}}}}, {"object_id", "printable"}));
    tools.push_back(tool_definition("set_instance_printable", "Set one object instance printable or unprintable without changing its sibling instances.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"printable", {{"type", "boolean"}}}}, {"object_id", "instance_id", "printable"}));
    tools.push_back(tool_definition("open_send_to_printer", "Open QIDI Studio's send-to-printer flow for the print-ready active plate."));
    tools.push_back(tool_definition("control_printer", "Pause, resume, or cancel an online printer. Cancel requires confirm=true.",
        {{"device_id", {{"type", "string"}, {"minLength", 1}}},
         {"action", {{"type", "string"}, {"enum", json::array({"pause", "resume", "cancel"})}}},
         {"confirm", {{"type", "boolean"}, {"default", false}}}}, {"device_id", "action"}));
    tools.push_back(tool_definition("get_suite_capabilities",
        "Describe the suite's native, computed, agent-orchestrated, unavailable, and safety-gated capabilities."));
    tools.push_back(tool_definition("get_slicing_warnings",
        "Return current native print-object, print-step, and final G-code warnings, including floating-region diagnostics.",
        {{"include_stale", {{"type", "boolean"}, {"default", false}}}}));
    tools.push_back(tool_definition("get_machine_capabilities",
        "Return the active machine preset, exact build volume, nozzle configuration, motion limits, and chamber capability."));
    tools.push_back(tool_definition("get_nozzle_capabilities",
        "Return active-preset nozzle diameter/type/layer limits and optionally compare them with a physical printer report.",
        {{"device_id", {{"type", "string"}}}}));
    tools.push_back(tool_definition("list_project_filaments",
        "Return engineering-relevant settings for every filament slot assigned to the project."));
    tools.push_back(tool_definition("get_filament_capabilities",
        "Return engineering-relevant settings for one zero-based project filament slot.",
        {{"filament_index", integer_id}}, {"filament_index"}));
    tools.push_back(tool_definition("compare_filament_profiles",
        "Return directly comparable temperature, flow, density, cost, retraction, and nozzle-requirement data for selected project filaments.",
        {{"filament_indices", {{"type", "array"}, {"items", integer_id}}}}));
    tools.push_back(tool_definition("validate_active_configuration",
        "Run QIDI's native full print-configuration validator and return every invalid setting."));
    tools.push_back(tool_definition("preview_profile_changes",
        "Validate and normalize serialized setting changes against a base preset without mutating QIDI Studio.",
        {{"scope", scope}, {"base_name", {{"type", "string"}}},
         {"values", {{"type", "object"}, {"minProperties", 1},
                     {"additionalProperties", {{"type", "string"}}}}}}, {"scope", "values"}));
    tools.push_back(tool_definition("create_profile_variant",
        "Clone a print, filament, or printer preset; apply validated serialized values; and save it under a new name.",
        {{"scope", scope}, {"name", {{"type", "string"}, {"minLength", 1}}},
         {"base_name", {{"type", "string"}}},
         {"values", {{"type", "object"}, {"additionalProperties", {{"type", "string"}}}}},
         {"overwrite", {{"type", "boolean"}, {"default", false}}},
         {"save_to_project", {{"type", "boolean"}, {"default", false}}}}, {"scope", "name"}));
    tools.push_back(tool_definition("measure_model",
        "Measure one transformed object instance: exact bounds, mesh volume, facets, parts, and average edge length.",
        {{"object_id", integer_id}, {"instance_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("compare_model_to_build_volume",
        "Run QIDI's exact build-volume test for one transformed object instance and report remaining span.",
        {{"object_id", integer_id}, {"instance_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("analyze_overhangs",
        "Estimate downward-facing and severe-overhang surface area from transformed mesh facets, with bounded sampling for dense meshes.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"angle_deg", {{"type", "number"}, {"minimum", 0}, {"maximum", 90}, {"default", 45}}},
         {"max_sample_facets", {{"type", "integer"}, {"minimum", 1}, {"maximum", 2000000}, {"default", 250000}}}},
        {"object_id"}));
    tools.push_back(tool_definition("analyze_bed_contact",
        "Estimate transformed mesh contact area at its lowest Z and compare it with its bounding footprint.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"tolerance_mm", {{"type", "number"}, {"exclusiveMinimum", 0}, {"maximum", 5}, {"default", 0.1}}},
         {"max_sample_facets", {{"type", "integer"}, {"minimum", 1}, {"maximum", 2000000}, {"default", 500000}}}},
        {"object_id"}));
    tools.push_back(tool_definition("analyze_printability",
        "Consolidate mesh topology, build-volume state, configuration validity, bed contact, and overhang risk for one instance.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"overhang_angle_deg", {{"type", "number"}, {"minimum", 0}, {"maximum", 90}, {"default", 45}}}},
        {"object_id"}));
    tools.push_back(tool_definition("generate_orientation_candidates",
        "Evaluate and rank six axis-aligned orientations for build fit, Z height, bed contact, and overhang area without changing the model.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"priority", {{"type", "string"}, {"enum", json::array({"balanced", "speed", "support", "adhesion"})},
                       {"default", "balanced"}}}}, {"object_id"}));
    tools.push_back(tool_definition("apply_orientation_candidate",
        "Apply one generated axis-aligned orientation, drop it to the bed, and create an undo snapshot.",
        {{"object_id", integer_id}, {"instance_id", integer_id},
         {"candidate_id", {{"type", "string"},
                           {"enum", json::array({"z_up", "z_down", "x_up", "x_down", "y_up", "y_down"})}}}},
        {"object_id", "candidate_id"}));
    tools.push_back(tool_definition("analyze_object_relationship",
        "Report transformed object bounds, axis overlap, clearance, and AABB intersection; does not claim surface clearance.",
        {{"first_object_id", integer_id}, {"first_instance_id", integer_id},
         {"second_object_id", integer_id}, {"second_instance_id", integer_id}},
        {"first_object_id", "second_object_id"}));
    tools.push_back(tool_definition("calculate_fit_scaling",
        "Calculate coarse uniform or axis-specific outer-bounding-box scaling to fit one object within another, without mutation.",
        {{"subject_object_id", integer_id}, {"subject_instance_id", integer_id},
         {"container_object_id", integer_id}, {"container_instance_id", integer_id},
         {"clearance_mm", {{"type", "number"}, {"default", 0}}},
         {"uniform", {{"type", "boolean"}, {"default", false}}}},
        {"subject_object_id", "container_object_id"}));
    tools.push_back(tool_definition("align_objects",
        "Align one object's transformed min, center, or max bounds to another on selected axes with an undo snapshot.",
        {{"moving_object_id", integer_id}, {"moving_instance_id", integer_id},
         {"reference_object_id", integer_id}, {"reference_instance_id", integer_id},
         {"anchor", {{"type", "string"}, {"enum", json::array({"center", "min", "max"})}, {"default", "center"}}},
         {"axes", {{"type", "array"}, {"items", {{"type", "string"}, {"enum", json::array({"x", "y", "z"})}}}}}},
        {"moving_object_id", "reference_object_id"}));
    tools.push_back(tool_definition("scale_object_to_fit",
        "Apply coarse uniform or axis-specific outer-bounding-box fit scaling with an undo snapshot; not anatomical surface fitting.",
        {{"subject_object_id", integer_id}, {"subject_instance_id", integer_id},
         {"container_object_id", integer_id}, {"container_instance_id", integer_id},
         {"clearance_mm", {{"type", "number"}, {"default", 0}}},
         {"uniform", {{"type", "boolean"}, {"default", false}}}},
        {"subject_object_id", "container_object_id"}));
    tools.push_back(tool_definition("preview_mesh_repair",
        "Return exact pre-repair topology and disclose native repair side effects without changing the object.",
        {{"object_id", integer_id}}, {"object_id"}));
    tools.push_back(tool_definition("split_object_to_parts",
        "Use QIDI's native split-to-objects operation. Replaces the source and requires confirm=true.",
        {{"object_id", integer_id}, {"confirm", {{"type", "boolean"}, {"default", false}}}},
        {"object_id", "confirm"}));
    tools.push_back(tool_definition("merge_object_volumes",
        "Use QIDI's native transformed-volume merge. Replaces selected volumes and requires confirm=true.",
        {{"object_id", integer_id},
         {"volume_ids", {{"type", "array"}, {"minItems", 2}, {"uniqueItems", true}, {"items", integer_id}}},
         {"confirm", {{"type", "boolean"}, {"default", false}}}},
        {"object_id", "volume_ids", "confirm"}));
    tools.push_back(tool_definition("inspect_toolpath",
        "Aggregate valid G-code moves into travel, extrusion, retraction, flow, speed, role, bounds, and collision-check metrics."));
    tools.push_back(tool_definition("get_layer_summary",
        "Return paginated layer Z positions and estimated layer times for a valid slice.",
        {{"start_layer", {{"type", "integer"}, {"minimum", 0}, {"default", 0}}},
         {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 100}}}}));
    tools.push_back(tool_definition("analyze_first_layer",
        "Inspect the valid first-layer toolpath for Z, time, bounds, extrusion, line width, fan, temperature, and flow."));
    tools.push_back(tool_definition("get_printer_details",
        "Return detailed physical-printer status including nozzle report, temperatures, fans, explicitly labeled QIDI Box slots 0-15 and external slot 16, the currently loaded physical slot, and camera metadata.",
        {{"device_id", {{"type", "string"}, {"minLength", 1}}}}, {"device_id"}));
    tools.push_back(capture_viewer_tool(tool_definition("capture_printer_camera",
        "Turn on the printer case light when needed, wait for exposure, and return the current camera image in the QIDI capture viewer with a short-lived local download fallback. Display the image inline; if MCP Apps are unavailable, materialize the image content as a temporary image attachment. The light is left on.",
        {{"device_id", {{"type", "string"}, {"minLength", 1}}},
         {"light_warmup_ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5000}, {"default", 1200}}}},
        {"device_id"})));
    tools.push_back(tool_definition("set_printer_case_light",
        "Request the printer case light on or off and report whether device telemetry confirms the requested state.",
        {{"device_id", {{"type", "string"}, {"minLength", 1}}},
         {"on", {{"type", "boolean"}}},
         {"confirm_wait_ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5000}, {"default", 750}}}},
        {"device_id", "on"}));
    tools.push_back(capture_viewer_tool(tool_definition("capture_print_monitor_snapshot",
        "Return one camera frame in the QIDI capture viewer with matching print state, progress, layers, temperatures, fans, and light telemetry. Display the frame inline; if MCP Apps are unavailable, materialize the image content as a temporary image attachment. Observational only; it never pauses or cancels a print.",
        {{"device_id", {{"type", "string"}, {"minLength", 1}}},
         {"light_warmup_ms", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5000}, {"default", 1200}}}},
        {"device_id"})));
    tools.push_back(tool_definition("check_printer_readiness",
        "Check observable online/busy state and list physical readiness items the MCP cannot verify.",
        {{"device_id", {{"type", "string"}, {"minLength", 1}}}}, {"device_id"}));
    tools.push_back(tool_definition("check_filament_quantity",
        "Compare user-supplied spool weights with per-filament weight calculated from the valid slice.",
        {{"available_g", {{"type", "object"}, {"minProperties", 1},
                          {"additionalProperties", {{"type", "number"}, {"minimum", 0}}}}}}, {"available_g"}));
    tools.push_back(tool_definition("get_calibration_recommendations",
        "Generate an ordered calibration plan from the active filament profile and whether the filament or nozzle is new.",
        {{"filament_index", integer_id},
         {"new_filament", {{"type", "boolean"}, {"default", true}}},
         {"changed_nozzle", {{"type", "boolean"}, {"default", false}}}}));
    tools.push_back(tool_definition("run_print_preflight",
        "Run a consolidated model, build-volume, configuration, slice, G-code, sequential-clearance, and optional printer-readiness gate.",
        {{"device_id", {{"type", "string"}}}}));
    tools.push_back(tool_definition("prepare_print_job",
        "Lock a single-filament print-ready plate, explicit physical QIDI Box/external source, target printer, native options, preflight, and slice fingerprint into a short-lived single-use token. The slice must already use project_filament_index; otherwise assign that filament and reslice. This does not upload or print.",
        {{"device_id", {{"type", "string"}, {"minLength", 1}}},
         {"filament_source", {{"type", "object"},
                              {"properties", {{"project_filament_index", integer_id},
                                              {"source", {{"type", "string"},
                                                          {"enum", json::array({"qidi_box", "external"})}}},
                                              {"slot_id", {{"type", "integer"}, {"minimum", 0}, {"maximum", 16}}}}},
                              {"required", json::array({"project_filament_index", "source"})},
                              {"additionalProperties", false}}},
         {"bed_leveling", {{"type", "boolean"}, {"default", false}}},
         {"timelapse", {{"type", "boolean"}, {"default", false}}},
         {"expires_in_seconds", {{"type", "integer"}, {"minimum", 60}, {"maximum", 1800}, {"default", 600}}}},
        {"device_id", "filament_source"}));
    tools.push_back(tool_definition("start_print_job",
        "Consume a prepared confirmation token, apply its locked physical QIDI Box/external mapping, and use QIDI's native local/LAN .gcode.3mf upload with StartPrint. Requires confirm=true and returns accepted before printing is confirmed.",
        {{"confirmation_token", {{"type", "string"}, {"minLength", 1}}},
         {"confirm", {{"type", "boolean"}, {"default", false}}}},
        {"confirmation_token", "confirm"}));
    tools.push_back(tool_definition("get_print_job_status",
        "Report native packaging/upload state and confirm printer acceptance or printing only when telemetry reports both the matching filename and locked physical filament slot. A prior toolhead slot remains pending during layer-zero preparation; cancel is recommended only if the first layer begins with a mismatched slot.",
        {{"job_id", {{"type", "string"}, {"minLength", 1}}}}, {"job_id"}));
    return {{"tools", std::move(tools)}};
}

json handle_rpc(QDSDeviceManager* manager, const json& request)
{
    const json id = request.contains("id") ? request["id"] : json(nullptr);
    if (!request.is_object() || request.value("jsonrpc", "") != "2.0" ||
        !request.contains("method") || !request["method"].is_string())
        return rpc_error(id, -32600, "Invalid Request");

    const std::string method = request["method"].get<std::string>();
    if (method == "initialize") {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {
                {"protocolVersion", "2025-06-18"},
                {"capabilities", {
                    {"tools", {{"listChanged", false}}},
                    {"resources", {{"subscribe", false}, {"listChanged", false}}}
                }},
                {"serverInfo", {{"name", "qidi-studio"}, {"version", "1.11.0"}}}
            }}
        };
    }
    if (method == "ping")
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}};
    if (method == "tools/list")
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", tools_list()}};
    if (method == "resources/list") {
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {{"resources", json::array({{
                {"uri", CAPTURE_VIEWER_URI},
                {"name", "QIDI Studio Capture Viewer"},
                {"description", "Displays QIDI Studio and printer-camera captures inside an MCP Apps host."},
                {"mimeType", "text/html;profile=mcp-app"}
            }})}}}
        };
    }
    if (method == "resources/read") {
        const json params = request.value("params", json::object());
        if (!params.is_object() || params.value("uri", "") != CAPTURE_VIEWER_URI)
            return rpc_error(id, -32602, "Unknown resource URI");
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {{"contents", json::array({{
                {"uri", CAPTURE_VIEWER_URI},
                {"mimeType", "text/html;profile=mcp-app"},
                {"text", capture_viewer_html()},
                {"_meta", {
                    {"ui", {
                        {"prefersBorder", true},
                        {"domain", "https://sslinkyy.github.io"},
                        {"csp", {
                            {"connectDomains", json::array()},
                            {"resourceDomains", json::array()}
                        }}
                    }},
                    {"openai/widgetDescription", "Displays the image returned by a QIDI Studio capture tool."}
                }}
            }})}}}
        };
    }
    if (method == "tools/call") {
        const json params = request.value("params", json::object());

        if (!params.is_object())
            return rpc_error(id, -32602, "Invalid parameters");

        const std::string tool_name = params.value("name", "");
        const json arguments = params.value("arguments", json::object());
        if (!arguments.is_object())
            return rpc_error(id, -32602, "Tool arguments must be an object");

        json payload;

        if (tool_name == "list_printers")
            payload = list_printers(manager);
        else if (tool_name == "get_tunnel_status")
            payload = get_tunnel_status();
        else if (tool_name == "get_recovery_state")
            payload = get_recovery_state();
        else if (tool_name == "resolve_project_recovery")
            payload = resolve_project_recovery(arguments);
        else if (tool_name == "get_ui_state")
            payload = get_ui_state();
        else if (tool_name == "capture_studio_screenshot")
            payload = capture_studio_screenshot(arguments);
        else if (tool_name == "get_plate_state")
            payload = get_plate_state();
        else if (tool_name == "get_print_object_sequence")
            payload = get_print_object_sequence();
        else if (tool_name == "set_print_object_sequence")
            payload = set_print_object_sequence(arguments);
        else if (tool_name == "validate_print_by_object")
            payload = validate_print_by_object();
        else if (tool_name == "list_objects")
            payload = list_objects();
        else if (tool_name == "get_object_state")
            payload = get_object_state(arguments);
        else if (tool_name == "get_model_diagnostics")
            payload = get_model_diagnostics(arguments);
        else if (tool_name == "get_slicing_warnings")
            payload = get_slicing_warnings(arguments);
        else if (tool_name == "list_object_volumes")
            payload = list_object_volumes(arguments);
        else if (tool_name == "repair_object_mesh")
            payload = repair_object_mesh(arguments);
        else if (tool_name == "center_object")
            payload = center_object(arguments);
        else if (tool_name == "drop_object_to_bed")
            payload = drop_object_to_bed(arguments);
        else if (tool_name == "mirror_object")
            payload = mirror_object(arguments);
        else if (tool_name == "cut_object_horizontal")
            payload = cut_object_horizontal(arguments);
        else if (tool_name == "undo")
            payload = undo_project_change();
        else if (tool_name == "redo")
            payload = redo_project_change();
        else if (tool_name == "get_object_settings")
            payload = get_object_settings(arguments);
        else if (tool_name == "set_object_settings")
            payload = set_object_settings(arguments);
        else if (tool_name == "reset_object_settings")
            payload = reset_object_settings(arguments);
        else if (tool_name == "get_volume_settings")
            payload = get_volume_settings(arguments);
        else if (tool_name == "set_volume_settings")
            payload = set_volume_settings(arguments);
        else if (tool_name == "reset_volume_settings")
            payload = reset_volume_settings(arguments);
        else if (tool_name == "list_setting_definitions")
            payload = list_setting_definitions(arguments);
        else if (tool_name == "preview_settings_update")
            payload = preview_settings_update(arguments);
        else if (tool_name == "get_layer_height_profile")
            payload = get_layer_height_profile(arguments);
        else if (tool_name == "preview_adaptive_layer_height")
            payload = preview_adaptive_layer_height(arguments);
        else if (tool_name == "apply_adaptive_layer_height")
            payload = apply_adaptive_layer_height(arguments);
        else if (tool_name == "smooth_layer_height_profile")
            payload = smooth_layer_height_profile(arguments);
        else if (tool_name == "reset_layer_height_profile")
            payload = reset_layer_height_profile(arguments);
        else if (tool_name == "preview_surface_selection")
            payload = preview_surface_selection(arguments);
        else if (tool_name == "get_support_paint_state")
            payload = get_support_paint_state(arguments);
        else if (tool_name == "set_support_paint")
            payload = set_support_paint(arguments);
        else if (tool_name == "get_seam_paint_state")
            payload = get_seam_paint_state(arguments);
        else if (tool_name == "set_seam_paint")
            payload = set_seam_paint(arguments);
        else if (tool_name == "import_attached_models")
            payload = import_attached_models(arguments);
        else if (tool_name == "import_model")
            payload = import_model(arguments);
        else if (tool_name == "list_presets")
            payload = list_presets(arguments);
        else if (tool_name == "get_active_presets")
            payload = get_active_presets();
        else if (tool_name == "select_presets")
            payload = select_presets(arguments);
        else if (tool_name == "set_project_filament")
            payload = set_project_filament(arguments);
        else if (tool_name == "get_slice_settings")
            payload = get_slice_settings(arguments);
        else if (tool_name == "set_slice_settings")
            payload = set_slice_settings(arguments);
        else if (tool_name == "reset_slice_settings")
            payload = reset_slice_settings(arguments);
        else if (tool_name == "save_preset_as")
            payload = save_preset_as(arguments);
        else if (tool_name == "transform_object")
            payload = transform_object(arguments);
        else if (tool_name == "duplicate_object")
            payload = duplicate_object(arguments);
        else if (tool_name == "rename_object")
            payload = rename_object(arguments);
        else if (tool_name == "rename_volume")
            payload = rename_volume(arguments);
        else if (tool_name == "delete_instance")
            payload = delete_instance(arguments);
        else if (tool_name == "delete_volume")
            payload = delete_volume(arguments);
        else if (tool_name == "set_object_extruder")
            payload = set_object_extruder(arguments);
        else if (tool_name == "set_volume_extruder")
            payload = set_volume_extruder(arguments);
        else if (tool_name == "set_volume_type")
            payload = set_volume_type(arguments);
        else if (tool_name == "delete_object")
            payload = delete_object(arguments);
        else if (tool_name == "arrange_objects")
            payload = start_ui_job(false);
        else if (tool_name == "auto_orient")
            payload = start_ui_job(true);
        else if (tool_name == "slice_plate")
            payload = slice_plate();
        else if (tool_name == "get_slice_status")
            payload = get_slice_status();
        else if (tool_name == "get_slice_result")
            payload = get_slice_result();
        else if (tool_name == "export_object_stl")
            payload = export_object_stl(arguments);
        else if (tool_name == "export_gcode")
            payload = export_gcode(arguments);
        else if (tool_name == "get_project_state") payload = get_project_state();
        else if (tool_name == "new_project") payload = new_project(arguments);
        else if (tool_name == "load_project") payload = load_project(arguments);
        else if (tool_name == "export_project_3mf") payload = export_project_3mf(arguments);
        else if (tool_name == "list_plates") payload = list_plates();
        else if (tool_name == "add_plate") payload = add_plate();
        else if (tool_name == "select_plate") payload = select_plate(arguments);
        else if (tool_name == "rename_plate") payload = rename_plate(arguments);
        else if (tool_name == "delete_plate") payload = delete_plate(arguments);
        else if (tool_name == "list_slice_settings") payload = list_slice_settings(arguments);
        else if (tool_name == "set_object_printable") payload = set_object_printable(arguments);
        else if (tool_name == "set_instance_printable") payload = set_instance_printable(arguments);
        else if (tool_name == "open_send_to_printer") payload = open_send_to_printer();
        else if (tool_name == "control_printer") payload = control_printer(manager, arguments);
        else if (tool_name == "get_suite_capabilities") payload = get_suite_capabilities();
        else if (tool_name == "get_machine_capabilities") payload = get_machine_capabilities();
        else if (tool_name == "get_nozzle_capabilities") payload = get_nozzle_capabilities(manager, arguments);
        else if (tool_name == "list_project_filaments") payload = list_project_filaments();
        else if (tool_name == "get_filament_capabilities") payload = get_filament_capabilities(arguments);
        else if (tool_name == "compare_filament_profiles") payload = compare_filament_profiles(arguments);
        else if (tool_name == "validate_active_configuration") payload = validate_active_configuration();
        else if (tool_name == "preview_profile_changes") payload = preview_profile_changes(arguments);
        else if (tool_name == "create_profile_variant") payload = create_profile_variant(arguments);
        else if (tool_name == "measure_model") payload = measure_model(arguments);
        else if (tool_name == "compare_model_to_build_volume") payload = compare_model_to_build_volume(arguments);
        else if (tool_name == "analyze_overhangs") payload = analyze_overhangs(arguments);
        else if (tool_name == "analyze_bed_contact") payload = analyze_bed_contact(arguments);
        else if (tool_name == "analyze_printability") payload = analyze_printability(arguments);
        else if (tool_name == "generate_orientation_candidates") payload = generate_orientation_candidates(arguments);
        else if (tool_name == "apply_orientation_candidate") payload = apply_orientation_candidate(arguments);
        else if (tool_name == "analyze_object_relationship") payload = analyze_object_relationship(arguments);
        else if (tool_name == "calculate_fit_scaling") payload = calculate_fit_scaling(arguments);
        else if (tool_name == "align_objects") payload = align_objects(arguments);
        else if (tool_name == "scale_object_to_fit") payload = scale_object_to_fit(arguments);
        else if (tool_name == "preview_mesh_repair") payload = preview_mesh_repair(arguments);
        else if (tool_name == "split_object_to_parts") payload = split_object_to_parts(arguments);
        else if (tool_name == "merge_object_volumes") payload = merge_object_volumes(arguments);
        else if (tool_name == "inspect_toolpath") payload = inspect_toolpath();
        else if (tool_name == "get_layer_summary") payload = get_layer_summary(arguments);
        else if (tool_name == "analyze_first_layer") payload = analyze_first_layer();
        else if (tool_name == "get_printer_details") payload = get_printer_details(manager, arguments);
        else if (tool_name == "capture_printer_camera") payload = capture_printer_camera(manager, arguments);
        else if (tool_name == "set_printer_case_light") payload = set_printer_case_light(manager, arguments);
        else if (tool_name == "capture_print_monitor_snapshot") payload = capture_print_monitor_snapshot(manager, arguments);
        else if (tool_name == "check_printer_readiness") payload = check_printer_readiness(manager, arguments);
        else if (tool_name == "check_filament_quantity") payload = check_filament_quantity(arguments);
        else if (tool_name == "get_calibration_recommendations") payload = get_calibration_recommendations(arguments);
        else if (tool_name == "run_print_preflight") payload = run_print_preflight(manager, arguments);
        else if (tool_name == "prepare_print_job") payload = prepare_print_job(manager, arguments);
        else if (tool_name == "start_print_job") payload = start_print_job(manager, arguments);
        else if (tool_name == "get_print_job_status") payload = get_print_job_status(manager, arguments);
        else
            return rpc_error(id, -32602, "Unknown tool");

        json content = json::array();
        if (payload.contains("_mcp_image")) {
            json image = std::move(payload["_mcp_image"]);
            payload.erase("_mcp_image");
            content.push_back({{"type", "text"}, {"text", payload.dump(2)}});
            content.push_back({{"type", "image"},
                               {"data", std::move(image["data"])},
                               {"mimeType", std::move(image["mimeType"])}});
        } else {
            content.push_back({{"type", "text"}, {"text", payload.dump(2)}});
        }
        return {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {
                {"content", std::move(content)},
                {"structuredContent", payload},
                {"isError", payload.contains("error")}
            }}
        };
    }
    return rpc_error(id, -32601, "Method not found");
}

} // namespace

struct QidiMcpServer::Impl
{
    explicit Impl(QDSDeviceManager* manager)
        : device_manager(manager), acceptor(io_context)
    {}

    bool start()
    {
        if (thread.joinable())
            return true;

        boost::system::error_code ec;
        const tcp::endpoint endpoint(boost::asio::ip::address_v4::loopback(), MCP_PORT);
        acceptor.open(endpoint.protocol(), ec);
        if (!ec)
            acceptor.set_option(tcp::acceptor::reuse_address(true), ec);
        if (!ec)
            acceptor.bind(endpoint, ec);
        if (!ec)
            acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (!ec)
            acceptor.non_blocking(true, ec);
        if (ec) {
            BOOST_LOG_TRIVIAL(error) << "QIDI MCP failed to bind 127.0.0.1:"
                                     << MCP_PORT << ": " << ec.message();
            boost::system::error_code ignored;
            acceptor.close(ignored);
            return false;
        }

        stopping = false;
        thread = std::thread([this] { run(); });
        BOOST_LOG_TRIVIAL(info) << "QIDI MCP listening on http://127.0.0.1:"
                                << MCP_PORT << "/mcp";
        return true;
    }

    void stop()
    {
        stopping = true;
        boost::system::error_code ignored;
        acceptor.close(ignored);
        if (thread.joinable())
            thread.join();
    }

    void run()
    {
        while (!stopping) {
            tcp::socket socket(io_context);
            boost::system::error_code ec;
            acceptor.accept(socket, ec);
            if (ec) {
                if (stopping || ec == boost::asio::error::operation_aborted ||
                    ec == boost::asio::error::bad_descriptor)
                    break;
                if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }
                BOOST_LOG_TRIVIAL(warning) << "QIDI MCP accept failed: " << ec.message();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            serve(socket);
        }
    }

    void serve(tcp::socket& socket)
    {
        HttpRequest request;
        if (!read_http_request(socket, stopping, request)) {
            if (!stopping)
                write_http_response(socket, 400, "{\"error\":\"invalid HTTP request\"}");
            return;
        }
        if (request.method == "OPTIONS") {
            write_http_response(socket, 204, "");
            return;
        }
        if (request.method == "GET" && request.target.rfind("/captures/", 0) == 0) {
            DownloadableCapture capture;
            if (!find_capture_download(request.target, capture))
                write_http_response(socket, 404, "{\"error\":\"capture not found or expired\"}");
            else
                write_capture_download(socket, capture);
            return;
        }
        if (request.target != "/mcp") {
            write_http_response(socket, 404, "{\"error\":\"not found\"}");
            return;
        }
        if (request.method != "POST") {
            write_http_response(socket, 405, "{\"error\":\"POST required\"}");
            return;
        }

        json rpc;
        try {
            rpc = json::parse(request.body);
        } catch (const std::exception&) {
            write_http_response(socket, 200, rpc_error(nullptr, -32700, "Parse error").dump());
            return;
        }

        const bool notification = rpc.is_object() && !rpc.contains("id");
        const json response = handle_rpc(device_manager, rpc);
        if (notification)
            write_http_response(socket, 202, "");
        else
            write_http_response(socket, 200, response.dump());
    }

    QDSDeviceManager* device_manager{nullptr};
    boost::asio::io_context io_context;
    tcp::acceptor acceptor;
    std::atomic<bool> stopping{false};
    std::thread thread;
};

QidiMcpServer::QidiMcpServer(QDSDeviceManager* device_manager)
    : m_impl(std::make_unique<Impl>(device_manager))
{}

QidiMcpServer::~QidiMcpServer()
{
    stop();
}

bool QidiMcpServer::start()
{
    return m_impl->start();
}

void QidiMcpServer::stop()
{
    if (m_impl)
        m_impl->stop();
}

} // namespace Slic3r::GUI
