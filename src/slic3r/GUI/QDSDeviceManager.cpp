#include "QDSDeviceManager.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <random>
#include "libslic3r/Utils.hpp"
#include "GUI_App.hpp"

namespace Slic3r {
namespace GUI {

std::string UrlEncodeForFilename(const std::string& input) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;
    
    for (char c : input) {
        unsigned char uc = static_cast<unsigned char>(c);

        if ((uc >= 32 && uc <= 126) && 
            uc != '%' &&
            uc != '+' &&
            uc != ' ') {
            escaped << c;
        }
        else if (c == '/') {
            escaped << c;
        }
        else {
            escaped << '%' << std::setw(2) << static_cast<int>(uc);
        }
    }
    return escaped.str();
}

namespace pt = boost::property_tree;
std::vector<QDSDevice::Filament> QDSDevice::m_general_filamentConfig;
bool QDSDevice::m_is_init_general = false;
std::mutex QDSDevice::m_general_mtx;


template<typename T>
bool is_json_type(const json& j)
{
	if constexpr (std::is_same_v<T, int> ||
		std::is_same_v<T, long> ||
		std::is_same_v<T, short>) {
		return j.is_number_integer();
	}
	else if constexpr (std::is_same_v<T, double> ||
		std::is_same_v<T, float>) {
		return j.is_number_float();
	}
	else if constexpr (std::is_same_v<T, bool>) {
		return j.is_boolean();
	}
	else if constexpr (std::is_same_v<T, std::string> ||
		std::is_same_v<T, const char*>) {
		return j.is_string();
	}
	else if constexpr (std::is_same_v<T, json>) {
		return true;
	}
	else {
		return false;
	}
}

template<typename T>
void twoStageParse1(const json& status, T& target, std::string first, std::string second, bool& is_update)
{
	if (status.contains(first) && status[first].is_object()
		&& status[first].contains(second) && is_json_type<T>(status[first][second])) {
		if (target != status[first][second].get<T>()) {
			target = status[first][second].get<T>();
            is_update = true;
		}
	}
}

//cj_1
QDSFilamentConfig::QDSFilamentConfig()
{
    init();
}

QDSFilamentConfig::~QDSFilamentConfig()
{
	
}


std::string QDSFilamentConfig::getHexCode(int index)
{
    return getData(m_colorHexCode, index);
}

int QDSFilamentConfig::getColorIndex(std::string hexCode)
{
    return getIndex(m_colorHexCode, hexCode) + 1;
}

std::string QDSFilamentConfig::getcolorDes(int index)
{
    return getData(m_colorDes, index);
}

std::string QDSFilamentConfig::getTypeName(int index)
{
    return getData(m_typeName, index);
}

int QDSFilamentConfig::getTypeNameIndex(std::string typeName)
{
    return getIndex(m_typeName, typeName);
}

std::string QDSFilamentConfig::getVendor(int index)
{
    return getData(m_vendor, index);
}

int QDSFilamentConfig::getVendorIndex(std::string vendor)
{
    return getIndex(m_vendor, vendor);

}

std::string QDSFilamentConfig::getFilamentType(int index){
    return getData(m_filament_type, index);
}

void QDSFilamentConfig::init()
{
    std::async(std::launch::async, [this]() {
		initFilamentData(m_colorHexCode);
		initFilamentData(m_colorDes);
        initTypeName();
		initFilamentData(m_vendor);
        initFilamentData(m_filament_type);
    });
}


void QDSFilamentConfig::initTypeName()
{
#if QDT_RELEASE_TO_PUBLIC
	std::string resultBody = MakerHttpHandle::getInstance().httpGetTask(Environment::TESTENV, m_typeName.path);
	if (resultBody == "") {
		return;
	}
    m_typeName.data.resize(100);
	try {
		json bodyJson = json::parse(resultBody);
		if (!bodyJson.contains("data") || !bodyJson["data"].is_array()) {
			return;
		}
		for (json data : bodyJson["data"]) {
			if (!data.contains(m_typeName.name)
				|| !data[m_typeName.name].is_string()) {
				continue;
			}
			if (!data.contains("filamentTypeId")
				|| !data["filamentTypeId"].is_number_integer()) {
				continue;
			}
            int filamentTypeId = data["filamentTypeId"].get<int>();
            if (filamentTypeId > 0) {
                m_typeName.data[filamentTypeId] = data[m_typeName.name].get<std::string>();
            }
		}

	}
	catch (...) {

	}
#endif
}

void QDSFilamentConfig::initFilamentData(FilamentData& filamentData)
{
#if QDT_RELEASE_TO_PUBLIC
	std::string resultBody = MakerHttpHandle::getInstance().httpGetTask(Environment::TESTENV, filamentData.path);
	if (resultBody == "") {
		return;
	}
	try {
		json bodyJson = json::parse(resultBody);
		if (!bodyJson.contains("data") || !bodyJson["data"].is_array()) {
			return;
		}
		for (json data : bodyJson["data"]) {
			if (!data.contains(filamentData.name)
				|| !data[filamentData.name].is_string()) {
				continue;
			}
			filamentData.data.push_back(data[filamentData.name].get<std::string>());
		}

	}
	catch (...) {

	}
#endif
}

std::string QDSFilamentConfig::getData(FilamentData filamentData, int index)
{
	if (index <0 ||index >= filamentData.data.size()) {
		return "";
	}
	return filamentData.data[index];
}

int QDSFilamentConfig::getIndex(FilamentData filementData, std::string name)
{
	int index = 0;
	for (std::string data : filementData.data) {
		if (name == data) {
			return index;
		}
		++index;
	}
	return -1;
}

QDSDevice::QDSDevice(const std::string dev_id, const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type)
    : m_id(dev_id), m_name(dev_name), m_ip(dev_ip), m_type(dev_type)
    , m_boxData(17), m_boxTemperature(4, 0.0), m_boxHumidity(4, 0)
{
    m_url = "ws://" + dev_ip + ":7125/websocket";
    last_update = std::chrono::steady_clock::now();
    
    QDSDevice::initGeneralData();
    m_filamentConfig = m_general_filamentConfig;
}

void QDSDevice::updateByJsonData(json& status)
{
	if (status.contains("print_stats") && status["print_stats"].contains("state")) {

		if (m_status != status["print_stats"]["state"].get<std::string>()) {
			is_update = true;
			m_status = status["print_stats"]["state"].get<std::string>();

            // 处于未打印状态需要自己将打印信息恢复默认值
            if (m_status == "standby") {
                m_print_progress = "N/A";
                m_print_filename = "";
                m_print_png_url = "";
                m_print_cur_layer = 0;
                m_print_total_layer = 0;
                m_print_progress_float = 0.0;
            }
		}

	}
	if (status.contains("print_stats") && status["print_stats"].contains("info")
		&& status["print_stats"]["info"].contains("total_layer") && status["print_stats"]["info"]["total_layer"].is_number_integer())
	{
		if (m_print_total_layer != status["print_stats"]["info"]["total_layer"].get<int>()) {
			is_update = true;
			m_print_total_layer = status["print_stats"]["info"]["total_layer"].get<int>();
		}
	}
	if (status.contains("print_stats") && status["print_stats"].contains("info")
		&& status["print_stats"]["info"].contains("current_layer") && status["print_stats"]["info"]["current_layer"].is_number_integer())
	{
		if (m_print_cur_layer != status["print_stats"]["info"]["current_layer"].get<int>()) {
			is_update = true;
			m_print_cur_layer = status["print_stats"]["info"]["current_layer"].get<int>();
		}
	}


	twoStageParseIntToString(status, m_print_total_duration, "print_stats", "total_duration");
	twoStageParseIntToString(status, m_print_duration, "print_stats", "print_duration");
	twoStageParseIntToString(status, m_bed_temperature, "heater_bed", "temperature");
	twoStageParseIntToString(status, m_target_bed, "heater_bed", "target");
	twoStageParseIntToString(status, m_extruder_temperature, "extruder", "temperature");
	twoStageParseIntToString(status, m_target_extruder, "extruder", "target");
	twoStageParseIntToString(status, m_chamber_temperature, "heater_generic chamber", "temperature");
	twoStageParseIntToString(status, m_target_chamber, "heater_generic chamber", "target");


	if (status.contains("print_stats") && status["print_stats"].contains("filename")) {

		if (m_print_filename != status["print_stats"]["filename"].get<std::string>()) {
			is_update = true;
			m_print_filename = status["print_stats"]["filename"].get<std::string>();
            m_print_filename = wxString::FromUTF8(m_print_filename.c_str()).ToStdString();
            
		}

	}

	if (status.contains("display_status") && status["display_status"].contains("progress")) {

		if (m_print_progress_float != status["display_status"]["progress"].get<float>()) {
			is_update = true;
			m_print_progress_float = status["display_status"]["progress"].get<float>();
		}
	}

    if (status.contains("save_variables") && status["save_variables"].is_object()) {
        updateBoxDataByJson(status);
    }

	if (status.contains("output_pin caselight") && status["output_pin caselight"].contains("value")) {

		if (m_case_light != bool(status["output_pin caselight"]["value"].get<float>())) {
			is_update = true;
            m_case_light = bool(status["output_pin caselight"]["value"].get<float>());
		}
	}


	twoStageParse(status, m_auxiliary_fan_speed, "fan_generic auxiliary_cooling_fan", "speed");
	twoStageParse(status, m_chamber_fan_speed, "fan_generic chamber_circulation_fan", "speed");
	twoStageParse(status, m_cooling_fan_speed, "fan_generic cooling_fan", "speed");
	twoStageParse(status, m_home_axes, "toolhead", "homed_axes");

	if (status.contains("output_pin Polar_cooler") && status["output_pin Polar_cooler"].contains("value")) {

		if (m_case_light != bool(status["output_pin Polar_cooler"]["value"].get<float>())) {
			is_update = true;
            m_polar_cooler = bool(status["output_pin Polar_cooler"]["value"].get<float>());
		}
	}

    for (int i = 0; i < 4; ++i) {
        std::string key = "aht20_f heater_box" + std::to_string(i + 1);
		if (status.contains(key) ) {
            if (status[key].contains("temperature")) {
                if (m_boxTemperature[i] != int(status[key]["temperature"].get<float>())) {
                    box_is_update = true;
                    m_boxTemperature[i] = int(status[key]["temperature"].get<float>());
                }
            }
			if (status[key].contains("humidity")) {
				if (m_boxHumidity[i] != status[key]["humidity"].get<int>()) {
                    box_is_update = true;
                    m_boxHumidity[i] = status[key]["humidity"].get<int>();

				}
			}
		}
    }


}

void QDSDevice::updateBoxDataByJson(const json status)
{
	json saveVariables = status["save_variables"];
	for (int i = 0; i < 17; ++i) {
		std::string serial = "slot" + std::to_string(i);
		int filamentIndex = getJsonCurStageToInt(saveVariables, "filament_" + serial);
		if (filamentIndex != -1) {
            m_boxData[i].filament_idex = filamentIndex;
			m_boxData[i].name = m_filamentConfig[filamentIndex].name;
			m_boxData[i].type = m_filamentConfig[filamentIndex].type;
		}
		int vendorIndx = getJsonCurStageToInt(saveVariables, "vendor_" + serial);
		if (vendorIndx != -1) {
			m_boxData[i].vendor = m_filamentConfig[vendorIndx].vendor;
		}

		int colorIndex = getJsonCurStageToInt(saveVariables, "color_" + serial);
		if (colorIndex != -1) {
			m_boxData[i].colorHexCode = m_filamentConfig[colorIndex].colorHexCode;

		}
		if (i < 16) {
            std::string box_stepper = "box_stepper " + serial;
            if(status.contains(box_stepper)){
                if(status[box_stepper].contains("runout_button")){
                    if(!status[box_stepper]["runout_button"].is_null()){
                        int runout_value = status[box_stepper]["runout_button"].get<int>();
                        m_boxData[i].hasMaterial = (runout_value == 0) ? 1 : 0;
						box_filament_is_update = true;

                    }
                    else {
                        //m_boxData[i].hasMaterial = false;
                    }
                }
                else {
                    //m_boxData[i].hasMaterial = false;
                }
            }
            else {
                //m_boxData[i].hasMaterial = false;
            }
		}
	}



    int isExit = getJsonCurStageToInt(saveVariables, "enable_box");
    if (isExit != -1) {
        m_boxData[16].hasMaterial = bool(isExit);
    }
	int count = getJsonCurStageToInt(saveVariables, "box_count");
	if (count != -1) {
		m_box_count = count;
	}

	if (saveVariables.contains("slot_sync") && saveVariables["slot_sync"].is_string()) {
        m_cur_slot = saveVariables["slot_sync"].get<std::string>();
	}
	
    int autoReadInt = getJsonCurStageToInt(saveVariables, "auto_read_rfid");
    if (autoReadInt != -1) {
        m_auto_read_rfid = bool(autoReadInt);
    }

    int initDetctInt = getJsonCurStageToInt(saveVariables, "auto_init_detect");
    if (initDetctInt != -1) {
        m_init_detect = bool(initDetctInt);
    }

    int autoReloadInt = getJsonCurStageToInt(saveVariables, "auto_reload_detect");
    if (autoReloadInt != -1) {
        m_auto_reload_detect = bool(autoReloadInt);
    }
    box_is_update = true;
}

void QDSDevice::updateFilamentConfig()
{
    if (!m_is_init_filamentConfig) {
       
        std::lock_guard<std::mutex> lock(m_config_mtx);
        if (m_is_init_filamentConfig) {
            return;
        }
    }
    
    auto future1 = std::async(std::launch::async, [this]() {
        std::lock_guard<std::mutex> lock(m_config_mtx);
        std::string url = m_frp_url + "/api/qidiclient/config/offical_filament_list";
        Slic3r::Http httpPost = Slic3r::Http::get(url);
        std::string resultBody;
        httpPost.timeout_max(5)
            .header("accept", "application/json")
            .header("Content-Type", "application/json")
            .on_complete(
                [&resultBody](std::string body, unsigned status) {
                    resultBody = body;
                }
            )
            .on_error(
                [this](std::string body, std::string error, unsigned status) {

                }
                ).perform_sync();
        try {
            json bodyJson = json::parse(resultBody);
            if (!bodyJson.is_object()) {
                return;
            }
            if (!bodyJson.contains("result")) {
                return;
            }
            json resultJson = bodyJson["result"];
            if (!resultJson.is_object()) {
                return;
            }
            auto parseToString = [&resultJson](std::string name, std::vector<std::string>& data) {
                if (!resultJson.contains(name) || !resultJson[name].is_object()) {
                    return;
                }
                data.resize(100);
                for (auto& element : resultJson[name].items()) {
                    std::string key = element.key();
                    int index = std::stoi(key);
                    data[index] = element.value().get<std::string>();
                }
            };
            auto parseToInt = [&resultJson](std::string name, std::vector<int>& data) {
                if (!resultJson.contains(name) || !resultJson[name].is_object()) {
                    return;
                }
                data.resize(100);
                for (auto& element : resultJson[name].items()) {
                    std::string key = element.key();
                    int index = std::stoi(key);
                    data[index] = element.value().get<int>();
                }
            };

            std::vector<std::string> names;
			std::vector<std::string> types;
			std::vector<std::string> colorHexCodes;
			std::vector<std::string> vendors;
            parseToString("filament", names);
			parseToString("type", types);
			parseToString("colordict", colorHexCodes);
			parseToString("vendor_list", vendors);
			std::vector<int> minTemps;
			std::vector<int> maxTemps;
			std::vector<int> boxMinTemps;
			std::vector<int> boxMaxTemps;
			parseToInt("min_temp", minTemps);
			parseToInt("max_temp", maxTemps);
			parseToInt("box_min_temp", boxMinTemps);
			parseToInt("box_max_temp", boxMaxTemps);

            if(minTemps.size() != m_filamentConfig.size() || maxTemps.size() != m_filamentConfig.size() || boxMinTemps.size() != m_filamentConfig.size() || boxMaxTemps.size() != m_filamentConfig.size())
                return;
            for (int i = 1; i < m_filamentConfig.size(); ++i) {
				m_filamentConfig[i].name = names[i];
				m_filamentConfig[i].type = types[i];
				m_filamentConfig[i].minTemp = minTemps[i];
				m_filamentConfig[i].maxTemp = maxTemps[i];
				m_filamentConfig[i].boxMinTemp = boxMinTemps[i];
				m_filamentConfig[i].boxMaxTemp = boxMaxTemps[i];

				m_filamentConfig[i].vendor = vendors[i];
				m_filamentConfig[i].colorHexCode = colorHexCodes[i];
            }
            m_is_init_filamentConfig = true;

        }
        catch (...) {

        }

        
	});


    
}

bool QDSDevice::is_online(){
    return m_status!= "offline";
}

void QDSDevice::twoStageParseIntToString(const json& status, std::string& target, std::string first, std::string second)
{

	if (status.contains(first) && status[first].contains(second)) {
		if (target != std::to_string(status[first][second].get<int>())) {
			target = std::to_string(status[first][second].get<int>());
			is_update = true;
		}
	}
}

void QDSDevice::twoStageParseStringToString(const json& status, std::string& target, std::string first, std::string second)
{
	if (status.contains(first) && status[first].contains(second)) {
		if (target != std::to_string(status[first][second].get<int>())) {
			target = status[first][second].get<std::string>();
			is_update = true;
		}
	}
}

template<typename T>
void QDSDevice::twoStageParse(const json& status, T& target, std::string first, std::string second)
{
    bool temp_is_update = false;
    twoStageParse1(status, target, first, second, temp_is_update);
    if (temp_is_update) {
        is_update = temp_is_update;
    }
}



int QDSDevice::getJsonCurStageToInt(const json& jsonData, std::string jsonName)
{
    if (!jsonData.contains(jsonName) || !jsonData[jsonName].is_number_integer()) {
        return -1;
    }
    return jsonData[jsonName].get<int>();
}

bool extractNumberWithSscanf(const std::string& str, int& result) {
	return (sscanf(str.c_str(), "fila%d", &result) == 1);
}

void QDSDevice::initGeneralData()
{
    if (QDSDevice::m_is_init_general) {
        return;
    }
    std::lock_guard<std::mutex> lock(QDSDevice::m_general_mtx);
	if (QDSDevice::m_is_init_general) {
		return;
	}

    QDSDevice::m_general_filamentConfig.resize(100);
    std::string cfg_path = Slic3r::resources_dir() + "/profiles/officiall_filas_list.cfg";
	pt::ptree pt;
	try {
		pt::ini_parser::read_ini(cfg_path, pt);
	}
	catch (const std::exception& e) {
		std::cerr << "Error reading config file: " << e.what() << std::endl;
		return ;
	}
	for (const auto& section : pt) {
        std::string sectionName = section.first;
        if (sectionName == "colordict") {
            for (const auto& item : section.second) {
                int index = std::stoi(item.first);
                m_general_filamentConfig[index].colorHexCode = item.second.data();
            }
        }
		if (sectionName == "vendor_list") {
			for (const auto& item : section.second) {
				int index = std::stoi(item.first);
				m_general_filamentConfig[index].vendor = item.second.data();
			}
		}

        int filaIndex = 0;
        if (!extractNumberWithSscanf(sectionName,filaIndex)) {
            continue;
        }
		for (const auto& item : section.second) {
            if (item.first == "filament") {
                m_general_filamentConfig[filaIndex].name = item.second.data();
            }
			if (item.first == "type") {
				m_general_filamentConfig[filaIndex].type = item.second.data();

			}
            if (item.first == "min_temp") {
                m_general_filamentConfig[filaIndex].minTemp = item.second.get_value<int>(0);
			}
            if (item.first == "max_temp") {
				m_general_filamentConfig[filaIndex].maxTemp = item.second.get_value<int>(0);

			}
			if (item.first == "box_min_temp") {
				m_general_filamentConfig[filaIndex].boxMinTemp = item.second.get_value<int>(0);

			}
			if (item.first == "box_max_temp") {
				m_general_filamentConfig[filaIndex].boxMaxTemp = item.second.get_value<int>(0);

			}
		}
	}
	

    QDSDevice::m_is_init_general = true;
}

QDSDeviceManager::QDSDeviceManager() {
    health_check_running_ = true;
    health_check_thread_ = std::thread(&QDSDeviceManager::healthCheckLoop, this);
}

QDSDeviceManager::~QDSDeviceManager() {

    health_check_running_ = false;
    if (health_check_thread_.joinable()) {
        health_check_thread_.join();
    }
    stopAllConnection();
}

void QDSDeviceManager::healthCheckLoop() {
    while (health_check_running_) {
        std::this_thread::sleep_for(health_check_interval_);

        if (!health_check_running_) break;

        performHealthCheck();
    }
}

void QDSDeviceManager::performHealthCheck() {
    std::vector<std::string> devices_to_reconnect;

    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto now = std::chrono::steady_clock::now();

        for (const auto& [device_id, device] : devices_) {
            bool needs_reconnect = false;
            std::string reason;

            if (device->is_selected.load()) {
                if (device->m_status == "Unauthorized")
                    continue;
                if (device->m_status == "offline" || device->m_status == "error") {
                    needs_reconnect = true;
                    reason = "status is " + device->m_status;
                }
                else if (std::chrono::duration_cast<std::chrono::seconds>(now - device->last_update).count() > 60) {
                    needs_reconnect = true;
                    reason = "no update for " +
                        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - device->last_update).count()) + " seconds";
                    std::cout << "device last update : " << (device->last_update).time_since_epoch().count() << std::endl;
                    std::cout << "now time is  : " << now.time_since_epoch().count() << std::endl;
                }
            }

