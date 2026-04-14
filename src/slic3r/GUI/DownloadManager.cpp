#include "DownloadManager.hpp"

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <ctime>
#include <random>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <openssl/md5.h>

#include <wx/app.h>

#include "slic3r/Utils/Http.hpp"
#include "GUI_App.hpp"

namespace Slic3r {
namespace GUI {

// ============================================================
//  Internal Utility Functions
// ============================================================

namespace {

/** Remove the query parameters (everything after '?') from a URL */
static std::string stripUrlQuery(const std::string& url)
{
    auto pos = url.find('?');
    return (pos != std::string::npos) ? url.substr(0, pos) : url;
}

/** Compute the MD5 hash of a byte array and return a 32-character lowercase hex string */
static std::string md5Hex(const std::string& input)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(input.data()),
        input.size(),
        digest);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return oss.str();
}

/** Generate a UUID v4-style string (used for task_id) */
static std::string generateUUID()
{
    std::random_device rd;
    std::mt19937_64    gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << dist(gen) << '-';
    oss << std::setw(4) << (dist(gen) & 0xFFFF) << '-';
    oss << std::setw(4) << ((dist(gen) & 0x0FFF) | 0x4000) << '-';
    oss << std::setw(4) << ((dist(gen) & 0x3FFF) | 0x8000) << '-';
    oss << std::setw(8) << dist(gen);
    oss << std::setw(4) << (dist(gen) & 0xFFFF);
    return oss.str();
}

/** Read all bytes from a local file */
static std::vector<uint8_t> readFileBytes(const std::string& path)
{
    boost::nowide::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(ifs),
                                std::istreambuf_iterator<char>());
}

} // anonymous namespace


// ============================================================
//  ThumbnailCache Implementation
// ============================================================

ThumbnailCache::ThumbnailCache(const std::string& cache_dir)
    : m_cache_dir(cache_dir)
{
    boost::filesystem::create_directories(m_cache_dir);
}

std::string ThumbnailCache::makeKey(const std::string& url)
{
    return md5Hex(stripUrlQuery(url));
}

std::string ThumbnailCache::keyToPath(const std::string& key) const
{
    // The first two characters are used as the subdirectory: {cache_dir}/{ab}/{abcdef...}
    std::string sub_dir = m_cache_dir + "/" + key.substr(0, 2);
    return sub_dir + "/" + key;
}

std::string ThumbnailCache::lookup(const std::string& url) const
{
    std::string key  = makeKey(url);
    std::string path = keyToPath(key);

    std::lock_guard<std::mutex> lock(m_mutex);
    if (boost::filesystem::exists(path)) {
        return path;
    }
    return {};
}

std::string ThumbnailCache::store(const std::string& url, const std::vector<uint8_t>& data)
{
    if (data.empty()) return {};

    std::string key     = makeKey(url);
    std::string path    = keyToPath(key);
    std::string sub_dir = m_cache_dir + "/" + key.substr(0, 2);

    std::lock_guard<std::mutex> lock(m_mutex);

    try {
        boost::filesystem::create_directories(sub_dir);
        boost::nowide::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            BOOST_LOG_TRIVIAL(warning) << "[ThumbnailCache] Failed to open for write: " << path;
            return {};
        }
        ofs.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        if (!ofs) {
            BOOST_LOG_TRIVIAL(warning) << "[ThumbnailCache] Write error: " << path;
            return {};
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "[ThumbnailCache] Exception: " << e.what();
        return {};
    }

    return path;
}


// ============================================================
//  ThumbnailDownloader Implementation
// ============================================================

ThumbnailDownloader::ThumbnailDownloader(std::shared_ptr<ThumbnailCache> cache,
                                         int thread_count)
    : m_cache(std::move(cache))
    , m_thread_count(thread_count)
{
    for (int i = 0; i < m_thread_count; ++i) {
        m_threads.emplace_back([this] { workerLoop(); });
    }
}

ThumbnailDownloader::~ThumbnailDownloader()
{
    shutdown();
}

void ThumbnailDownloader::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();
}

void ThumbnailDownloader::cancelAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::queue<Task> empty;
    m_queue.swap(empty);
}

