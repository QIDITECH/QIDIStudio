#ifndef QDSDEVICEMANAGER_H
#define QDSDEVICEMANAGER_H

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <cstdint>
#include <initializer_list>

//cj_2
#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetworkTypes.hpp"
#endif

#include "nlohmann/json.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"

#include "../QIDI/QIDIDeviceWebSocket.hpp"

using namespace nlohmann;

namespace Slic3r {
class Udp;
class DevConfig;
namespace GUI{

struct QDSPlateInfo {
    std::string index;
    std::vector<std::string> filament_colours {};
    std::vector<std::string> filament_types {};
    std::string filament_weight;
    std::string print_time;
    std::vector<std::string> used_extruders {};
    std::string thumb_url;
    std::string nozzle_diameter;
    ThumbnailData thumbnailData;
    //y84
    bool thumbnailFromP2P { false };
    bool thumbnailLoaded { false };
};
struct GCodeFileInfo {
    std::string extension;
    std::string file_name;
    std::string file_path;
    std::string plate_count;
    std::vector<QDSPlateInfo> plates {};
    std::string show_filament_weight;
    std::string show_print_time;
    std::string show_thumb_url;
    int thumbnailsSize{ 0 };
};

//cj_3
struct TimelapseFileInfo {
    std::string file_name;
    std::string file_size;
    std::string modified_time;
    std::string thumb_url;
    //y83
    std::string video_add;
    ThumbnailData thumbnailData;
    // y84
    bool thumbnailLoaded { false };
};

//cj_5
struct LocalDiscoveredDevice {
    std::string serial_number;
    std::string ip;
    std::string name;
    std::string model;
    std::string raw_payload;
    bool legacy_device{ false };
    std::chrono::steady_clock::time_point last_seen;
};

//cj_5
struct QDSDeviceErrorData {
    std::string event_value;
    std::string error_code;
    std::string error_message;
    bool error_popup;
    int error_type;
    int error_weight;
    std::string prossess_message;
};

//cj_5
class LocalDeviceDiscovery {
public:
    using Snapshot = std::vector<LocalDiscoveredDevice>;
    using RefreshCallback = std::function<void(Snapshot)>;

    void refresh(bool force, RefreshCallback callback);
    Snapshot snapshot() const;
    bool findBySerial(const std::string& serial, LocalDiscoveredDevice& out) const;
    bool isCacheFresh(std::chrono::seconds ttl) const;

private:
    void mergeDevice(LocalDiscoveredDevice device);
    void finishRefresh();

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, LocalDiscoveredDevice> m_by_serial;
    std::unordered_map<std::string, LocalDiscoveredDevice> m_by_ip;
    std::shared_ptr<Udp> m_udp;
    std::vector<RefreshCallback> m_pending_callbacks;
    bool m_refreshing{ false };
    std::chrono::steady_clock::time_point m_last_refresh{};
    std::chrono::seconds m_cache_ttl{ 15 };
};

//cj_5 SSDP-based device discovery. Sends M-SEARCH to 239.255.255.250:5863
// and parses QIDI-custom NOTIFY responses. Same snapshot type as LocalDeviceDiscovery.
//cj_5
class SSDPDiscovery {
public:
    using Snapshot = std::vector<LocalDiscoveredDevice>;
    using RefreshCallback = std::function<void(Snapshot)>;

    SSDPDiscovery();
    ~SSDPDiscovery();

    void refresh(bool force, RefreshCallback callback);
    Snapshot snapshot() const;
    bool findBySerial(const std::string& serial, LocalDiscoveredDevice& out) const;
    //cj_5
    bool findByIP(const std::string& ip, LocalDiscoveredDevice& out) const;
    bool isCacheFresh(std::chrono::seconds ttl) const;
    void stop();

private:
    void mergeDevice(LocalDiscoveredDevice device);
    void finishRefresh();

private:
    struct priv;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, LocalDiscoveredDevice> m_by_serial;
    std::unordered_map<std::string, LocalDiscoveredDevice> m_by_ip;
    std::vector<RefreshCallback> m_pending_callbacks;
    bool m_refreshing{ false };
    std::chrono::steady_clock::time_point m_last_refresh{};
    std::chrono::seconds m_cache_ttl{ 30 };
    std::unique_ptr<priv> p;
};

class QDSDevice{
public:
    struct Filament {
        bool hasMaterial{ false };
        int filament_idex;
        std::string name;
        std::string vendor;
        std::string colorHexCode;
        std::string filament_id;
        int minTemp;
        int maxTemp;
        int boxMinTemp;
        int boxMaxTemp;
        std::string type;
    };
public:
    QDSDevice(const std::string dev_id, const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type, const std::string& model_id, const std::string& firmware_version = "");
    ~QDSDevice();