            if (needs_reconnect) {
                std::cout << "[HealthCheck] Device " << device_id << "device name is " << device->m_name << " needs reconnect: " << reason << std::endl;
                devices_to_reconnect.push_back(device_id);
            }
        }
    }

    for (const auto& device_id : devices_to_reconnect) {
        reconnectDevice(device_id);
    }
}

void QDSDeviceManager::reconnectDevice(const std::string& device_id) {
    std::cout << "[HealthCheck] Reconnecting device " << device_id << "..." << std::endl;

    stopConnection(device_id);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    connectDevice(device_id);
}

int QDSDeviceManager::generateDeviceID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> distrib(1000, 9999);
    return distrib(gen);
}

std::shared_ptr<QDSDevice> QDSDeviceManager::getDevice(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(manager_mutex_);
    auto it = devices_.find(device_id);
    return (it != devices_.end()) ? it->second : nullptr;
}

std::shared_ptr<QDSDevice> QDSDeviceManager::getSelectedDevice(){
    std::lock_guard<std::mutex> lock(manager_mutex_);
    for(const auto& [device_id, device] : devices_){
        if(device->is_selected)
            return device;
    }
    return nullptr;
}

void QDSDeviceManager::stopConnection(const std::string& device_id) {
    std::shared_ptr<WebSocketConnect> conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto conn_it = connections_.find(device_id);
        if (conn_it == connections_.end()) return;
        conn = conn_it->second;
        conn->stopping = true;
    }

    int wait_count = 0;
    while (conn && conn->processing_message && wait_count < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_count++;
    }

    safeStopConnection(device_id);

    cleanupConnection(device_id);
}