void ThumbnailDownloader::download(const std::string& url,
                                   const std::string& file_name,
                                   ThumbnailCallback  callback)
{
    if (url.empty() || !callback) return;

    // First, check the cache in the main thread: if hit, directly callback through CallAfter, do not queue
    std::string cached = m_cache->lookup(url);
    if (!cached.empty()) {
        auto data = readFileBytes(cached);
        if (!data.empty()) {
            ThumbnailResult result;
            result.file_name   = file_name;
            result.url         = url;
            result.success     = true;
            result.png_data    = std::move(data);
            result.cache_path  = cached;
            dispatchResult(std::move(result), std::move(callback));
            return;
        }
        // Cache file is corrupted: continue with network download
    }

    Task task;
    task.url         = url;
    task.file_name   = file_name;
    task.callback    = std::move(callback);
    BOOST_LOG_TRIVIAL(trace) << url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(task));
    }
    m_cv.notify_one();
}

void ThumbnailDownloader::workerLoop()
{
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty()) break;
            task = std::move(m_queue.front());
            m_queue.pop();
        }

        ThumbnailResult result;
        result.file_name   = task.file_name;
        result.url         = task.url;

        // Double-check the cache (it may have been written by another thread while waiting in the queue)
        std::string cached = m_cache->lookup(task.url);
        if (!cached.empty()) {
            auto data = readFileBytes(cached);
            if (!data.empty()) {
                result.success    = true;
                result.png_data   = std::move(data);
                result.cache_path = cached;
                dispatchResult(std::move(result), std::move(task.callback));
                continue;
            }
        }

        // Make HTTP GET request
        bool ok = false;
        std::string body;
        std::string err_msg;

        Slic3r::Http::get(task.url)
            .timeout_max(10)
            .on_complete([&](std::string resp_body, unsigned /*status*/) {
                ok   = true;
                body = std::move(resp_body);
            })
            .on_error([&](std::string /*resp_body*/, std::string error, unsigned status) {
                err_msg = error + " (HTTP " + std::to_string(status) + ")";
                BOOST_LOG_TRIVIAL(warning)
                    << "[ThumbnailDownloader] Download failed: " << task.url
                    << " | " << err_msg;
            })
            .perform_sync();

        if (ok && !body.empty()) {
            std::vector<uint8_t> png_data(body.begin(), body.end());
            std::string cache_path = m_cache->store(task.url, png_data);

            result.success    = true;
            result.png_data   = std::move(png_data);
            result.cache_path = cache_path;
        } else {
            result.success   = false;
            result.error_msg = err_msg.empty() ? "Empty response" : err_msg;
        }

        dispatchResult(std::move(result), std::move(task.callback));
    }
}

void ThumbnailDownloader::dispatchResult(ThumbnailResult result, ThumbnailCallback cb)
{
    wxGetApp().CallAfter([r = std::move(result), callback = std::move(cb)]() mutable {
        callback(std::move(r));
    });
}


// ============================================================
//  FileDownloader Implementation
// ============================================================

FileDownloader::FileDownloader()
{
    m_thread = std::thread([this] { workerLoop(); });
}

FileDownloader::~FileDownloader()
{
    shutdown();
}

void FileDownloader::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

std::string FileDownloader::generateTaskId()
{
    return generateUUID();
}

std::string FileDownloader::download(const std::string&   url,
                                     const std::string&   local_path,
                                     const std::string&   file_name,
                                     FileDownloadCallback on_complete,
                                     FileProgressCallback on_progress)
{
    std::string task_id = generateTaskId();

    // Initialize the progress snapshot
    FileDownloadProgress snap;
    snap.task_id   = task_id;
    snap.file_name = file_name;
    snap.state     = FileDownloadProgress::State::Pending;
    {
        std::lock_guard<std::mutex> lock(m_progress_mutex);
        m_progress_map[task_id] = snap;
    }

    QueueItem item;
    item.task_id     = task_id;
    item.url         = url;
    item.local_path  = local_path;
    item.file_name   = file_name;
    item.on_complete = std::move(on_complete);
    item.on_progress = std::move(on_progress);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(item));
    }
    m_cv.notify_one();

    return task_id;
}

void FileDownloader::cancel(const std::string& task_id)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // If in the queue (Pending), remove it directly
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
        if (it->task_id == task_id) {
            // Update the progress snapshot to Cancelled
            {
                std::lock_guard<std::mutex> plock(m_progress_mutex);
                auto found = m_progress_map.find(task_id);
                if (found != m_progress_map.end()) {
                    found->second.state = FileDownloadProgress::State::Cancelled;
                }
            }
            m_queue.erase(it);
            return;
        }
    }

    // If it's the current download task, set the cancel flag
    if (m_current_task_id == task_id) {
        m_cancel_current = true;
    }
}