    //y84 重新读取模型 json 配置（仅合并 <= 设备版本号的参数），供版本号就绪后调用
    void update_config_from_file(std::string model_id);

	// cj_1 
	void updateByJsonData(const json& status);
    bool is_online();
    //y85
    bool is_p2p_download_device() const { return is_net_device && !is_local_transitioned; }
    void updateFilamentConfig();    //When "m_frp_url" is updated, update the config file.

    void updateBoxDataByJson(const json status);
    std::vector<float> getNozzleDiameter();
    void reset_update_status(){
        box_is_update = true;
    };

    //y79
    void updatePrinterStatusData(json& status);
    std::string getMakerJobState();
    std::string getMakerJobProgress();
    void setMakerJobIsUpdate(bool value);

    //cj_5
    void updateAllErrorData(json& jsonData);
    void updateErrorDataForNotiry(json& jsonData);
    void updateErrorDataSingle(json& jsonData, std::string event_value);
//y84
    void update_device_config(json json_result);
    std::vector<GCodeFileInfo>     snapshotModelFiles();
    std::vector<TimelapseFileInfo> snapshotTimelapseFiles();
    bool has_model_files_loaded()     { return m_model_list_loaded.load(); }
    bool has_timelapse_files_loaded() { return m_timelapse_list_loaded.load(); }
    std::string model_list_signature()     { std::lock_guard<std::mutex> lk(m_file_info_mtx);  return m_last_model_sig; }
    std::string timelapse_list_signature() { std::lock_guard<std::mutex> lk(m_timelapse_mtx); return m_last_timelapse_sig; }
//y84
private:
	// 统一字段提取：path 为 1~N 级 key 路径（如 {"a"} / {"a","b"}）
	template<typename T>
	bool extract(T& target, const json& j, std::initializer_list<std::string> path);
	bool extract_int_str(std::string& target, const json& j, std::initializer_list<std::string> path);
public:

    // 打印机数据
    std::string     m_name;
    std::string     m_id;
    std::string     m_ip;
    std::string     m_url;
    std::string     m_type;
    std::string     m_model_id; //y84
	std::string     m_frp_url;    // 用于摄像头查看  
//y84
    std::string     m_serial_number;
    std::string     m_mac_address;
    std::string     m_firmware_version;
    std::string     m_machine_name;

    json            parameters;
    std::string                  m_chamber_temperature{ "0" };
    std::string                  m_extruder_temperature{ "0" };
    std::string                  m_bed_temperature{ "0" };
	std::string                  m_target_chamber{ "0" };
	std::string                  m_target_extruder{ "0" };
	std::string                  m_target_bed{ "0" };
    bool                         m_case_light{ false };
    bool                         m_extruder_filament{ false };   // 
    //移动控制数据
    std::string                  m_home_axes;    // 现在只有两个值：一个"xyz"代表已经回归原位，可移动 一个为空代表已经不在原位，不可移动

    //cj_3
    //空调参数：m_enable_polar_cooler 为机型/配置是否支持（如 printing.polar_cooler）；m_polar_cooler 为运行时真实输出状态（polar_cooler pin）
	std::atomic<bool>  m_enable_polar_cooler{ false };
	std::atomic<bool>  m_polar_cooler{ false };
	//cj_4
	// Set true when m_polar_cooler changes from device JSON; cleared after status UI sync.
	std::atomic<bool>  m_polar_cooler_dirty_for_ui{ false };
	float m_auxiliary_fan_speed{ 0.0 };
    float m_chamber_fan_speed{ 0.0 };
    float m_cooling_fan_speed{ 0.0 };
    /** Snapped display percent (50/100/124/166) from gcode_move.speed_factor; fan row print speed label */
    int m_print_speed_display_percent{ 100 };

    //y78
    std::vector<float> m_nozzle_diameter { 0.4f }; // 喷嘴直径，默认为0.4mm