void QDSDeviceManager::safeStopConnection(const std::string& device_id) {
    std::shared_ptr<WebSocketConnect> conn = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto conn_it = connections_.find(device_id);
        if (conn_it == connections_.end()) return;
        conn = conn_it->second;
    }
    
    if (!conn || !conn->running) return;
    
    try {
        websocketpp::lib::error_code ec;

        auto con = conn->client.get_con_from_hdl(conn->connection_hdl);
        if (con && con->get_state() == websocketpp::session::state::open) {
            conn->client.close(conn->connection_hdl, 
                               websocketpp::close::status::going_away, 
                               "Connection stopped by manager", ec);
            if (ec) {
                std::cout << "[Stop] Warning closing connection for device " 
                          << device_id << ": " << ec.message() << std::endl;
            }
        }
        
        conn->client.stop();
        conn->running = false;
        
    } catch (const std::exception& e) {
        std::cout << "[Stop] Exception while stopping client for device " 
                  << device_id << ": " << e.what() << std::endl;
        if (conn) {
            conn->running = false;
        }
    }
}

void QDSDeviceManager::cleanupConnection(const std::string& device_id) {
    std::shared_ptr<WebSocketConnect> conn = nullptr;

    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto conn_it = connections_.find(device_id);
        if (conn_it == connections_.end()) return;
        conn = conn_it->second;
        connections_.erase(conn_it);
    }

    std::cout << "[Manager] Starting cleanup for device " << device_id << std::endl;

    if (conn) {
        std::thread& client_thread = conn->client_thread;

        if (client_thread.joinable()) {
            try {
                if (client_thread.joinable()) {
                    client_thread.join();
                }
            }
            catch (const std::system_error& e) {
                std::cout << "[Cleanup] System error joining thread for device "
                    << device_id << ": " << e.what()
                    << " (code: " << e.code() << ")" << std::endl;
                if (e.code() == std::errc::no_such_process ||
                    e.code() == std::errc::invalid_argument) {
                    try {
                        if (client_thread.joinable()) {
                            client_thread.detach();
                        }
                    }
                    catch (...) {
                        std::cout << "[Cleanup] Failed to detach thread for device "
                            << device_id << std::endl;
                    }
                }
            }
            catch (const std::exception& e) {
                std::cout << "[Cleanup] Error joining thread for device "
                    << device_id << ": " << e.what() << std::endl;
            }
        }
    }

    std::cout << "[Manager] Device " << device_id << " connection cleaned up." << std::endl;
}

