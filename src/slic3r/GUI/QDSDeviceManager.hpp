#ifndef QDSDEVICEMANAGER_H
#define QDSDEVICEMANAGER_H

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

//cj_2
#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetworkTypes.hpp"
#endif

#include "nlohmann/json.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"

using namespace nlohmann;

namespace Slic3r {
class Udp;
namespace GUI{

struct PlateInfo {
    std::string index;
    std::vector<std::string> filament_colours {};
    std::vector<std::string> filament_types {};
    std::string filament_weight;
    std::string print_time;
    std::vector<std::string> used_extruders {};
    std::string thumb_url;
    std::string nozzle_diameter;
    ThumbnailData thumbnailData;
};
struct GCodeFileInfo {
    std::string extension;
    std::string file_name;
    std::string file_path;
    std::string plate_count;
    std::vector<PlateInfo> plates {};
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
        int minTemp;
        int maxTemp;
        int boxMinTemp;
        int boxMaxTemp;
        std::string type;
    };
public:
    QDSDevice(const std::string dev_id, const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type);
    ~QDSDevice() {};

	// cj_1 
	void updateByJsonData(const json& status);
    bool is_online();
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
private:
	void twoStageParseIntToString(const json& status, std::string& target, std::string first, std::string second);
	void twoStageParseStringToString(const json& status, std::string& target, std::string first, std::string second);

    template<typename T>
	void twoStageParse(const json& status, T& target, std::string first, std::string second);

    // cj_1
    int getJsonCurStageToInt(const json& jsonData, std::string jsonName);
    
    template<typename T>
    bool parseJsonForPath(const json& jsonData, T& target, std::string path);

public:

    // 打印机数据
    std::string     m_name;
    std::string     m_id;
    std::string     m_ip;
    std::string     m_url;
    std::string     m_type;
	std::string     m_frp_url; 
    json            parameters;
    std::string                  m_chamber_temperature{ "0" };
    std::string                  m_extruder_temperature{ "0" };
    std::string                  m_bed_temperature{ "0" };
	std::string                  m_target_chamber{ "0" };
	std::string                  m_target_extruder{ "0" };
	std::string                  m_target_bed{ "0" };
    bool                         m_case_light{ false };
    bool                         m_extruder_filament{ false };   // 

    std::string                  m_home_axes;

    //cj_3
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
    std::vector<float> m_nozzle_diameter { 0.4f };


	std::string     m_print_total_duration;
	std::string     m_print_duration;
	std::string     m_print_filename;
    std::string     m_print_progress{ "N/A" };
    std::string     m_filament_weight{ "0g" };
    std::string     m_print_total_time{ "0m" };
    std::string     m_print_png_url{ "" };      // 打印图标的url
    std::string     m_status{ "standby" };      // 当前打印状态
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

    std::vector<Filament> m_boxData;
	std::vector<int> m_boxTemperature;
	std::vector<int> m_boxHumidity;
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

    std::vector<GCodeFileInfo>    file_info {};
    bool m_fresh_file_info{ false };

    //cj_4
    // Excluded object names pushed from Klipper (exclude_object/excluded_objects).
    std::vector<std::string> m_excluded_objects;

    //cj_3
    std::vector<TimelapseFileInfo> timelapse_file_info {};
    bool m_fresh_timelapse_file_info{ false };

    bool m_is_init_filamentConfig{ false };
	std::mutex m_config_mtx;
    bool is_net_device{ false };

    // Pending save_variables captured before m_filamentConfig is ready;
    // processed as soon as updateFilamentConfig completes.
    json m_pending_save_variables;
    std::atomic<bool> m_has_pending_box_update{ false };

    //cj_3 云端 legacy 轮询：新设备用 m_frp_url；旧设备用 m_net_link_url（NetDevice.link_url）
    std::string m_net_link_url;
    bool        m_net_poll_use_frp{ false };

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
};



using ParameterUpdateCallback = std::function<void(const std::string& device_id)>;
using ConnectionEventCallback = std::function<void(const std::string& device_id, std::string new_status)>;
using DeleteDeviceIDCallback = std::function<void(const std::string& device_id)>;
using FileInfoUpdateCallback = std::function<void(const std::string& device_id)>;

class QDSDeviceManager {
public:
    QDSDeviceManager();
    ~QDSDeviceManager();
    std::string addDevice(const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type);
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

    void stopAllConnection();


    std::string getDeviceTempNozzle(const std::string& deviceId);
    std::string getDeviceTempBed(const std::string& deviceId);
    std::string getDeviceTempChamber(const std::string& deviceId);
    bool        getDeviceCaseLight(const std::string& deviceId);