    // 进度条的数据
	std::string     m_print_total_duration;   // cj_1 总时间需要，现在这个字段不正确所以不使用
	std::string     m_print_duration;         // cj_1 使用这个字段和m_print_progress_float 计算时间
	std::string     m_print_filename;
    std::string     m_print_progress{ "N/A" };
    std::string     m_filament_weight{ "0g" };
    std::string     m_print_total_time{ "0m" };
    std::string     m_print_png_url{ "" };      // 打印图标的url
    std::string     m_status{ "offline" };      // 当前打印状态
	std::string     m_print_state;              // 当前打印状态
    std::string     m_print_msg{""};                // 当前打印状态信息 例如：清理打印头，校准
    //y83
    std::string     m_print_png_plate_index{""};
    std::string     m_print_png_path_for_p2p{""};
	int m_print_cur_layer{ 0 };
	int m_print_total_layer{ 0 };
	//cj_4
	// Current plate index from Klipper print_stats/plateindex,
	// parsed as int from JSON string. Default 1.
	int m_plate_index{1};
	double m_print_progress_float{ 0 };         // cj_1 当前进度百分比 0.16代表 16%

    // Multicolor box data
    /*
        0到15的位置代表盒子的数据，盒子最多四个，每个盒子最多 四个槽
        每个Filament代表一个槽的数据
        16代表外挂的数据
    */
    std::vector<Filament> m_boxData;
	std::vector<int> m_boxTemperature;
	std::vector<int> m_boxHumidity;
    std::vector<int> m_boxState;
    std::vector<long> m_boxEndTime;
    bool box_is_update;

    //y83
    std::string m_box_signature;
    bool m_is_auto_reload{ false };    // 
    std::string m_cur_slot;            // 当前使用的槽，第一个槽为 slot-0  第四个为slot-3
    int m_box_count{ 0 };              // 当前盒子的数量
    std::vector<Filament> m_filamentConfig; // 所有的数据，index是filament的编号
    bool m_auto_read_rfid{ false };             // 插入时自动更新
    bool m_init_detect{ false };                // 开机时检测
    bool m_auto_reload_detect{ false };         // 自动续料
    // QDS multi-color controller generation marker, e.g. "box_v2".
    // Empty for legacy/non-QDS devices. Used to hide the Unload button on gen-2 boxes.
    std::string m_box_identity = "box_v2";

    //y78
    std::vector<std::string> m_filament_colors;
    std::vector<std::string> m_filament_type;
    std::vector<std::string> m_filament_id;
    std::vector<int> m_slot_id;
    std::vector<int> m_slot_state;

    //cj_2 print model data


	// common data
	std::atomic<bool>            is_selected{ false };
	std::atomic<bool>            is_update{ false };
    //cj_3
    std::atomic<bool>            reconnecting{ false };
    std::atomic<bool>            m_is_update_box_temp{ false };
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
    //cj_3
    std::chrono::steady_clock::time_point last_reconnect = std::chrono::steady_clock::time_point::min();

    std::mutex                    m_file_info_mtx;
    std::vector<GCodeFileInfo>    file_info {};
//y84
    std::atomic<bool> m_fresh_file_info{ false };
    std::atomic<bool> m_file_info_load_failed{ false };
    std::atomic<uint64_t> m_file_gen{ 0 };
    std::mutex m_p2p_xfer_mtx;
    std::string m_last_model_sig;
    std::string m_last_timelapse_sig;
    std::atomic<bool> m_model_list_loaded{ false };
    std::atomic<bool> m_timelapse_list_loaded{ false };
    std::atomic<bool> m_fresh_timelapse_file_info{ false };
    std::atomic<bool> m_timelapse_info_load_failed{ false };
    std::atomic<bool> m_is_init_filamentConfig{ false };
//y84

    //cj_4
    // Excluded object names pushed from Klipper (exclude_object/excluded_objects).
    std::vector<std::string> m_excluded_objects;

    //cj_3
    std::mutex                     m_timelapse_mtx;
    std::vector<TimelapseFileInfo> timelapse_file_info {};
//y84
    std::shared_mutex m_config_mtx;
    std::atomic<bool> m_fetching{ false };
    std::atomic<bool> m_stop{ false };
    std::thread       m_cfg_thread;
//y84
    bool is_net_device{ false };
    //y85
    bool is_local_transitioned{ false };

//y84
    json        m_pending_save_variables;
    std::mutex  m_pending_mtx;

    //y79
    std::string maker_job_state = "";
    std::string maker_job_progress = "";
    std::atomic<bool> maker_job_is_update{ false };

    std::vector<QDSDeviceErrorData> m_errorData;
    //y83
    std::mutex m_errorData_mtx;
    bool m_needUpdateErrorData{ false };

    //y83 p2p
    std::atomic<bool> p2p_enable{ false };
    std::string p2p_license="";
    std::string p2p_server="";
    std::string p2p_relay_list="";
    std::atomic<bool> active_p2p{false};