std::string QDSDeviceManager::addDevice(const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type) {
    std::string device_id;
    bool id_is_unique = false;

    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        while (!id_is_unique) {
            device_id = std::to_string(generateDeviceID());
            if (devices_.find(device_id) == devices_.end()) {
                id_is_unique = true;
            }
            else {
                std::cerr << "[Manager] Warning: Device ID " << device_id << " conflict, retrying." << std::endl;
            }
        }

        auto device = std::make_shared<QDSDevice>(device_id, dev_name, dev_ip, dev_url, dev_type);
        device->m_frp_url = "http://" + dev_ip ;
        
        
            
        
        devices_[device_id] = device;
        

        std::cout << "[Manager] Device added: " << device_id << std::endl;
    }

    std::thread([this, device_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        connectDevice(device_id);
    }).detach();

    return device_id;
}

bool QDSDeviceManager::addDevice(std::shared_ptr<QDSDevice> device)
{
	std::string device_id = device->m_id;
	std::lock_guard<std::mutex> lock(manager_mutex_);
	if (devices_.find(device_id) != devices_.end()) {
		std::cout << "device :" << device << "exit" << std::endl;
		return false;
	}
    
	devices_[device_id] = device;
	return true;
}

bool QDSDeviceManager::removeDevice(const std::string& device_id) {
    bool removed = false;
    disconnectDevice(device_id);
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        removed = devices_.erase(device_id) > 0;
    }
    if (removed) {
        auto callback = getDeleteDeviceIDCallback();
        if (callback) {
            callback(device_id);
        }
    }
    return removed;
}

bool QDSDeviceManager::connectDevice(const std::string device_id) {
    std::shared_ptr<QDSDevice> dev = getDevice(device_id);
    if (!dev) {
        std::cout << "[Connect] Error: Device " << device_id << " not found." << std::endl;
        return false;
    }

    disconnectDevice(device_id);

    std::shared_ptr<WebSocketConnect> connection = nullptr;
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        connection = std::make_shared<WebSocketConnect>();
        connection->info = dev;
        connections_[device_id] = connection;
    }

    connection->client.init_asio();
    connection->client.clear_access_channels(websocketpp::log::alevel::all);
    connection->client.clear_error_channels(websocketpp::log::elevel::all);

    std::weak_ptr<WebSocketConnect> weak_conn = connection;

    connection->client.set_open_handler([this, device_id, weak_conn](auto hdl) {
        auto conn = weak_conn.lock();
        if (conn) {
            conn->last_activity = std::chrono::steady_clock::now();
        }
        onOpen(device_id, hdl);
    });

    connection->client.set_message_handler([this, device_id, weak_conn](auto hdl, auto msg) {
        auto conn = weak_conn.lock();
        if (conn && !conn->stopping) {
            conn->processing_message = true;
            conn->message_processing_count++;
            conn->last_activity = std::chrono::steady_clock::now();
            
            try {
                onMessage(device_id, hdl, msg);
            } catch (...) {
            }
            
            conn->message_processing_count--;
            if (conn->message_processing_count == 0) {
                conn->processing_message = false;
            }
        }
    });

    connection->client.set_close_handler([this, device_id, weak_conn](auto hdl) {
        auto conn = weak_conn.lock();
        if (conn) {
            conn->last_activity = std::chrono::steady_clock::now();
        }
        std::cout << "[DEBUG] WebSocket closed for device: " << device_id << std::endl;
        std::cout << "[DEBUG] Close code: " << conn->client.get_con_from_hdl(hdl)->get_remote_close_code() << std::endl;
        std::cout << "[DEBUG] Close reason: " << conn->client.get_con_from_hdl(hdl)->get_remote_close_reason() << std::endl;

        onClose(device_id, hdl);
    });

    connection->client.set_fail_handler([this, device_id, weak_conn](auto hdl) {
        auto conn = weak_conn.lock();
        if (conn) {
            conn->last_activity = std::chrono::steady_clock::now();
        }
        onFail(device_id, hdl);
    });

    try {
        websocketpp::lib::error_code ec;
        auto con = connection->client.get_connection(dev->m_url, ec);
        if (ec) {
            std::cout << "[Connect] Connection error for device " << device_id 
                      << ": " << ec.message() << std::endl;
            updateDeviceStatus(device_id, "offline");
            
            std::lock_guard<std::mutex> lock(manager_mutex_);
            connections_.erase(device_id);
            return false;
        }
        
        connection->connection_hdl = con->get_handle();
        connection->running = true;
        
        connection->client.connect(con);
        
        connection->client_thread = std::thread([connection]() {
            try {
                connection->client.run();
            } catch (const websocketpp::exception& e) {
                std::cout << "[WebSocket] Exception in client thread: " 
                          << e.what() << std::endl;
            } catch (const std::exception& e) {
                std::cout << "[WebSocket] Exception in client thread: " 
                          << e.what() << std::endl;
            }
            connection->running = false;
        });
        
        std::cout << "[Connect] Connecting to device " << device_id 
                  << " (" << dev->m_name << ")..." << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "[Connect] Exception while connecting to device " << device_id
                  << ": " << e.what() << std::endl;
        updateDeviceStatus(device_id, "error");
        
        std::lock_guard<std::mutex> lock(manager_mutex_);
        connections_.erase(device_id);
        return false;
    }
}

bool QDSDeviceManager::disconnectDevice(const std::string& device_id) {
    stopConnection(device_id);
    return true;
}