void FileDownloader::cancelAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Mark all pending tasks as cancelled
    {
        std::lock_guard<std::mutex> plock(m_progress_mutex);
        for (const auto& item : m_queue) {
            auto found = m_progress_map.find(item.task_id);
            if (found != m_progress_map.end()) {
                found->second.state = FileDownloadProgress::State::Cancelled;
            }
        }
    }

    // Clear queue
    m_queue.clear();

    // Cancel current downloading task (checked after sync request returns)
    if (!m_current_task_id.empty()) {
        m_cancel_current = true;
    }
}

std::optional<FileDownloadProgress> FileDownloader::getProgress(const std::string& task_id) const
{
    std::lock_guard<std::mutex> lock(m_progress_mutex);
    auto it = m_progress_map.find(task_id);
    if (it != m_progress_map.end()) {
        return it->second;
    }
    return std::nullopt;
}

void FileDownloader::workerLoop()
{
    while (true) {
        QueueItem item;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty()) break;
            item = std::move(m_queue.front());
            m_queue.pop_front();
            m_current_task_id = item.task_id;
            m_cancel_current  = false;
        }

        doDownload(item);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_current_task_id.clear();
        }
    }
}

void FileDownloader::doDownload(const QueueItem& item)
{
    // Update progress: Downloading
    {
        FileDownloadProgress snap;
        snap.task_id   = item.task_id;
        snap.file_name = item.file_name;
        snap.state     = FileDownloadProgress::State::Downloading;
        updateProgress(item.task_id, snap);
    }

    // Handle filename conflicts
    std::string actual_path = resolveConflict(item.local_path);

    // Ensure the directory exists
    try {
        boost::filesystem::create_directories(
            boost::filesystem::path(actual_path).parent_path());
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "[FileDownloader] create_directories failed: " << e.what();
    }

    bool        ok = false;
    std::string err_msg;
    std::string body;

    const std::string            task_id    = item.task_id;
    const std::string            file_name  = item.file_name;
    const FileProgressCallback   progressfn = item.on_progress;

    Slic3r::Http::get(item.url)
        .timeout_max(300)  // File downloads are allowed to take longer
        .on_progress([this, task_id, file_name, progressfn](Http::Progress hp, bool& cancel) {
            if (m_cancel_current.load()) {
                cancel = true;
                return;
            }
            if (!progressfn) {
                return;
            }

            FileDownloadProgress snap;
            snap.task_id          = task_id;
            snap.file_name        = file_name;
            snap.state            = FileDownloadProgress::State::Downloading;
            snap.bytes_downloaded = static_cast<int64_t>(hp.dlnow);
            snap.bytes_total      = hp.dltotal > 0 ? static_cast<int64_t>(hp.dltotal) : int64_t(-1);
            if (hp.dltotal > 0) {
                snap.progress = static_cast<float>(static_cast<double>(hp.dlnow)
                    / static_cast<double>(std::max<size_t>(hp.dltotal, 1)));
            }
            else {
                snap.progress = -1.0f;
            }

            updateProgress(task_id, snap);
            dispatchProgress(snap, progressfn);
        })
        .on_complete([&](std::string resp_body, unsigned /*status*/) {
            ok   = true;
            body = std::move(resp_body);
        })
        .on_error([&](std::string /*resp_body*/, std::string error, unsigned status) {
            err_msg = error + " (HTTP " + std::to_string(status) + ")";
            BOOST_LOG_TRIVIAL(error)
                << "[FileDownloader] Failed: " << item.file_name << " | " << err_msg;
        })
        .perform_sync();

    // Check if it has been cancelled
    if (m_cancel_current) {
        FileDownloadProgress snap;
        snap.task_id   = item.task_id;
        snap.file_name = item.file_name;
        snap.state     = FileDownloadProgress::State::Cancelled;
        updateProgress(item.task_id, snap);
        dispatchComplete(snap, item.on_complete);
        return;
    }

    if (ok && !body.empty()) {
        // Write the file
        try {
            boost::nowide::ofstream ofs(actual_path, std::ios::binary | std::ios::trunc);
            if (ofs) {
                ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
                ok = ofs.good();
                if (!ok) err_msg = "File write error: " + actual_path;
            } else {
                ok      = false;
                err_msg = "Cannot open file for write: " + actual_path;
            }
        } catch (const std::exception& e) {
            ok      = false;
            err_msg = std::string("Write exception: ") + e.what();
        }
    } else if (ok && body.empty()) {
        ok      = false;
        err_msg = "Empty response body";
    }

    // Build the final progress snapshot
    FileDownloadProgress snap;
    snap.task_id          = item.task_id;
    snap.file_name        = item.file_name;
    snap.bytes_downloaded = ok ? static_cast<int64_t>(body.size()) : 0;
    snap.bytes_total      = ok ? static_cast<int64_t>(body.size()) : -1;
    snap.progress         = ok ? 1.0f : 0.0f;
    snap.state            = ok ? FileDownloadProgress::State::Completed
                               : FileDownloadProgress::State::Failed;
    snap.error_msg        = err_msg;

    updateProgress(item.task_id, snap);
    dispatchComplete(snap, item.on_complete);

    if (ok) {
        BOOST_LOG_TRIVIAL(info)
            << "[FileDownloader] Completed: " << item.file_name
            << " -> " << actual_path;
    }
}