    //y84
    DevConfig* m_config;
    std::atomic<bool> timelapse_state {false};
    std::mutex m_process_state_mtx;
    std::atomic<bool> is_support_detect_spaghetti {false};
    std::atomic<bool> is_support_detect_foreign{ false };
    std::string spaghetti_level="";
    std::atomic<bool> support_no_sse { false };
    bool is_first_update { true };
    std::atomic<bool> box_info_is_ready {false};

};



using ParameterUpdateCallback = std::function<void(const std::string& device_id)>;
using ConnectionEventCallback = std::function<void(const std::string& device_id, std::string new_status)>;
using DeleteDeviceIDCallback = std::function<void(const std::string& device_id)>;
using FileInfoUpdateCallback = std::function<void(const std::string& device_id)>;
// y84
using FileThumbnailReadyCallback = std::function<void(const std::string& device_id,
                                                      bool               is_timelapse,
                                                      const std::string& file_name,
                                                      const std::vector<uint8_t>& data)>;

class QDSDeviceManager {
public:
    QDSDeviceManager();
    ~QDSDeviceManager();
    //y84
    std::string addDevice(const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type, const std::string& model_id);
    bool addDevice(std::shared_ptr<QDSDevice> device);
    bool removeDevice(const std::string& device_id);
    bool connectDevice(const std::string device_id);
    bool disconnectDevice(const std::string& device_id);
    void reconnectDevice(const std::string& device_id);
    std::shared_ptr<QDSDevice> getDevice(const std::string& device_id);

    //y80
    std::string getNetDeviceIDByIp(const std::string& ip);
    std::string getLocalDeviceIDByIp(const std::string& ip);

    void setConnectionEventCallback(ConnectionEventCallback cb) { 
        std::lock_guard<std::mutex> lock(callback_mutex_);
        connection_event_callback_ = std::move(cb); 
    }
    void setParameterUpdateCallback(ParameterUpdateCallback cb) { 
        std::lock_guard<std::mutex> lock(callback_mutex_);
        parameter_update_callback_ = std::move(cb); 
    }
    void setDeleteDeviceIDCallback(DeleteDeviceIDCallback cb) { 
        std::lock_guard<std::mutex> lock(callback_mutex_);
        delete_device_id_callback_ = std::move(cb); 
    }
    void setFileInfoUpdateCallback(FileInfoUpdateCallback cb){
        std::lock_guard<std::mutex> lock(callback_mutex_);
        file_info_update_callback_ = std::move(cb); 
    }

    //y84
    void setFileThumbnailReadyCallback(FileThumbnailReadyCallback cb){
        std::lock_guard<std::mutex> lock(callback_mutex_);
        file_thumbnail_ready_callback_ = std::move(cb);
    }

    void stopAllConnection();


    std::string getDeviceTempNozzle(const std::string& deviceId);
    std::string getDeviceTempBed(const std::string& deviceId);
    std::string getDeviceTempChamber(const std::string& deviceId);
    bool        getDeviceCaseLight(const std::string& deviceId);

    //cj_2
    //cj_3 Returns false if the device has no connection or the WebSocket send failed.
    bool sendCommand(const std::string& device_id, const std::string& scriptName,const std::string& script, const std::string& method);
    //y84
    bool sendCommand(const std::string& device_id, const json& params, const std::string& method);

    void sendCommand(const std::string& device_id, const std::string& script);
    void sendActionCommand(const std::string& device_id, const std::string& action_type);

    void setSelected(const std::string& device_id);
    std::shared_ptr<QDSDevice> getSelectedDevice();
    void unSelected();
#if QDT_RELEASE_TO_PUBLIC
    void setNetDevices(std::vector<NetDevice> devices);
    std::vector<NetDevice> getNetDevices();
#endif
    void upBoxInfoToBoxMsg(std::shared_ptr<QDSDevice>& device);
    void getFileInfo(const std::string& device_id);
    void resetBoxUpdateStatus(const std::string& device_id);

    //cj_5
    void refreshLocalDevices(bool force, LocalDeviceDiscovery::RefreshCallback callback);
    //cj_5
    bool findLocalDeviceBySerial(const std::string& serial, LocalDiscoveredDevice& out) const;
    //cj_5
    LocalDeviceDiscovery::Snapshot snapshotLocalDevices() const;
    //cj_5 SSDP discovery API.
    void refreshSSDPDevices(bool force, SSDPDiscovery::RefreshCallback callback);
    bool findSSDPDeviceBySerial(const std::string& serial, LocalDiscoveredDevice& out) const;
    //cj_5
    bool findSSDPDeviceByIP(const std::string& ip, LocalDiscoveredDevice& out) const;
    SSDPDiscovery::Snapshot snapshotSSDPDevices() const;