void QDSDeviceManager::onOpen(const std::string& device_id, websocketpp::connection_hdl hdl) {
    std::cout << "[WS] Device " << device_id << " connected." << std::endl;
    processConnectionStatus(device_id, "connected");
    
    std::thread([this, device_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        sendSubscribeMessage(device_id);
    }).detach();
}
void QDSDeviceManager::onMessage(const std::string& device_id, websocketpp::connection_hdl hdl, WebSocketClient::message_ptr msg) {
    std::string msg_str;
    try {
        msg_str = msg->get_payload();

        std::shared_ptr<WebSocketConnect> conn;
        {
            std::lock_guard<std::mutex> lock(manager_mutex_);
            auto conn_it = connections_.find(device_id);
            if (conn_it == connections_.end() || conn_it->second->stopping) {
                return;
            }
        }
        
        json message_json;
        try {
            message_json = json::parse(msg_str);
        } catch (const json::parse_error& e) {
            std::cout << "[Message] JSON parse error for device " << device_id 
                      << ": " << e.what() << std::endl;
            return;
        }
        
        handleDeviceMessage(device_id, message_json);
        
    } catch (const std::exception& e) {
//         std::cout << "[Message] Error in onMessage for device " << device_id 
//                   << ": " << e.what() 
//                   << ", message length: " << msg_str.length() << std::endl;
    }
}

void QDSDeviceManager::onClose(const std::string& device_id, websocketpp::connection_hdl hdl) {
    std::cout << "[WS] Device " << device_id << " disconnected." << std::endl;
    processConnectionStatus(device_id, "offline");
}

void QDSDeviceManager::onFail(const std::string& device_id, websocketpp::connection_hdl hdl) {
    std::cout << "[WS] Device " << device_id << " connection failed." << std::endl;
    processConnectionStatus(device_id, "offline");
}

void QDSDeviceManager::sendSubscribeMessage(const std::string& device_id) {
    std::shared_ptr<WebSocketConnect> conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto conn_it = connections_.find(device_id);
        if (conn_it == connections_.end() || conn_it->second->stopping) {
            return;
        }
        conn = conn_it->second;
    }

    if (!conn) return;

    json subscribe_msg = {
        {"id", std::atoi(device_id.c_str())},
        {"method", "printer.objects.subscribe"},
        {"jsonrpc", "2.0"},
        {"params", {
            {"objects", {
            // {"gcode", nullptr},
            // {"configfile", nullptr},
            // {"mcu", nullptr},
            // {"mcu mcu_box1", nullptr},
            // {"mcu THR", nullptr},
            // {"gcode_macro _KAMP_Settings", nullptr},
            // {"gcode_macro BED_MESH_CALIBRATE", nullptr},
            // {"gcode_macro PRINTER_PARAM", nullptr},
            // {"gcode_macro _CG28", nullptr},
            // {"gcode_macro save_zoffset", nullptr},
            // {"gcode_macro set_zoffset", nullptr},
            // {"gcode_macro CLEAR_NOZZLE_PLR", nullptr},
            // {"gcode_macro CLEAR_NOZZLE", nullptr},
            // {"gcode_macro SHAKE_OOZE", nullptr},
            // {"gcode_macro MOVE_TO_TRASH", nullptr},
            // {"gcode_macro EXTRUSION_AND_FLUSH", nullptr},
            // {"gcode_macro PRINT_START", nullptr},
            // {"gcode_macro ENABLE_ALL_SENSOR", nullptr},
            // {"gcode_macro DISABLE_ALL_SENSOR", nullptr},
            // {"gcode_macro AUTOTUNE_SHAPERS", nullptr},
            // {"gcode_macro M84", nullptr},
            // {"gcode_macro DETECT_INTERRUPTION", nullptr},
            // {"gcode_macro _HOME_X", nullptr},
            // {"gcode_macro _HOME_Y", nullptr},
            // {"gcode_macro _HOME_XY", nullptr},
            // {"gcode_macro SHAPER_CALIBRATE", nullptr},
            // {"gcode_macro PRINT_END", nullptr},
            // {"gcode_macro CANCEL_PRINT", nullptr},
            // {"gcode_macro PAUSE", nullptr},
            // {"gcode_macro RESUME_PRINT", nullptr},
            // {"gcode_macro RESUME", nullptr},
            // {"gcode_macro RESUME_1", nullptr},
            // {"gcode_macro M141", nullptr},
            // {"gcode_macro M191", nullptr},
            // {"gcode_macro M106", nullptr},
            // {"gcode_macro M107", nullptr},
            // {"gcode_macro M303", nullptr},
            // {"gcode_macro M900", nullptr},
            // {"gcode_macro M290", nullptr},
            // {"gcode_macro M901", nullptr},
            // {"gcode_macro M0", nullptr},
            // {"gcode_macro M25", nullptr},
            // {"gcode_macro M4029", nullptr},
            // {"gcode_macro move_screw1", nullptr},
            // {"gcode_macro move_screw2", nullptr},
            // {"gcode_macro move_screw3", nullptr},
            // {"gcode_macro move_screw4", nullptr},
            // {"gcode_macro M4030", nullptr},
            // {"gcode_macro M4031", nullptr},
            // {"gcode_macro CUT_FILAMENT_1", nullptr},
            // {"gcode_macro M603", nullptr},
            // {"gcode_macro M604", nullptr},
            // {"gcode_move", nullptr},
            // {"gcode_macro M109", nullptr},
            // {"exclude_object", nullptr},
            // {"gcode_macro G31", nullptr},
            // {"gcode_macro G32", nullptr},
            // {"gcode_macro G29", nullptr},
            // {"gcode_macro M204", nullptr},
            // {"gcode_macro BEEP", nullptr},
            // {"gcode_macro beep_on", nullptr},
            // {"gcode_macro beep_off", nullptr},
            // {"gcode_macro LED_ON", nullptr},
            // {"gcode_macro LED_OFF", nullptr},
            // {"gcode_macro GET_TIMELAPSE_SETUP", nullptr},
            // {"gcode_macro _SET_TIMELAPSE_SETUP", nullptr},
            // {"gcode_macro TIMELAPSE_TAKE_FRAME", nullptr},
            // {"gcode_macro _TIMELAPSE_NEW_FRAME", nullptr},
            // {"gcode_macro HYPERLAPSE", nullptr},
            // {"gcode_macro TIMELAPSE_RENDER", nullptr},
            // {"gcode_macro TEST_STREAM_DELAY", nullptr},
            // {"gcode_macro save_last_file", nullptr},
            // {"gcode_macro CLEAR_LAST_FILE", nullptr},
            // {"gcode_macro LOG_Z", nullptr},
            // {"gcode_macro RESUME_INTERRUPTED", nullptr},
            // {"stepper_enable", nullptr},
            // {"motion_report", nullptr},
            // {"query_endstops", nullptr},
            // {"box_extras", nullptr},
            // {"box_stepper slot0", nullptr},
            // {"box_stepper slot1", nullptr},
            // {"box_stepper slot2", nullptr},
            // {"box_stepper slot3", nullptr},
            // {"aht20_f heater_box1", nullptr},
            // {"heater_generic heater_box1", nullptr},
            // {"temperature_sensor heater_temp_a_box1", nullptr},
            // {"temperature_sensor heater_temp_b_box1", nullptr},
            // {"box_heater_fan heater_fan_a_box1", nullptr},
            // {"box_heater_fan heater_fan_b_box1", nullptr},
            // {"controller_fan board_fan_box1", nullptr},
            // {"heaters", nullptr},
            // {"heater_air", nullptr},
            // {"gcode_macro T0", nullptr},
            // {"gcode_macro T1", nullptr},
            // {"gcode_macro T2", nullptr},
            // {"gcode_macro T3", nullptr},
            // {"gcode_macro UNLOAD_T0", nullptr},
            // {"gcode_macro UNLOAD_T1", nullptr},
            // {"gcode_macro UNLOAD_T2", nullptr},
            // {"gcode_macro UNLOAD_T3", nullptr},
            // {"gcode_macro T4", nullptr},
            // {"gcode_macro T5", nullptr},
            // {"gcode_macro T6", nullptr},
            // {"gcode_macro T7", nullptr},
            // {"gcode_macro UNLOAD_T4", nullptr},
            // {"gcode_macro UNLOAD_T5", nullptr},
            // {"gcode_macro UNLOAD_T6", nullptr},
            // {"gcode_macro UNLOAD_T7", nullptr},
            // {"gcode_macro T8", nullptr},
            // {"gcode_macro T9", nullptr},
            // {"gcode_macro T10", nullptr},
            // {"gcode_macro T11", nullptr},
            // {"gcode_macro UNLOAD_T8", nullptr},
            // {"gcode_macro UNLOAD_T9", nullptr},
            // {"gcode_macro UNLOAD_T10", nullptr},
            // {"gcode_macro UNLOAD_T11", nullptr},
            // {"gcode_macro T12", nullptr},
            // {"gcode_macro T13", nullptr},
            // {"gcode_macro T14", nullptr},
            // {"gcode_macro T15", nullptr},
            // {"gcode_macro UNLOAD_T12", nullptr},
            // {"gcode_macro UNLOAD_T13", nullptr},
            // {"gcode_macro UNLOAD_T14", nullptr},
            // {"gcode_macro UNLOAD_T15", nullptr},
            // {"gcode_macro UNLOAD_FILAMENT", nullptr},
            // {"pause_resume", nullptr},
            // {"filament_switch_sensor filament_switch_sensor", nullptr},
            // {"bed_screws", nullptr},
            // {"tmc2209 extruder", nullptr},
            // {"z_tilt", nullptr},
            // {"tmc2240 stepper_x", nullptr},
            // {"tmc2240 stepper_y", nullptr},
            // {"tmc2209 stepper_z1", nullptr},
            // {"tmc2209 stepper_z", nullptr},
            // {"temperature_sensor Chamber_Thermal_Protection_Sensor", nullptr},
             {"fan_generic chamber_circulation_fan", nullptr},
            // {"controller_fan chamber_fan", nullptr},
            // {"heater_fan hotend_fan", nullptr},
             {"fan_generic cooling_fan", nullptr},
            // {"controller_fan board_fan", nullptr},
             {"fan_generic auxiliary_cooling_fan", nullptr},
             {"output_pin Polar_cooler", nullptr},
            // {"output_pin beeper", nullptr},
            // {"probe", nullptr},
            // {"probe_air", nullptr},
            // {"bed_mesh", nullptr},
            // {"idle_timeout", nullptr},
            // {"system_stats", nullptr},
            // {"manual_probe", nullptr},
            {"print_stats", nullptr},
            {"display_status", nullptr},
            // {"webhooks", nullptr},
            // {"virtual_sdcard", nullptr},
            // {"toolhead", nullptr},
            {"heater_bed", nullptr},
            {"extruder", nullptr},
            {"heater_generic chamber", nullptr},
            {"output_pin caselight", nullptr},
            {"save_variables", nullptr},
			{ "aht20_f heater_box1",nullptr },
			{ "aht20_f heater_box2",nullptr },
			{ "aht20_f heater_box3",nullptr },
			{ "aht20_f heater_box4",nullptr },
            {"box_stepper slot0", nullptr},
            {"box_stepper slot1", nullptr},
            {"box_stepper slot2", nullptr},
            {"box_stepper slot3", nullptr},
            {"box_stepper slot4", nullptr},
            {"box_stepper slot5", nullptr},
            {"box_stepper slot6", nullptr},
            {"box_stepper slot7", nullptr},
            {"box_stepper slot8", nullptr},
            {"box_stepper slot9", nullptr},
            {"box_stepper slot10", nullptr},
            {"box_stepper slot11", nullptr},
            {"box_stepper slot12", nullptr},
            {"box_stepper slot13", nullptr},
            {"box_stepper slot14", nullptr},
            {"box_stepper slot15", nullptr},
            {"box_stepper slot16", nullptr}
        }}
    }}
    };


    
    try {
        websocketpp::lib::error_code ec;
        conn->client.send(conn->connection_hdl, 
                          subscribe_msg.dump(), 
                          websocketpp::frame::opcode::text, 
                          ec);
        if (ec) {
            std::cout << "[Send] Error sending subscribe to " << device_id 
                      << ": " << ec.message() << std::endl;
        } else {
            std::cout << "[Send] Sent subscribe message to " << device_id << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "[Send] Exception sending to device " << device_id 
                  << ": " << e.what() << std::endl;
    }


    subscribe_msg = {
       {"id", std::atoi(device_id.c_str())},
       {"method", "server.files.get_directory"},
       {"jsonrpc", "2.0"},
       {"params", {
           {"root", "gcodes"},
           {"path", "gcodes"},
           {"extended", true}
       }}
    };


    try {
       websocketpp::lib::error_code ec;
       conn->client.send(conn->connection_hdl, 
                         subscribe_msg.dump(), 
                         websocketpp::frame::opcode::text, 
                         ec);
       if (ec) {
           std::cout << "[Send] Error sending subscribe to " << device_id 
                     << ": " << ec.message() << std::endl;
       } else {
           std::cout << "[Send] Sent subscribe message to " << device_id << std::endl;
       }
    } catch (const std::exception& e) {
       std::cout << "[Send] Exception sending to device " << device_id 
                 << ": " << e.what() << std::endl;
    }

}

