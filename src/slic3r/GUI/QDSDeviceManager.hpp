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

#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#endif

#include "nlohmann/json.hpp"

using namespace nlohmann;

namespace Slic3r {
namespace GUI{

struct FileInfo {
    std::string file_name;
    std::string filament_weight;
    std::string print_time;
    double modified_time;
    std::string thumb_url;
};


// cj_1
class QDSFilamentConfig 
{
private:
    struct  FilamentData
    {
        std::string path;
        std::string name;
        std::vector<std::string> data;
    };
public:
	static QDSFilamentConfig& getInstance() {
		static QDSFilamentConfig instance;
		return instance;
	}

    std::string getHexCode(int index);
    int getColorIndex(std::string hexCode);

	std::string getcolorDes(int index);
	std::string getTypeName(int index);
    std::string getFilamentType(int index);
	int getTypeNameIndex(std::string typeName);

	std::string getVendor(int index);
	int getVendorIndex(std::string vendor);

private:
    void init();
    void initTypeName();
    void initFilamentData(FilamentData& filamentData);

    std::string getData(FilamentData filamentData, int index);
    int getIndex(FilamentData filementData, std::string name);

#if QDT_RELEASE_TO_PUBLIC
    Environment m_env;
#endif

public:
	FilamentData m_colorHexCode{ "/backend/v1/setting/filament/color/all","hexCode" };
	FilamentData m_colorDes{ "/backend/v1/setting/filament/color/all","description" };
	FilamentData m_typeName{ "/backend/v1/setting/filament/material/all","filamentTypeName" };
	FilamentData m_vendor{ "/backend/v1/setting/filament/vendor/all","vendor" };
    FilamentData m_filament_type{ "/backend/v1/setting/filament/vendor/all", "type"};

private:
    QDSFilamentConfig();
	~QDSFilamentConfig();

    QDSFilamentConfig(const QDSFilamentConfig&) = delete;
    QDSFilamentConfig& operator=(const QDSFilamentConfig&) = delete;
    QDSFilamentConfig(QDSFilamentConfig&&) = delete;
    QDSFilamentConfig& operator=(QDSFilamentConfig&&) = delete;

    std::future<void> m_future;

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
	void updateByJsonData(json& status);
    bool is_online();
    void updateFilamentConfig();

    void updateBoxDataByJson(const json status);
private:
	void twoStageParseIntToString(const json& status, std::string& target, std::string first, std::string second);
	void twoStageParseStringToString(const json& status, std::string& target, std::string first, std::string second);

    template<typename T>
	void twoStageParse(const json& status, T& target, std::string first, std::string second);

    // cj_1
    int getJsonCurStageToInt(const json& jsonData, std::string jsonName);
    static void initGeneralData();

public:
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
    std::atomic<bool>            m_case_light{ false };
    bool                         m_extruder_filament{ false };   // 

    std::string                  m_home_axes;

    std::atomic<bool>  m_polar_cooler{ false };
    float m_auxiliary_fan_speed{ 0.0 };
    float m_chamber_fan_speed{ 0.0 };
    float m_cooling_fan_speed{ 0.0 };

	std::string     m_print_total_duration;   // cj_1
	std::string     m_print_duration;         // cj_1
	std::string     m_print_filename;
    std::string     m_print_progress{ "N/A" };
    std::string     m_filament_weight{ "0g" };
    std::string     m_print_total_time{ "0m" };
    std::string     m_print_png_url{ "" };
    std::string     m_status{ "standby" };
    std::string     m_print_state;
	int m_print_cur_layer{ 0 };
	int m_print_total_layer{ 0 };
	double m_print_progress_float{ 0 };         // cj_1

    std::vector<Filament> m_boxData;
	std::vector<int> m_boxTemperature;
	std::vector<int> m_boxHumidity;
    bool box_is_update;
    bool box_filament_is_update;
    bool m_is_auto_reload{ false };
    std::string m_cur_slot;
    int m_box_count{ 0 };
    std::vector<Filament> m_filamentConfig;
    static std::vector<Filament> m_general_filamentConfig;
    bool m_auto_read_rfid{ false };
    bool m_init_detect{ false };
    bool m_auto_reload_detect{ false };


    std::atomic<bool>            has_box{false};
	std::atomic<bool>            is_selected{ false };
	std::atomic<bool>            is_update{ false };
    std::atomic<bool>            is_support_mqtt{ false };
    std::atomic<bool>            is_first_connect{ true };
    std::atomic<bool>            should_stop{ false };
    std::atomic<bool>            m_is_update_box_temp{ false };
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();

    std::vector<FileInfo>    file_info {};

    bool m_is_init_filamentConfig{ false };
    static bool m_is_init_general;
	static std::mutex m_general_mtx;
	std::mutex m_config_mtx;
};


using ParameterUpdateCallback = std::function<void(const std::string& device_id)>;
using ConnectionEventCallback = std::function<void(const std::string& device_id, std::string new_status)>;
using DeviceConnectTypeUpdateCallback = std::function<void(const std::string& device_id, std::string device_ip)>;
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

    void setConnectionEventCallback(ConnectionEventCallback cb) { 
        std::lock_guard<std::mutex> lock(callback_mutex_);
        connection_event_callback_ = std::move(cb); 
    }
    void setParameterUpdateCallback(ParameterUpdateCallback cb) { 
        std::lock_guard<std::mutex> lock(callback_mutex_);
        parameter_update_callback_ = std::move(cb); 
    }
    void setDeviceConnectTypeUpdateCallback(DeviceConnectTypeUpdateCallback cb) { 
        std::lock_guard<std::mutex> lock(callback_mutex_);
        device_connect_type_update_callback_ = std::move(cb); 
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


    // WebSocket
    void onOpen(const std::string& device_id, websocketpp::connection_hdl hdl);
    void onMessage(const std::string& device_id, websocketpp::connection_hdl hdl, 
                   WebSocketClient::message_ptr msg);
    void onClose(const std::string& device_id, websocketpp::connection_hdl hdl);
    void onFail(const std::string& device_id, websocketpp::connection_hdl hdl);

    void sendSubscribeMessage(const std::string& device_id);
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
    DeviceConnectTypeUpdateCallback device_connect_type_update_callback_;
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
    void updateDeviceFileInfo(std::shared_ptr<QDSDevice>& device, const json& result);
    
    ConnectionEventCallback getConnectionEventCallback() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return connection_event_callback_;
    }
    
    ParameterUpdateCallback getParameterUpdateCallback() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return parameter_update_callback_;
    }
    
    DeviceConnectTypeUpdateCallback getDeviceConnectTypeUpdateCallback() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return device_connect_type_update_callback_;
    }
    
    DeleteDeviceIDCallback getDeleteDeviceIDCallback() {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return delete_device_id_callback_;
    }

    FileInfoUpdateCallback getFileInfoUpdateCallback(){
        std::lock_guard<std::mutex> lock(callback_mutex_);
        return file_info_update_callback_;
    }
};




}
}

#endif //QDSDEVICEMANAGER_H