    //cj_3 供后台线程枚举设备做 HTTP 状态轮询（拷贝 shared_ptr，持锁时间短）
    std::vector<std::pair<std::string, std::shared_ptr<QDSDevice>>> snapshotDevices();

//y83 y84
    bool getFileInfoViaP2P(std::shared_ptr<QDSDevice> device,
                           std::vector<std::pair<std::string, std::string>>& out_thumb_reqs,
                           std::string& out_text);
    bool getTimelapseInfoP2P(std::shared_ptr<QDSDevice> device,
                             std::vector<std::string>& out_jpg_reqs,
                             std::string& out_text);
    void fetchModelThumbnailsP2P(std::shared_ptr<QDSDevice> device,
                                 const std::string& device_id,
                                 const std::vector<std::pair<std::string, std::string>>& thumb_reqs,
                                 uint64_t gen);
    void fetchTimelapseThumbnailsP2P(std::shared_ptr<QDSDevice> device,
                                     const std::string& device_id,
                                     const std::vector<std::string>& jpg_reqs,
                                     uint64_t gen);
//y84   

    void updateDeviceStatus(const std::string& device_id, std::string new_status);
    //y84
    void getDeviceInfo(const std::string& device_id);


private:
    std::mutex manager_mutex_;
    std::mutex callback_mutex_;
    std::unordered_map<std::string, std::shared_ptr<QDSDevice>> devices_;
    // WebSocket 连接管理已抽离到 QIDIDeviceWebSocket（成员 m_ws）
    std::unique_ptr<QIDIDeviceWebSocket> m_ws;

#if QDT_RELEASE_TO_PUBLIC
    std::vector<NetDevice> net_devices;
#endif

    //cj_5
    LocalDeviceDiscovery m_local_discovery;
    //cj_5
    SSDPDiscovery         m_ssdp_discovery;


    void sendSubscribeMessage(const std::string& device_id);
    void getAllErrorList(const std::string& device_id);
    void handleDeviceMessage(const std::string& device_id, const json& message);
    void updateDeviceMsg(const std::string& device_id, const json& message);

    int generateDeviceID();
    void stopConnection(const std::string& device_id);

    ConnectionEventCallback connection_event_callback_;
    ParameterUpdateCallback parameter_update_callback_;
    DeleteDeviceIDCallback delete_device_id_callback_;
    FileInfoUpdateCallback file_info_update_callback_;
    //y84
    FileThumbnailReadyCallback file_thumbnail_ready_callback_;


    std::thread health_check_thread_;
    std::atomic<bool> health_check_running_{false};
    std::chrono::seconds health_check_interval_{5};

    void healthCheckLoop();
    void performHealthCheck();

    void updateDeviceData(std::shared_ptr<QDSDevice>& device, const json& result, 
                          std::string& new_status, bool& is_update);
    void processConnectionStatus(const std::string& device_id, const std::string& status);
    void safeCallbackInvoke();
    bool updateDeviceFileInfo(std::shared_ptr<QDSDevice>& device, const json& result, bool support_p2p = false,
                             const std::map<std::string, std::vector<char>>* p2p_thumbnails = nullptr);

    bool updateDeviceTimelapseFileInfo(std::shared_ptr<QDSDevice>& device, const std::string& response_body,
                                       const std::map<std::string, std::vector<char>>* p2p_timelapse_thumbnails = nullptr);
    std::vector<std::pair<std::string, std::string>> collectMissingModelThumbRequests(
        std::shared_ptr<QDSDevice>& device,
        const std::vector<std::pair<std::string, std::string>>& reqs);
    std::vector<std::string> collectMissingTimelapseThumbRequests(
        std::shared_ptr<QDSDevice>& device,
        const std::vector<std::string>& jpg_reqs);
    
    // 线程安全地获取回调函数
    ConnectionEventCallback getConnectionEventCallback() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return connection_event_callback_;
    }
    
    ParameterUpdateCallback getParameterUpdateCallback() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return parameter_update_callback_;
    }
    
    DeleteDeviceIDCallback getDeleteDeviceIDCallback() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return delete_device_id_callback_;
    }

    FileInfoUpdateCallback getFileInfoUpdateCallback(){
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return file_info_update_callback_;
    }

    FileThumbnailReadyCallback getFileThumbnailReadyCallback(){
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return file_thumbnail_ready_callback_;
    }

    //y83
    std::string m_text_from_p2p;
    std::map<std::string, std::vector<char>> m_p2p_thumbnails;
    std::map<std::string, std::vector<char>> m_p2p_timelapse_thumbnails;
};




}
}

#endif //QDSDEVICEMANAGER_H