void QDSDeviceManager::sendCommand(const std::string& device_id, const std::string& script){
    std::shared_ptr<WebSocketConnect> conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto conn_it = connections_.find(device_id);
        if (conn_it == connections_.end() || conn_it->second->stopping) {
            return;
        }
        conn = conn_it->second;
    }

    if (!conn) return;

    json subscribe_msg = {
        {"id", std::atoi(device_id.c_str())},
        {"method", "printer.gcode.script"},
        {"jsonrpc", "2.0"},
        {"params", {
            {"script", script}
        }}
    };

    try {
        websocketpp::lib::error_code ec;
        conn->client.send(conn->connection_hdl, 
                          subscribe_msg.dump(), 
                          websocketpp::frame::opcode::text, 
                          ec);
        if (ec) {
            std::cout << "[Send] Error sending subscribe to " << device_id 
                      << ": " << ec.message() << std::endl;
        } else {
            std::cout << "[Send] Sent subscribe message to " << device_id << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "[Send] Exception sending to device " << device_id 
                  << ": " << e.what() << std::endl;
    }
}

void QDSDeviceManager::sendActionCommand(const std::string& device_id, const std::string& action_type){
    std::shared_ptr<WebSocketConnect> conn = nullptr;
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto conn_it = connections_.find(device_id);
        if (conn_it == connections_.end() || conn_it->second->stopping) {
            return;
        }
        conn = conn_it->second;
    }

    if (!conn) return;

    std::string script = "printer.print.";
    json subscribe_msg = {
        {"id", std::atoi(device_id.c_str())},
        {"method", script + action_type},
        {"jsonrpc", "2.0"}
    };

    try {
        websocketpp::lib::error_code ec;
        conn->client.send(conn->connection_hdl, 
                          subscribe_msg.dump(), 
                          websocketpp::frame::opcode::text, 
                          ec);
        if (ec) {
            std::cout << "[Send] Error sending subscribe to " << device_id 
                      << ": " << ec.message() << std::endl;
        } else {
            std::cout << "[Send] Sent subscribe message to " << device_id << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "[Send] Exception sending to device " << device_id 
                  << ": " << e.what() << std::endl;
    }
}