    //cj_2
    //cj_3 Returns false if the device has no connection or the WebSocket send failed.
    bool sendCommand(const std::string& device_id, const std::string& scriptName,const std::string& script, const std::string& method);

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
#if QDT_RELEASE_TO_PUBLIC
    //cj_5 Match a NetDevice to a locally-discovered device.
    // Primary: NetDevice.mac_address == LocalDiscoveredDevice.serial_number
    // Fallback: NetDevice.local_ip == LocalDiscoveredDevice.ip
    bool findLocalForNetDevice(const NetDevice& net_dev, LocalDiscoveredDevice& out) const;
#endif

    //cj_3 供后台线程枚举设备做 HTTP 状态轮询（拷贝 shared_ptr，持锁时间短）
    std::vector<std::pair<std::string, std::shared_ptr<QDSDevice>>> snapshotDevices();

    //y83
    bool getFileInfoViaP2P();
    bool getTimelapseInfoP2P();
private:
    using WebSocketClient = websocketpp::client<websocketpp::config::asio_client>;

    struct WebSocketConnect {
        std::shared_ptr<QDSDevice> info;
        WebSocketClient client;
        websocketpp::connection_hdl connection_hdl;
        std::thread client_thread;
        std::atomic<bool> running{false};
        std::atomic<bool> stopping{false};
        std::atomic<bool> processing_message{false};
        std::atomic<int> message_processing_count{0};
        std::chrono::steady_clock::time_point last_activity;
        WebSocketConnect() : last_activity(std::chrono::steady_clock::now()) {}
    };

    std::mutex manager_mutex_;
    std::mutex callback_mutex_;
    std::unordered_map<std::string, std::shared_ptr<QDSDevice>> devices_;
    std::unordered_map<std::string, std::shared_ptr<WebSocketConnect>> connections_;

#if QDT_RELEASE_TO_PUBLIC
    std::vector<NetDevice> net_devices;
#endif

    //cj_5
    LocalDeviceDiscovery m_local_discovery;
    //cj_5
    SSDPDiscovery         m_ssdp_discovery;


    // WebSocket
    void onOpen(const std::string& device_id, websocketpp::connection_hdl hdl);
    void onMessage(const std::string& device_id, websocketpp::connection_hdl hdl, 
                   WebSocketClient::message_ptr msg);
    void onClose(const std::string& device_id, websocketpp::connection_hdl hdl);
    void onFail(const std::string& device_id, websocketpp::connection_hdl hdl);

    void sendSubscribeMessage(const std::string& device_id);
    void getAllErrorList(const std::string& device_id);
    void handleDeviceMessage(const std::string& device_id, const json& message);
    void updateDeviceMsg(const std::string& device_id, const json& message);
    void updateDeviceStatus(const std::string& device_id, std::string new_status);
    void updatePrintThumbUrl(std::shared_ptr<QDSDevice>& device, const json& message);
    void updatePrintThumbUrlWithOutMsg(std::shared_ptr<QDSDevice>& device);

    int generateDeviceID();
    void stopConnection(const std::string& device_id);
    void safeStopConnection(const std::string& device_id);
    void cleanupConnection(const std::string& device_id);

    ConnectionEventCallback connection_event_callback_;
    ParameterUpdateCallback parameter_update_callback_;
    DeleteDeviceIDCallback delete_device_id_callback_;
    FileInfoUpdateCallback file_info_update_callback_;


    std::thread health_check_thread_;
    std::atomic<bool> health_check_running_{false};
    std::chrono::seconds health_check_interval_{5};

    void healthCheckLoop();
    void performHealthCheck();

    void updateDeviceData(std::shared_ptr<QDSDevice>& device, const json& result, 
                          std::string& new_status, bool& is_update);
    void processConnectionStatus(const std::string& device_id, const std::string& status);
    void safeCallbackInvoke();
    //y83
    void updateDeviceFileInfo(std::shared_ptr<QDSDevice>& device, const json& result, bool support_p2p=false);

    //cj_3
    void updateDeviceTimelapseFileInfo(std::shared_ptr<QDSDevice>& device, const std::string& response_body);
    
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

    //y83
    std::string m_text_from_p2p;
    std::map<std::string, std::vector<char>> m_p2p_thumbnails;
    std::map<std::string, std::vector<char>> m_p2p_timelapse_thumbnails;
};




}
}

#endif //QDSDEVICEMANAGER_H
