#ifndef slic3r_DownloadManager_hpp_
#define slic3r_DownloadManager_hpp_

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <deque>
#include <unordered_map>
#include <atomic>
#include <optional>

namespace Slic3r {
namespace GUI {

// ============================================================
//  Callback Result Structures
// ============================================================

/**
 * @brief Callback result for a single thumbnail download.
 *        The callback is guaranteed to be triggered in the GUI main thread.
 */
struct ThumbnailResult {
    std::string          file_name;    // G-Code file name for locating list items
    std::string          url;          // Thumbnail URL (used as unique identifier)
    bool                 success{false};
    std::string          error_msg;    // Failure reason (empty if successful)
    std::vector<uint8_t> png_data;     // PNG binary data (valid if successful)
    std::string          cache_path;   // Local cache path (valid if successful)
};

/**
 * @brief File download progress snapshot.
 *        Both on_complete and on_progress callbacks carry this structure,
 *        guaranteed to be triggered in the GUI main thread.
 */
struct FileDownloadProgress {
    std::string task_id;               // Unique task ID
    std::string file_name;             // File name (for UI display)
    int64_t     bytes_downloaded{0};   // Bytes downloaded
    int64_t     bytes_total{-1};       // Total bytes (-1 if Content-Length is not returned by the server)
    float       progress{0.0f};        // Progress 0.0~1.0 (-1.0f if total is unknown)

    enum class State {
        Pending,     // Waiting in queue
        Downloading, // Downloading
        Completed,   // Completed
        Failed,      // Failed
        Cancelled,   // Cancelled
    } state{State::Pending};

    std::string error_msg;             // Failure reason (empty if successful)
};

// ============================================================
//  Callback Type Aliases
// ============================================================

/** Thumbnail download completion callback (triggered in GUI thread) */
using ThumbnailCallback    = std::function<void(ThumbnailResult)>;

/** File download completion or failure callback (triggered in GUI thread) */
using FileDownloadCallback = std::function<void(FileDownloadProgress)>;

/** File download progress callback, triggered for each data block (can be nullptr) */
using FileProgressCallback = std::function<void(FileDownloadProgress)>;

// ============================================================
//  ThumbnailCache — Disk Cache for Thumbnails
// ============================================================

/**
 * @brief Manages disk cache for thumbnails, providing hit detection and writing.
 *
 *  Directory structure:
 *    {cache_dir}/
 *      ab/abcdef1234567890abcdef1234567890   (file name = MD5(URL without query params), PNG content)
 *      cd/...
 *
 *  Cache Key: MD5 of the URL after removing query parameters (everything after '?'),
 *             taking the first two characters as the subdirectory.
 */
class ThumbnailCache {
public:
    explicit ThumbnailCache(const std::string& cache_dir);

    /**
     * @brief 根据 URL 生成缓存 key（去除查询参数后取 MD5 hex，全小写）。
     */
    static std::string makeKey(const std::string& url);

    /**
     * @brief 查询缓存是否命中。
     * @return 命中：返回缓存文件完整路径；未命中：返回空字符串。
     */
    std::string lookup(const std::string& url) const;

    /**
     * @brief 将 PNG 数据写入磁盘缓存。
     * @return 成功：返回缓存文件路径；失败：返回空字符串。
     */
    std::string store(const std::string& url, const std::vector<uint8_t>& data);

    const std::string& cacheDir() const { return m_cache_dir; }

private:
    /** 根据 key 生成完整文件路径（key 前两位为子目录） */
    std::string keyToPath(const std::string& key) const;

    std::string        m_cache_dir;
    mutable std::mutex m_mutex;
};


// ============================================================
//  ThumbnailDownloader — 缩略图线程池下载器
// ============================================================

/**
 * @brief 使用固定大小线程池并发下载缩略图。
 *
 *  - 优先查询 ThumbnailCache，命中时直接通过 CallAfter 触发回调，不占线程槽位。
 *  - 未命中时加入任务队列，由工作线程下载后写缓存，再通过 CallAfter 触发回调。
 *  - 所有回调保证在 GUI 主线程中执行。
 */
class ThumbnailDownloader {
public:
    /**
     * @param cache        共享的缓存实例
     * @param thread_count 工作线程数，默认 3
     */
    explicit ThumbnailDownloader(std::shared_ptr<ThumbnailCache> cache,
                                 int thread_count = 3);
    ~ThumbnailDownloader();

    /**
     * @brief 请求下载一张缩略图。
     *
     *  缓存命中：CallAfter 立即触发 callback（不占线程槽位）。
     *  缓存未命中：任务加入队列，下载完成后触发 callback。
     *  callback 保证在 GUI 线程执行。
     */
    void download(const std::string& url,
                  const std::string& file_name,
                  ThumbnailCallback  callback);

    /**
     * @brief 取消所有尚未被线程取出的缩略图下载任务。
     */
    void cancelAll();

    /** 停止所有工作线程，等待它们退出（析构时自动调用）。 */
    void shutdown();

private:
    struct Task {
        std::string       url;
        std::string       file_name;
        ThumbnailCallback callback;
    };

    void workerLoop();
    void dispatchResult(ThumbnailResult result, ThumbnailCallback cb);

    std::shared_ptr<ThumbnailCache> m_cache;
    int                             m_thread_count;