void QDSDeviceManager::handleDeviceMessage(const std::string& device_id, const json& message) {
    std::shared_ptr<QDSDevice> device = getDevice(device_id);
    if (!device) {
        return;
    }

    if (device->should_stop.load()) {
        std::thread([this, device_id]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            removeDevice(device_id);
        }).detach();
        return;
    } else if(!device->is_first_connect.load() && !device->is_selected.load()){
        std::thread([this, device_id]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            stopConnection(device_id);
        }).detach();
    }

    updateDeviceMsg(device_id, message);
}

void QDSDeviceManager::updateDeviceMsg(const std::string& device_id, const json& message) {
    std::string new_status;
    bool is_update = false;
    bool is_file_info_update = false;
    std::shared_ptr<QDSDevice> device = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto dev_it = devices_.find(device_id);
        if (dev_it == devices_.end()) {
            return;
        }
        device = dev_it->second;
        
        if(!device->is_first_connect.load() && !device->is_selected.load())
            return;

        if (message.contains("method") && message.contains("params")) {
            if (message.at("method").get<std::string>() == "notify_proc_stat_update" && 
                device->is_first_connect && 
                message.at("params").is_array()) {
                
                const json& result = message.at("params").at(0);

                if(result.contains("config_items")){
                    device->m_polar_cooler = result["config_items"]["printing.polar_cooler"].get<std::string>() == "1" ? true : false;
                }

                if (result.contains("mqtt_state")) {
                    device->is_first_connect = false;
                    device->is_support_mqtt = true;
                    auto callback = getDeviceConnectTypeUpdateCallback();
                    if (callback) {
                        callback(device_id, device->m_ip);
                    }
                } else {
                    device->is_first_connect = false;
                    device->is_support_mqtt = false;
                }
            }
        }
        
        if (message.contains("result")) {
            if(message.at("result").contains("files")){
                const json& result = message.at("result");
                updateDeviceFileInfo(device, result);
                //is_file_info_update = true;
            }
            if(message.at("result").contains("status")){
                const json& result = message.at("result").at("status");
                updateDeviceData(device, result, new_status, is_update);
            }
        }

        
        if(message.contains("method")){
            if(message.at("method").get<std::string>() == "notify_history_changed"){
                updatePrintThumbUrl(device, message);
            }
        }

        // if(message.contains("method")){
        //     if(message.at("method").get<std::string>() == "notify_filelist_changed"){

        //     }
        // }
        
        if (message.contains("method") && 
            message.at("method").get<std::string>() == "notify_status_update" && 
            message.at("params").is_array() && 
            !message.at("params").empty()) {
            
            const json& result = message.at("params").at(0);
            updateDeviceData(device, result, new_status, is_update);
        }
        
        if (message.contains("error") && 
            message["error"].contains("message") &&
            message["error"]["message"] == "Unauthorized") {
            device->m_status = "Unauthorized";
            new_status = "Unauthorized";
        }
        
        if (is_update) {
            device->last_update = std::chrono::steady_clock::now();
        }
    }
    
    if (!new_status.empty()) {
        updateDeviceStatus(device_id, new_status);
    }
    
    if (is_update) {
        auto callback = getParameterUpdateCallback();
        if (callback) {
            callback(device_id);
        }
    }

    if(is_file_info_update){
        auto callback = getFileInfoUpdateCallback();
        if(callback)
            callback(device_id);
    }
    
    if (device && !device->is_first_connect && !device->is_support_mqtt) {
        device->should_stop = true;
    }
}

void QDSDeviceManager::updateDeviceData(std::shared_ptr<QDSDevice>& device, 
                                        const json& result, 
                                        std::string& new_status, 
                                        bool& is_update) {
    if (result.contains("print_stats") && 
        result["print_stats"].contains("state")) {
        std::string status = result["print_stats"]["state"].get<std::string>();
        if (status != device->m_status) {
            device->m_status = status;
            new_status = status;
        }
    }

    if(result.contains("save_variables")){
        device->updateBoxDataByJson(result);
    }
    
    if (result.contains("heater_bed")){
        if(result["heater_bed"].contains("temperature")) {
            device->m_bed_temperature = std::to_string(
                result["heater_bed"]["temperature"].get<int>());
            is_update = true;
        }
        if(result["heater_bed"].contains("target")) {
            device->m_target_bed = std::to_string(
                result["heater_bed"]["target"].get<int>());
            is_update = true;
        }
    }
    
    if (result.contains("extruder")){
        if(result["extruder"].contains("temperature")) {
            device->m_extruder_temperature = std::to_string(
                result["extruder"]["temperature"].get<int>());
            is_update = true;
        }
        if(result["extruder"].contains("target")) {
            device->m_target_extruder = std::to_string(
                result["extruder"]["target"].get<int>());
            is_update = true;
        }
    }
    
    if (result.contains("heater_generic chamber")){ 
        if(result["heater_generic chamber"].contains("temperature")) {
            device->m_chamber_temperature = std::to_string(
                result["heater_generic chamber"]["temperature"].get<int>());
            is_update = true;
        }
        if(result["heater_generic chamber"].contains("target")) {
            device->m_target_chamber = std::to_string(
                result["heater_generic chamber"]["target"].get<int>());
            is_update = true;
        }
    }
    
    if (result.contains("display_status") && 
        result["display_status"].contains("progress")) {
        device->m_print_progress = std::to_string(
            result["display_status"]["progress"].get<int>());
        is_update = true;
    }

    if(result.contains("output_pin caselight")){
        device->m_case_light = result["output_pin caselight"]["value"].get<double>() > 0 ? true : false;
        is_update = true;
    }

	for (int i = 0; i < 4; ++i) {
		std::string key = "aht20_f heater_box" + std::to_string(i + 1);
		if (result.contains(key)) {
			if (result[key].contains("temperature")) {

				if (device->m_boxTemperature[i] != int(result[key]["temperature"].get<float>())) {
					device->box_is_update = true;
					device->m_boxTemperature[i] = int(result[key]["temperature"].get<float>());
				}
			}
			if (result[key].contains("humidity")) {
				if (device->m_boxHumidity[i] != result[key]["humidity"].get<int>()) {
					device->box_is_update = true;
					device->m_boxHumidity[i] = result[key]["humidity"].get<int>();

				}
			}
		}
	}

	twoStageParse1(result, device->m_auxiliary_fan_speed, "fan_generic auxiliary_cooling_fan", "speed",is_update);
	twoStageParse1(result, device->m_chamber_fan_speed, "fan_generic chamber_circulation_fan", "speed", is_update);
	twoStageParse1(result, device->m_cooling_fan_speed, "fan_generic cooling_fan", "speed", is_update);

    if (result.contains("print_stats")){ 
         if(result["print_stats"].contains("filename")) {
             if (device->m_print_filename != result["print_stats"]["filename"].get<std::string>()) {
                 is_update = true;
                 device->m_print_filename = result["print_stats"]["filename"].get<std::string>();
             }
         }
     }

    if (result.contains("display_status")){
        if(result["display_status"].contains("progress")) {
            if (device->m_print_progress_float != result["display_status"]["progress"].get<float>()) {
                is_update = true;
                device->m_print_progress_float = result["display_status"]["progress"].get<float>();
            }
        }
	}

    if (result.contains("print_stats")){
        if (result["print_stats"].contains("info")) {
            if (result["print_stats"]["info"].contains("total_layer") && result["print_stats"]["info"]["total_layer"].is_number_integer()) {
                if (device->m_print_total_layer != result["print_stats"]["info"]["total_layer"].get<int>()) {
                    is_update = true;
                    device->m_print_total_layer = result["print_stats"]["info"]["total_layer"].get<int>();
                }
            }
        }
	}

	if (result.contains("print_stats")){
        if(result["print_stats"].contains("info")
		&& result["print_stats"]["info"].contains("current_layer")){
            if (device->m_print_cur_layer != result["print_stats"]["info"]["current_layer"].get<int>()) {
                is_update = true;
                device->m_print_cur_layer = result["print_stats"]["info"]["current_layer"].get<int>();
            }
        }
	}

    if(result.contains("print_stats")){
        if(result["print_stats"].contains("print_duration")) {
            if (device->m_print_duration != std::to_string(result["print_stats"]["print_duration"].get<int>())) {
                is_update = true;
                device->m_print_duration = std::to_string(result["print_stats"]["print_duration"].get<int>());
                if(device->m_print_png_url.empty())
                    updatePrintThumbUrlWithOutMsg(device);
            }
        }
    }

    if(result.contains("print_stats")){
        if(result["print_stats"].contains("total_duration")) {
            if (device->m_print_total_duration != std::to_string(result["print_stats"]["total_duration"].get<int>())) {
                is_update = true;
                device->m_print_total_duration = std::to_string(result["print_stats"]["total_duration"].get<int>());
            }
        }
    }


}