void FileDownloader::updateProgress(const std::string& task_id,
                                    const FileDownloadProgress& snap)
{
    std::lock_guard<std::mutex> lock(m_progress_mutex);
    m_progress_map[task_id] = snap;
}

void FileDownloader::dispatchProgress(FileDownloadProgress snap, FileProgressCallback cb)
{
    if (!cb) {
        return;
    }
    wxGetApp().CallAfter([snap = std::move(snap), cb = std::move(cb)]() mutable { cb(snap); });
}

void FileDownloader::dispatchComplete(FileDownloadProgress snap, FileDownloadCallback cb)
{
    if (!cb) return;
    wxGetApp().CallAfter([s = std::move(snap), callback = std::move(cb)]() mutable {
        callback(std::move(s));
    });
}

std::string FileDownloader::resolveConflict(const std::string& local_path)
{
    if (!boost::filesystem::exists(local_path)) {
        return local_path;
    }

    // Append timestamp suffix: filename_YYYYMMDD_HHMMSS.ext
    boost::filesystem::path p(local_path);
    std::string stem      = p.stem().string();
    std::string extension = p.extension().string();
    std::string parent    = p.parent_path().string();

    std::time_t now = std::time(nullptr);
    std::tm*    tm  = std::localtime(&now);
    char        buf[20];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", tm);

    std::string new_path = parent + "/" + stem + "_" + buf + extension;
    return new_path;
}


// ============================================================
//  DownloadManager Implementation
// ============================================================

DownloadManager& DownloadManager::getInstance()
{
    static DownloadManager instance;
    return instance;
}

void DownloadManager::init(const std::string& thumb_cache_dir)
{
    if (m_initialized) return;

    m_thumb_cache      = std::make_shared<ThumbnailCache>(thumb_cache_dir);
    m_thumb_downloader = std::make_unique<ThumbnailDownloader>(m_thumb_cache, 3);
    m_file_downloader  = std::make_unique<FileDownloader>();
    m_initialized      = true;

    BOOST_LOG_TRIVIAL(info)
        << "[DownloadManager] Initialized. Cache dir: " << thumb_cache_dir;
}

void DownloadManager::downloadThumbnail(const std::string& url,
                                        const std::string& file_name,
                                        ThumbnailCallback  callback)
{
    if (!m_initialized) {
        BOOST_LOG_TRIVIAL(warning) << "[DownloadManager] Not initialized, ignoring downloadThumbnail()";
        return;
    }
    m_thumb_downloader->download(url, file_name, std::move(callback));
}



std::string DownloadManager::downloadFile(const std::string&   url,
                                          const std::string&   local_path,
                                          const std::string&   file_name,
                                          FileDownloadCallback on_complete,
                                          FileProgressCallback on_progress)
{
    if (!m_initialized) {
        BOOST_LOG_TRIVIAL(warning) << "[DownloadManager] Not initialized, ignoring downloadFile()";
        return {};
    }
    return m_file_downloader->download(url, local_path, file_name,
                                       std::move(on_complete), std::move(on_progress));
}

void DownloadManager::cancelFileDownload(const std::string& task_id)
{
    if (!m_initialized) return;
    m_file_downloader->cancel(task_id);
}

std::optional<FileDownloadProgress> DownloadManager::getFileProgress(
    const std::string& task_id) const
{
    if (!m_initialized) return std::nullopt;
    return m_file_downloader->getProgress(task_id);
}

void DownloadManager::cancelAllDownloads()
{
    if (!m_initialized) return;
    m_thumb_downloader->cancelAll();
    m_file_downloader->cancelAll();
}

void DownloadManager::shutdown()
{
    if (!m_initialized) return;

    BOOST_LOG_TRIVIAL(info) << "[DownloadManager] Shutting down...";

    m_thumb_downloader->shutdown();
    m_file_downloader->shutdown();
    m_initialized = false;

    BOOST_LOG_TRIVIAL(info) << "[DownloadManager] Shutdown complete.";
}

} // namespace GUI
} // namespace Slic3r