    std::mutex               m_mutex;
    std::condition_variable  m_cv;
    std::queue<Task>         m_queue;
    std::vector<std::thread> m_threads;
    std::atomic<bool>        m_stop{false};
};


// ============================================================
//  FileDownloader — G-Code 文件串行下载器
// ============================================================

/**
 * @brief 使用单个后台线程串行下载 G-Code 文件。
 *
 *  - 多个 download() 调用加入 deque 队列，依次执行。
 *  - 维护进度快照表，可通过 getProgress() 轮询。
 *  - on_complete 和 on_progress 回调均保证在 GUI 主线程执行。
 */
class FileDownloader {
public:
    FileDownloader();
    ~FileDownloader();

    /**
     * @brief 请求下载一个文件，加入串行队列尾部。
     * @return task_id，可用于取消或查询进度。
     */
    std::string download(const std::string&   url,
                         const std::string&   local_path,
                         const std::string&   file_name,
                         FileDownloadCallback on_complete,
                         FileProgressCallback on_progress = nullptr);

    /**
     * @brief 取消任务。
     *  - Pending 状态：立即从队列移除。
     *  - Downloading 状态：设置取消标志，当前下载完成后停止。
     */
    void cancel(const std::string& task_id);

    /**
     * @brief 获取指定任务的进度快照（线程安全）。
     * @return 任务存在时返回快照；不存在时返回 std::nullopt。
     */
    std::optional<FileDownloadProgress> getProgress(const std::string& task_id) const;

    /** 取消当前与队列中全部文件下载任务。 */
    void cancelAll();

    /** 停止下载线程，等待其退出（析构时自动调用）。 */
    void shutdown();

    /** 生成唯一任务 ID（UUID 格式）。 */
    static std::string generateTaskId();

private:
    struct QueueItem {
        std::string          task_id;
        std::string          url;
        std::string          local_path;
        std::string          file_name;
        FileDownloadCallback on_complete;
        FileProgressCallback on_progress;
    };

    void workerLoop();
    void doDownload(const QueueItem& item);
    void updateProgress(const std::string& task_id, const FileDownloadProgress& snap);
    void dispatchProgress(FileDownloadProgress snap, FileProgressCallback cb);
    void dispatchComplete(FileDownloadProgress snap, FileDownloadCallback cb);

    /** 若本地已存在同名文件，追加 _YYYYMMDD_HHMMSS 后缀返回新路径。 */
    static std::string resolveConflict(const std::string& local_path);

    mutable std::mutex      m_mutex;
    std::condition_variable m_cv;
    std::deque<QueueItem>   m_queue;

    mutable std::mutex    m_progress_mutex;
    std::unordered_map<std::string, FileDownloadProgress> m_progress_map;

    std::thread      m_thread;
    std::atomic<bool> m_stop{false};
    std::string      m_current_task_id;  // 当前正在下载的 task_id
    std::atomic<bool> m_cancel_current{false};
};


// ============================================================
//  DownloadManager — 全局单例门面
// ============================================================

/**
 * @brief 全局下载管理器，统一管理缩略图下载和文件下载。
 *
 *  调用方只需调用 downloadThumbnail() / downloadFile()，
 *  无需了解内部的队列、线程池或缓存实现。
 *
 *  生命周期：
 *    - GUI_App::OnInit()  中调用 init()
 *    - GUI_App::OnExit()  中调用 shutdown()
 */
class DownloadManager {
public:
    /** 获取全局单例。 */
    static DownloadManager& getInstance();

    /**
     * @brief 初始化，必须在首次调用 download*() 之前调用。
     * @param thumb_cache_dir 缩略图缓存根目录（不存在时自动创建）。
     */
    void init(const std::string& thumb_cache_dir);

    // ── 缩略图接口 ────────────────────────────────────────────

    /**
     * @brief 下载一张缩略图。
     *
     *  - 缓存命中：callback 在 GUI 线程异步触发，不发起网络请求。
     *  - 缓存未命中：线程池排队下载，完成后 callback 在 GUI 线程触发。
     *  - 无论成功失败，callback 保证被调用一次。
     */
    void downloadThumbnail(const std::string& url,
                           const std::string& file_name,
                           ThumbnailCallback  callback);

    // ── 文件下载接口 ──────────────────────────────────────────

    /**
     * @brief 下载一个 G-Code 文件（加入串行队列）。
     *
     * @param on_complete  完成或失败时在 GUI 线程触发（必须提供）。
     * @param on_progress  每个数据块到达时在 GUI 线程触发（可为 nullptr）。
     * @return task_id，可用于取消或查询进度。
     */
    std::string downloadFile(const std::string&   url,
                             const std::string&   local_path,
                             const std::string&   file_name,
                             FileDownloadCallback on_complete,
                             FileProgressCallback on_progress = nullptr);

    /** 取消文件下载任务。 */
    void cancelFileDownload(const std::string& task_id);

    /**
     * @brief 获取文件下载进度快照（可用于轮询刷新进度条）。
     * @return 任务存在时返回快照；不存在时返回 std::nullopt。
     */
    std::optional<FileDownloadProgress> getFileProgress(const std::string& task_id) const;

    // ── 生命周期 ─────────────────────────────────────────────

    /** 切换设备时调用，取消当前与排队中的全部下载任务。 */
    void cancelAllDownloads();

    /** 停止所有后台线程，在 GUI_App::OnExit() 中调用。 */
    void shutdown();

private:
    DownloadManager()  = default;
    ~DownloadManager() = default;
    DownloadManager(const DownloadManager&)            = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    std::shared_ptr<ThumbnailCache>      m_thumb_cache;
    std::unique_ptr<ThumbnailDownloader> m_thumb_downloader;
    std::unique_ptr<FileDownloader>      m_file_downloader;
    bool                                 m_initialized{false};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_DownloadManager_hpp_