void QDSDeviceManager::updateDeviceFileInfo(std::shared_ptr<QDSDevice>& device, const json& result){
    device->file_info.clear();
    auto files = result.at("files");
    for(auto file : files){
       FileInfo file_info;
       file_info.file_name = file["filename"];
       file_info.filament_weight = std::to_string(file["filament_weight_total"].get<float>());
       file_info.print_time = get_qdt_monitor_time_dhm(file["estimated_time"].get<float>());
       file_info.modified_time = file["modified"];
       file_info.thumb_url = UrlEncodeForFilename(file["thumbnails"][0]["relative_path"]);
       device->file_info.emplace_back(file_info);
    }
}

void QDSDeviceManager::updatePrintThumbUrl(std::shared_ptr<QDSDevice>& device, const json& message){
    const json& result = message.at("params")[0];
    if(result["action"] == "added"){
        device->m_print_filename = result["job"]["filename"];
        std::string thumb_path = result["job"]["metadata"]["thumbnails"]["relative_path"];
        device->m_print_png_url = device->m_frp_url + "/server/files/gcodes/" + thumb_path;
    } else if(result["action"] == "finished"){
        device->m_print_filename = "";
        device->m_print_png_url = "";
    }
}

void QDSDeviceManager::updatePrintThumbUrlWithOutMsg(std::shared_ptr<QDSDevice>& device){
    if(!device->file_info.empty() && device->m_print_png_url.empty()){
        std::string print_file_name = device->m_print_filename;
        std::vector<FileInfo> files_info = device->file_info;
        for(auto file_ : files_info){
            if(file_.file_name == print_file_name)
                device->m_print_png_url = device->m_frp_url + "/server/files/gcodes/" + file_.thumb_url;
        }
    }
}

void QDSDeviceManager::updateDeviceStatus(const std::string& device_id, std::string new_status) {
    bool should_callback = false;

    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto dev_it = devices_.find(device_id);
        if (dev_it != devices_.end()) {
            if (!new_status.empty()) {
                dev_it->second->m_status = new_status;
                should_callback = true;
            }
        }
    }

    if (should_callback) {
        auto callback = getConnectionEventCallback();
        if (callback) {
            callback(device_id, new_status);
        }
    }
}

void QDSDeviceManager::processConnectionStatus(const std::string& device_id, 
                                               const std::string& status) {
    updateDeviceStatus(device_id, status);
}

void QDSDeviceManager::stopAllConnection() {
    std::vector<std::string> device_ids;
    
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        if (connections_.empty()) return;
        
        for (const auto& pair : connections_) {
            device_ids.push_back(pair.first);
        }
    }
    
    for (const auto& device_id : device_ids) {
        disconnectDevice(device_id);
    }
    
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        devices_.clear();
    }
}

std::string QDSDeviceManager::getDeviceTempNozzle(const std::string& deviceId)
{
    std::lock_guard<std::mutex> lock(manager_mutex_);
    auto dev_it = devices_.find(deviceId);
    return (dev_it != devices_.end()) ? dev_it->second->m_extruder_temperature : "0.0";
}

std::string QDSDeviceManager::getDeviceTempBed(const std::string& deviceId)
{
    std::lock_guard<std::mutex> lock(manager_mutex_);
    auto dev_it = devices_.find(deviceId);
    return (dev_it != devices_.end()) ? dev_it->second->m_bed_temperature : "0.0";
}

std::string QDSDeviceManager::getDeviceTempChamber(const std::string& deviceId)
{
    std::lock_guard<std::mutex> lock(manager_mutex_);
    auto dev_it = devices_.find(deviceId);
    return (dev_it != devices_.end()) ? dev_it->second->m_chamber_temperature : "0.0";
}

void QDSDeviceManager::setSelected(const std::string& device_id){
    std::shared_ptr<QDSDevice> device = nullptr;
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        for (auto [device_id, device] : devices_) {
            device->is_selected = false;
        }

        auto dev_it = devices_.find(device_id);
        if (dev_it == devices_.end()) {
            return;
        }
        device = dev_it->second;
        device->is_selected = true;
    }
}

void QDSDeviceManager::unSelected(){
    std::lock_guard<std::mutex> lock(manager_mutex_);
    for (auto [device_id, device] : devices_) {
        device->is_selected = false;
    }
}

//y76
#if QDT_RELEASE_TO_PUBLIC
void QDSDeviceManager::setNetDevices(std::vector<NetDevice> devices){
    std::lock_guard<std::mutex> lock(manager_mutex_);
    net_devices = devices;
}

std::vector<NetDevice> QDSDeviceManager::getNetDevices(){
    std::lock_guard<std::mutex> lock(manager_mutex_);
    return net_devices;
}
#endif

void QDSDeviceManager::upBoxInfoToBoxMsg(std::shared_ptr<QDSDevice>& device){
    std::vector<int> slot_state(17);
    std::vector<int> slot_id(17);
    std::vector<std::string> filament_id(17);
    std::vector<std::string> filament_colors(17);
    std::vector<std::string> filament_type(17);
    std::string box_list_preset_name;
    int box_count = 0;
    int auto_reload_detect = 0;

    if(device == nullptr)
        return;
    
    std::unordered_map<std::string, std::string> mapping = {
        {"X-Plus 4", "0"},
        {"Q2", "1"},
        {"Q2C", "2"},
        {"X-Max 4", "3"}
    };
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        GUI::wxGetApp().sidebar().update_sync_status(device);
        for(int i = 0; i < 17; ++i){
            if(device->m_boxData[i].hasMaterial){
                slot_state[i] = device->m_boxData[i].hasMaterial;
                slot_id[i] = i;
                filament_type[i] = device->m_boxData[i].type;
                filament_colors[i] = device->m_boxData[i].colorHexCode;
                
                std::string slot_vendor = device->m_boxData[i].vendor;

                std::string test_type = mapping[device->m_type];
                std::string test_vendor = slot_vendor == "QIDI" ? "1" : "0";
                std::string tset_idx = std::to_string(device->m_boxData[i].filament_idex);
                std::string test_id = "QD_" + test_type + "_" +  test_vendor + "_" + tset_idx;
                filament_id[i] = test_id;
            }
        }
        box_count = device->m_box_count;
        auto_reload_detect = device->m_auto_reload_detect;
        box_list_preset_name = device->m_type;
        wxGetApp().plater()->sidebar().box_list_printer_ip = device->m_ip;
    }

    wxGetApp().plater()->box_msg.slot_state = slot_state;
    wxGetApp().plater()->box_msg.filament_id = filament_id;
    wxGetApp().plater()->box_msg.filament_colors = filament_colors;
    wxGetApp().plater()->box_msg.box_count = box_count;
    wxGetApp().plater()->box_msg.filament_type = filament_type;
    wxGetApp().plater()->box_msg.slot_id = slot_id;
    wxGetApp().plater()->box_msg.auto_reload_detect = auto_reload_detect;
    wxGetApp().plater()->box_msg.box_list_preset_name = box_list_preset_name;

    GUI::wxGetApp().sidebar().load_box_list();
}

}}