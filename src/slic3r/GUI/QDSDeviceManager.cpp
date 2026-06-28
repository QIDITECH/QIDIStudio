#include "QDSDeviceManager.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/asio.hpp>
#include <random>
#include "libslic3r/Utils.hpp"
#include "GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/Udp.hpp"
#include "DownloadManager.hpp"
#include "GUI_Utils.hpp"
#include "DeviceCore/DevDefs.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <wx/datetime.h>

//cj_2
#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#endif

//cj_2
#include <wx/image.h>


namespace Slic3r {
namespace GUI {

//cj_3
namespace {

static void apply_gcode_move_speed_percent(QDSDevice& dev, const json& status, bool* out_update = nullptr)
{
    try {
        if (!status.contains("gcode_move") || !status["gcode_move"].is_object())
            return;
        const auto& gm = status["gcode_move"];
        if (!gm.contains("speed_factor") || !gm["speed_factor"].is_number())
            return;
        const double sf = gm["speed_factor"].get<double>();
        const int    pct = Slic3r::dev_speed_factor_to_snapped_percent(sf);
        if (dev.m_print_speed_display_percent != pct) {
            dev.m_print_speed_display_percent = pct;
            if (out_update)
                *out_update = true;
            else
                dev.is_update = true;
        }
    } catch (...) {
    }
}

std::string format_timelapse_file_size_b_kb_mb(std::uint64_t bytes)
{
    constexpr std::uint64_t k_kb = 1024;
    constexpr std::uint64_t k_mb = 1024ULL * 1024ULL;
    std::ostringstream oss;
    oss << std::fixed;
    if (bytes >= k_mb) {
        oss << std::setprecision(2) << (static_cast<double>(bytes) / static_cast<double>(k_mb)) << "MB";
        return oss.str();
    }
    if (bytes >= k_kb) {
        oss << std::setprecision(2) << (static_cast<double>(bytes) / static_cast<double>(k_kb)) << "KB";
        return oss.str();
    }
    oss << std::setprecision(0) << bytes << "B";
    return oss.str();
}
} // namespace

namespace pt = boost::property_tree;

//cj_5
LocalDeviceDiscovery::Snapshot LocalDeviceDiscovery::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Snapshot devices;
    devices.reserve(m_by_ip.size());
    for (const auto& item : m_by_ip) {
        devices.push_back(item.second);
    }
    return devices;
}

//cj_5
bool LocalDeviceDiscovery::findBySerial(const std::string& serial, LocalDiscoveredDevice& out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_by_serial.find(serial);
    if (it == m_by_serial.end()) {
        return false;
    }
    out = it->second;
    return true;
}

//cj_5
bool LocalDeviceDiscovery::isCacheFresh(std::chrono::seconds ttl) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_last_refresh == std::chrono::steady_clock::time_point{}) {
        return false;
    }
    return std::chrono::steady_clock::now() - m_last_refresh <= ttl;
}

//cj_5
void LocalDeviceDiscovery::refresh(bool force, RefreshCallback callback)
{
    Snapshot cached_devices;
    bool use_cache = false;
    bool start_lookup = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool fresh = m_last_refresh != std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() - m_last_refresh <= m_cache_ttl;

        if (!force && fresh && !m_refreshing) {
            cached_devices.reserve(m_by_ip.size());
            for (const auto& item : m_by_ip) {
                cached_devices.push_back(item.second);
            }
            use_cache = true;
        }
        else {
            if (callback) {
                m_pending_callbacks.push_back(std::move(callback));
            }
            if (!m_refreshing) {
                m_refreshing = true;
                start_lookup = true;
                m_by_serial.clear();
                m_by_ip.clear();
            }
        }
    }

    if (use_cache) {
        if (callback) {
            callback(std::move(cached_devices));
        }
        return;
    }

    if (!start_lookup) {
        return;
    }

    Udp::TxtKeys udp_txt_keys{ "version", "model" };
    Udp::Ptr udp = Udp("octoprint")
        .set_txt_keys(std::move(udp_txt_keys))
        .set_retries(3)
        .set_timeout(4)
        .on_udp_reply([this](UdpReply&& reply) {
            LocalDiscoveredDevice device;
            device.serial_number = reply.serial_number;
            device.ip = reply.service_name;
            device.name = reply.hostname;
            device.model = reply.model_name;
            device.raw_payload = reply.raw_payload;
            device.legacy_device = reply.legacy_device;
            device.last_seen = std::chrono::steady_clock::now();

            mergeDevice(std::move(device));
        })
        .on_complete([this]() {
            finishRefresh();
        })
        .lookup();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_udp = std::move(udp);
    }
}

//cj_5
void LocalDeviceDiscovery::mergeDevice(LocalDiscoveredDevice device)
{
    if (device.ip.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    device.last_seen = std::chrono::steady_clock::now();
    if (!device.serial_number.empty()) {
        m_by_serial[device.serial_number] = device;
    }
    m_by_ip[device.ip] = std::move(device);
}

//cj_5
void LocalDeviceDiscovery::finishRefresh()
{
    std::vector<RefreshCallback> callbacks;
    Snapshot devices;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_refreshing = false;
        m_last_refresh = std::chrono::steady_clock::now();
        callbacks = std::move(m_pending_callbacks);
        m_pending_callbacks.clear();

        devices.reserve(m_by_ip.size());
        for (const auto& item : m_by_ip) {
            devices.push_back(item.second);
        }
    }

    for (auto& callback : callbacks) {
        if (callback) {
            callback(devices);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// SSDPDiscovery implementation                                     //cj_5
// ─────────────────────────────────────────────────────────────────
namespace asio = boost::asio;
using asio::ip::udp;

namespace {

const char*            SSDP_MCAST_ADDR   = "239.255.255.250";
const unsigned short   SSDP_MCAST_PORT   = 5863;
const int              SSDP_TIMEOUT_SEC  = 10;
const int              SSDP_RETRIES      = 2;

std::string build_msearch()
{
    std::ostringstream oss;
    oss << "M-SEARCH * HTTP/1.1\r\n"
        << "HOST: " << SSDP_MCAST_ADDR << ":" << SSDP_MCAST_PORT << "\r\n"
        << "MAN: \"ssdp:discover\"\r\n"
        << "ST: ssdp:all\r\n"
        << "MX: " << SSDP_TIMEOUT_SEC << "\r\n"
        << "\r\n";
    return oss.str();
}

bool parse_ssdp_notify(const std::string& raw, LocalDiscoveredDevice& out)
{
    std::istringstream stream(raw);
    std::string line;
    std::unordered_map<std::string, std::string> headers;

    if (!std::getline(stream, line))
        return false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty()) continue;

        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        if (!val.empty() && val.front() == ' ') val.erase(0, 1);
        if (!val.empty() && val.back() == '\r') val.pop_back();
        headers[key] = val;
    }

    auto usn_it = headers.find("USN");
    auto loc_it = headers.find("Location");
    if (usn_it == headers.end() || loc_it == headers.end())
        return false;

    out.serial_number = usn_it->second;

    std::string loc = loc_it->second;
    if (loc.find("http://") == 0)      loc = loc.substr(7);
    else if (loc.find("https://") == 0) loc = loc.substr(8);
    const size_t slash = loc.find('/');
    if (slash != std::string::npos) loc = loc.substr(0, slash);
    const size_t colon2 = loc.find(':');
    if (colon2 != std::string::npos) loc = loc.substr(0, colon2);
    out.ip = loc;

    auto model_it = headers.find("DevModel.qidi.com");
    if (model_it != headers.end()) out.model = model_it->second;

    auto name_it = headers.find("DevName.qidi.com");
    if (name_it != headers.end()) out.name = name_it->second;

    out.raw_payload = raw;
    out.last_seen    = std::chrono::steady_clock::now();
    out.legacy_device = false;
    return true;
}

} // anonymous namespace

struct SSDPDiscovery::priv
{
    std::shared_ptr<asio::io_context> io_ctx;
    std::unique_ptr<std::thread>       io_thread;
    std::atomic<bool>                  stopping{ false };

    priv() : io_ctx(std::make_shared<asio::io_context>()) {}
};

SSDPDiscovery::SSDPDiscovery()
    : p(std::make_unique<priv>())
{
}

SSDPDiscovery::~SSDPDiscovery()
{
    stop();
}

void SSDPDiscovery::stop()
{
    p->stopping = true;
    if (p->io_ctx) p->io_ctx->stop();
    if (p->io_thread && p->io_thread->joinable())
        p->io_thread->join();
}

bool SSDPDiscovery::isCacheFresh(std::chrono::seconds ttl) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_last_refresh == std::chrono::steady_clock::time_point{})
        return false;
    return std::chrono::steady_clock::now() - m_last_refresh <= ttl;
}

SSDPDiscovery::Snapshot SSDPDiscovery::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Snapshot devices;
    devices.reserve(m_by_ip.size());
    for (const auto& item : m_by_ip)
        devices.push_back(item.second);
    return devices;
}

bool SSDPDiscovery::findBySerial(const std::string& serial,
                                 LocalDiscoveredDevice& out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_by_serial.find(serial);
    if (it == m_by_serial.end()) return false;
    out = it->second;
    return true;
}

//cj_5
bool SSDPDiscovery::findByIP(const std::string& ip,
                              LocalDiscoveredDevice& out) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_by_ip.find(ip);
    if (it != m_by_ip.end()) {
        out = it->second;
        return true;
    }
    
    return false;
}

void SSDPDiscovery::mergeDevice(LocalDiscoveredDevice device)
{
    if (device.ip.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    device.last_seen = std::chrono::steady_clock::now();
    if (!device.serial_number.empty())
        m_by_serial[device.serial_number] = device;
    m_by_ip[device.ip] = std::move(device);
}

void SSDPDiscovery::finishRefresh()
{
    std::vector<RefreshCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_refreshing = false;
        m_last_refresh = std::chrono::steady_clock::now();
        callbacks = std::move(m_pending_callbacks);
        m_pending_callbacks.clear();
    }
    Snapshot devices = snapshot();
    BOOST_LOG_TRIVIAL(trace)
        << "[SSDP] discovery finished, found " << devices.size() << " device(s)";
    for (auto& cb : callbacks) {
        if (cb) cb(devices);
    }
}

void SSDPDiscovery::refresh(bool force, RefreshCallback callback)
{
    
    Snapshot cached_devices;
    bool use_cache = false;
    bool start_lookup = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const bool fresh = m_last_refresh != std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() - m_last_refresh <= m_cache_ttl;

        if (!force && fresh && !m_refreshing) {
            cached_devices.reserve(m_by_ip.size());
            for (const auto& item : m_by_ip)
                cached_devices.push_back(item.second);
            use_cache = true;
        } else {
            if (callback) m_pending_callbacks.push_back(std::move(callback));
            if (!m_refreshing) {
                m_refreshing  = true;
                start_lookup  = true;
                m_by_serial.clear();
                m_by_ip.clear();
            }
        }
    }

    if (use_cache) {
        if (callback) callback(std::move(cached_devices));
        return;
    }
    if (!start_lookup) return;

    //cj_5 Stop previous lookup thread before starting a new one, otherwise
    // std::thread destructor will call std::terminate on a joinable thread.
    stop();
    p->stopping = false;

    p->io_thread = std::make_unique<std::thread>([this]() {
        try {
            //cj_5 Re-create io_context each refresh so previous async ops are
            // fully cancelled after stop().
            p->io_ctx = std::make_shared<asio::io_context>();
            auto& io = *p->io_ctx;

            auto recv_buf = std::make_shared<std::vector<char>>(8192);
            const std::string msearch = build_msearch();

            // Receive loop – re-arms until error or stopping.
            std::function<void(udp::socket*)> start_receive;
            start_receive = [this, recv_buf, &start_receive](udp::socket* sock) {
                sock->async_receive(
                    asio::buffer(*recv_buf),
                    [this, recv_buf, sock, &start_receive](const boost::system::error_code& err, size_t bytes) {
                        if (err || bytes == 0 || p->stopping) {
                            if (!p->stopping) start_receive(sock);
                            return;
                        }
                        LocalDiscoveredDevice dev;
                        if (parse_ssdp_notify(std::string(recv_buf->data(), bytes), dev)) {
                            BOOST_LOG_TRIVIAL(trace)
                                << "[SSDP] discovered device: serial="
                                << dev.serial_number << " ip=" << dev.ip
                                << " model=" << dev.model
                                << " name=" << dev.name;
                            mergeDevice(std::move(dev));
                        }
                        start_receive(sock);
                    });
            };

            // Bind to SSDP port, join multicast, send M-SEARCH.
            auto sock = std::make_unique<udp::socket>(io);
            sock->open(udp::v4());
            sock->set_option(asio::socket_base::reuse_address(true));
            sock->bind(udp::endpoint(asio::ip::address_v4::any(), SSDP_MCAST_PORT));
            try {
                sock->set_option(asio::ip::multicast::join_group(
                    asio::ip::make_address(SSDP_MCAST_ADDR).to_v4()));
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error)
                    << "[SSDP] multicast join_group failed: " << e.what();
            }

            start_receive(sock.get());

            boost::system::error_code ec;
            sock->send_to(asio::buffer(msearch),
                udp::endpoint(asio::ip::make_address(SSDP_MCAST_ADDR), SSDP_MCAST_PORT), 0, ec);

            // Retry loop
            for (int retry = 0; retry < SSDP_RETRIES && !p->stopping; ++retry) {
                if (retry > 0) {
                    sock->send_to(asio::buffer(msearch),
                        udp::endpoint(asio::ip::make_address(SSDP_MCAST_ADDR), SSDP_MCAST_PORT), 0, ec);
                }
                asio::steady_timer timer(io);
                timer.expires_after(std::chrono::seconds(SSDP_TIMEOUT_SEC));
                timer.async_wait([&io](const boost::system::error_code&) { io.stop(); });
                io.run();
                if (p->stopping) break;
                io.restart();
            }
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "[SSDP] lookup exception: " << e.what();
        }
        finishRefresh();
    });
}

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
		return true;  // 任何 JSON 对象都匹配
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


QDSDevice::QDSDevice(const std::string dev_id, const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type)
    : m_id(dev_id), m_name(dev_name), m_ip(dev_ip), m_type(dev_type)
    , m_boxData(17), m_boxTemperature(4, 0.0), m_boxHumidity(4, 0)
{
    //y79
    m_url = "ws://" + dev_url + ":7125/websocket";

    last_update = std::chrono::steady_clock::now();
    
    QDSDevice::initGeneralData();
    m_filamentConfig = m_general_filamentConfig;
}

void QDSDevice::updateByJsonData(const json& status)
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
                m_print_duration = "";
                m_print_total_time = "";
                m_filament_weight = "";
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

	//cj_4
	if (status.contains("print_stats") && status["print_stats"].contains("plateindex")) {
		int plate_idx = std::stoi(status["print_stats"]["plateindex"].get<std::string>());
		if (m_plate_index != plate_idx) {
			is_update = true;
			m_plate_index = plate_idx;
		}
	}
	if (status.contains("print_stats") && status["print_stats"].contains("filename")) {

		if (m_print_filename != status["print_stats"]["filename"].get<std::string>()) {
			is_update = true;
			m_print_filename = status["print_stats"]["filename"].get<std::string>();
            
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



	if (status.contains("display_status") && status["display_status"].contains("progress")) {

		if (m_print_progress_float != status["display_status"]["progress"].get<float>()) {
			is_update = true;
			m_print_progress_float = status["display_status"]["progress"].get<float>();
		}
			//cj_4
			std::string progress_str = std::to_string(status["display_status"]["progress"].get<int>());
			if (m_print_progress != progress_str) {
				is_update = true;
				m_print_progress = progress_str;
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

	//cj_3
	if (status.contains("output_pin polar_cooler") && status["output_pin polar_cooler"].contains("value")) {
        const bool pin_on = bool(status["output_pin polar_cooler"]["value"].get<float>());
        if (m_polar_cooler.load() != pin_on) {
			is_update = true;
			m_polar_cooler = pin_on;
			//cj_4
			m_polar_cooler_dirty_for_ui = true;
		}
	}
	twoStageParse(status, m_auxiliary_fan_speed, "fan_generic auxiliary_cooling_fan", "speed");
	twoStageParse(status, m_chamber_fan_speed, "fan_generic chamber_circulation_fan", "speed");
	twoStageParse(status, m_cooling_fan_speed, "fan_generic cooling_fan", "speed");
	twoStageParse(status, m_home_axes, "toolhead", "homed_axes");
	twoStageParse(status, m_extruder_filament, "filament_switch_sensor filament_switch_sensor", "filament_detected");

    apply_gcode_move_speed_percent(*this, status, nullptr);

    for (int i = 0; i < 4; ++i) {
        std::string key = "aht20_f heater_box" + std::to_string(i + 1);
		if (status.contains(key) ) {
            if (status[key].contains("temperature")) {
                if (m_boxTemperature[i] != int(status[key]["temperature"].get<float>())) {
                    m_is_update_box_temp = true;
                    m_boxTemperature[i] = int(status[key]["temperature"].get<float>());
                }
            }
			if (status[key].contains("humidity")) {
				if (m_boxHumidity[i] != status[key]["humidity"].get<int>()) {
                    m_is_update_box_temp = true;
                    m_boxHumidity[i] = status[key]["humidity"].get<int>();

				}
			}
		}
    }
	//cj_4
	if (status.contains("exclude_object") && status["exclude_object"].is_object()) {
		const auto& eo = status["exclude_object"];
		if (eo.contains("excluded_objects") && eo["excluded_objects"].is_array()) {
			std::vector<std::string> new_list;
			for (const auto& obj : eo["excluded_objects"]) {
				if (obj.is_string())
					new_list.push_back(obj.get<std::string>());
			}
			if (m_excluded_objects != new_list) {
				is_update = true;
				m_excluded_objects = std::move(new_list);
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



    //int isExit = getJsonCurStageToInt(saveVariables, "enable_box");
    //if (isExit != -1) {
        m_boxData[16].hasMaterial =  true;
    //}
	int count = getJsonCurStageToInt(saveVariables, "box_count");
	if (count != -1) {
		m_box_count = count;
	}
    
	if (saveVariables.contains("last_load_slot") && saveVariables["last_load_slot"].is_string()) {
        m_cur_slot = saveVariables["last_load_slot"].get<std::string>();
	}
	
    int b_endstop_state = 0;
    //twoStageParse1(status, target, first, second, temp_is_update);
    twoStageParse1(status, b_endstop_state, "", "", box_is_update);
    if (b_endstop_state == 1) {
        m_cur_slot = "slot16";
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

    //y78
    std::vector<int> slot_state(17);
    std::vector<int> slot_id(17);
    std::vector<std::string> filament_id(17);
    std::vector<std::string> filament_colors(17);
    std::vector<std::string> filament_type(17);
    
    std::unordered_map<std::string, std::string> mapping = {
        {"X-Plus 4", "0"},
        {"Q2", "1"},
        {"Q2C", "2"},
        {"X-Max 4", "3"}
    };
    for(int i = 0; i < 17; ++i){
        if(m_boxData[i].hasMaterial){
            slot_state[i] = m_boxData[i].hasMaterial;
            slot_id[i] = i;
            filament_type[i] = m_boxData[i].type;
            filament_colors[i] = m_boxData[i].colorHexCode;

            std::string slot_vendor = m_boxData[i].vendor;

            std::string test_type = mapping[m_type];
            std::string test_vendor = slot_vendor == "QIDI" ? "1" : "0";
            std::string tset_idx = std::to_string(m_boxData[i].filament_idex);
            std::string test_id = "QD_" + test_type + "_" +  test_vendor + "_" + tset_idx;
            filament_id[i] = test_id;
        }
    }
    m_filament_colors = filament_colors;
    m_filament_type = filament_type;
    m_filament_id = filament_id;
    m_slot_id = slot_id;
    m_slot_state = slot_state;

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

std::vector<float> QDSDevice::getNozzleDiameter(){
    return m_nozzle_diameter;
}

//y79
void QDSDevice::updatePrinterStatusData(json& status){
    std::lock_guard<std::mutex> lock(m_config_mtx);
    maker_job_is_update = true;
    maker_job_state = status.contains("jobState") ? status["jobState"].get<std::string>() : maker_job_state;
    maker_job_progress = status.contains("progress") ? status["progress"].get<std::string>() : maker_job_progress;
    if(status.contains("failCause") && !status["failCause"].empty()){
        std::cout << "some error is " << status << std::endl;
        maker_job_is_update = false;
    }
}

std::string QDSDevice::getMakerJobState(){
    std::lock_guard<std::mutex> lock(m_config_mtx);
    return maker_job_state;
}

std::string QDSDevice::getMakerJobProgress(){
    std::lock_guard<std::mutex> lock(m_config_mtx);
    return maker_job_progress;
}

void QDSDevice::setMakerJobIsUpdate(bool value) {
    std::lock_guard<std::mutex> lock(m_config_mtx);
    maker_job_is_update = value;
}
//y79

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
    const auto reconnect_cooldown = std::chrono::seconds(20);

    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto now = std::chrono::steady_clock::now();

        for (const auto& [device_id, device] : devices_) {
            bool needs_reconnect = false;
            std::string reason;

            if (device->is_selected.load() && !device->is_net_device) {
                if (device->reconnecting.load()) {
                    continue;
                }
                if (device->last_reconnect != std::chrono::steady_clock::time_point::min() &&
                    (now - device->last_reconnect) < reconnect_cooldown) {
                    continue;
                }
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
                    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "device last update : " << (device->last_update).time_since_epoch().count() << std::endl;
                    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "now time is  : " << now.time_since_epoch().count() << std::endl;
                }
            }

            if (needs_reconnect) {
                BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[HealthCheck] Device " << device_id << "device name is " << device->m_name << " needs reconnect: " << reason << std::endl;
                devices_to_reconnect.push_back(device_id);
            }
        }
    }

    for (const auto& device_id : devices_to_reconnect) {
        reconnectDevice(device_id);
    }
}

void QDSDeviceManager::reconnectDevice(const std::string& device_id) {
    auto device = getDevice(device_id);
    if (!device) {
        return;
    }
    bool expected = false;
    if (!device->reconnecting.compare_exchange_strong(expected, true)) {
        return;
    }
    struct ReconnectGuard {
        std::shared_ptr<QDSDevice> dev;
        ~ReconnectGuard() { if (dev) dev->reconnecting = false; }
    } guard { device };

    device->last_reconnect = std::chrono::steady_clock::now();
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[HealthCheck] Reconnecting device " << device_id << "..." << std::endl;

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
    std::shared_ptr<QDSDevice> ret;

    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto it = devices_.find(device_id);
        if (it != devices_.end())
            ret = it->second;
    }
    return ret;
}

//y80
std::string QDSDeviceManager::getNetDeviceIDByIp(const std::string& ip){
    std::lock_guard<std::mutex> lock(manager_mutex_);
    for(const auto& [device_id, device] : devices_){
        if(device->m_ip == ip && device->is_net_device)
            return device_id;
    }
    return "";
}

//y80
std::string QDSDeviceManager::getLocalDeviceIDByIp(const std::string& ip){
    std::lock_guard<std::mutex> lock(manager_mutex_);
    for(const auto& [device_id, device] : devices_){
        if(device->m_ip == ip && !device->is_net_device)
            return device_id;
    }
    return "";
}

//cj_3
std::vector<std::pair<std::string, std::shared_ptr<QDSDevice>>> QDSDeviceManager::snapshotDevices()
{
    std::lock_guard<std::mutex> lock(manager_mutex_);
    std::vector<std::pair<std::string, std::shared_ptr<QDSDevice>>> out;
    out.reserve(devices_.size());
    for (const auto& kv : devices_) {
        out.emplace_back(kv.first, kv.second);
    }
    return out;
}

//cj_5
void QDSDeviceManager::refreshLocalDevices(bool force, LocalDeviceDiscovery::RefreshCallback callback)
{
    m_local_discovery.refresh(force, std::move(callback));
    //cj_5 Trigger SSDP discovery alongside UDP so both run together.
    m_ssdp_discovery.refresh(force, nullptr);
}

//cj_5
bool QDSDeviceManager::findLocalDeviceBySerial(const std::string& serial, LocalDiscoveredDevice& out) const
{
    return m_local_discovery.findBySerial(serial, out);
}

//cj_5
LocalDeviceDiscovery::Snapshot QDSDeviceManager::snapshotLocalDevices() const
{
    return m_local_discovery.snapshot();
}

//cj_5 SSDP discovery API
void QDSDeviceManager::refreshSSDPDevices(bool force, SSDPDiscovery::RefreshCallback callback)
{
    m_ssdp_discovery.refresh(force, std::move(callback));
}

bool QDSDeviceManager::findSSDPDeviceBySerial(const std::string& serial, LocalDiscoveredDevice& out) const
{
    return m_ssdp_discovery.findBySerial(serial, out);
}

bool QDSDeviceManager::findSSDPDeviceByIP(const std::string& ip, LocalDiscoveredDevice& out) const
{
    return m_ssdp_discovery.findByIP(ip, out);
}

SSDPDiscovery::Snapshot QDSDeviceManager::snapshotSSDPDevices() const
{
    return m_ssdp_discovery.snapshot();
}

//cj_5
#if QDT_RELEASE_TO_PUBLIC
bool QDSDeviceManager::findLocalForNetDevice(const NetDevice& net_dev, LocalDiscoveredDevice& out) const
{
    // Primary match: serial number (cloud serialNumber == UDP field 7 serial)
    if (findLocalDeviceBySerial(net_dev.mac_address, out)) {
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__
            << " Found local match by serial: " << net_dev.mac_address
            << " at IP " << out.ip << std::endl;
        return true;
    }

    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__
        << " No local match for net device: " << net_dev.mac_address << std::endl;
    return false;
}
#endif

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
                BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Stop] Warning closing connection for device " 
                          << device_id << ": " << ec.message() << std::endl;
            }
        }
        
        conn->client.stop();
        conn->running = false;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Stop] Exception while stopping client for device " 
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

    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Manager] Starting cleanup for device " << device_id << std::endl;

    if (conn) {
        std::thread& client_thread = conn->client_thread;

        if (client_thread.joinable()) {
            try {
                if (client_thread.joinable()) {
                    client_thread.join();
                }
            }
            catch (const std::system_error& e) {
                BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Cleanup] System error joining thread for device "
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
                        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Cleanup] Failed to detach thread for device "
                            << device_id << std::endl;
                    }
                }
            }
            catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Cleanup] Error joining thread for device "
                    << device_id << ": " << e.what() << std::endl;
            }
        }
    }

    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Manager] Device " << device_id << " connection cleaned up." << std::endl;
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
        //y79
        device->m_frp_url = "http://" + dev_url ;

        devices_[device_id] = device;
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Manager] Device added: " << device_id << std::endl;
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
		BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "device :" << device << "exit" << std::endl;
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
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[Connect] Error: Device " << device_id << " not found." << std::endl;
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
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[DEBUG] WebSocket closed for device: " << device_id << std::endl;
            auto ws_conn = conn->client.get_con_from_hdl(hdl);
            if (ws_conn) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[DEBUG] Close code: " << ws_conn->get_remote_close_code() << std::endl;
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[DEBUG] Close reason: " << ws_conn->get_remote_close_reason() << std::endl;
            }
        }

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
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[Connect] Connection error for device " << device_id 
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
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[WebSocket] Exception in client thread: " 
                          << e.what() << std::endl;
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[WebSocket] Exception in client thread: "
                          << e.what() << std::endl;
            }
            connection->running = false;
        });
        
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[Connect] Connecting to device " << device_id 
                  << " (" << dev->m_name << ")..." << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "[Connect] Exception while connecting to device " << device_id
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
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[WS] Device " << device_id << " connected." << std::endl;
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
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Message] JSON parse error for device " << device_id 
                      << ": " << e.what() << std::endl;
            return;
        }
        //cj_3
        // Any valid websocket payload means the connection is alive.
        if (auto dev = getDevice(device_id)) {
            dev->last_update = std::chrono::steady_clock::now();
        }
        
        handleDeviceMessage(device_id, message_json);
        
    } catch (const std::exception& e) {
//         BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Message] Error in onMessage for device " << device_id 
//                   << ": " << e.what() 
//                   << ", message length: " << msg_str.length() << std::endl;
    }
}

void QDSDeviceManager::onClose(const std::string& device_id, websocketpp::connection_hdl hdl) {
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[WS] Device " << device_id << " disconnected." << std::endl;
    processConnectionStatus(device_id, "offline");
}

void QDSDeviceManager::onFail(const std::string& device_id, websocketpp::connection_hdl hdl) {
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[WS] Device " << device_id << " connection failed." << std::endl;
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
            {"gcode_move", nullptr},
            // {"gcode_macro M109", nullptr},
             {"exclude_object", nullptr},
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
             {"filament_switch_sensor filament_switch_sensor", nullptr},
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
             //cj_3
             {"output_pin polar_cooler", nullptr},
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
            {"toolhead", nullptr},
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
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Error sending subscribe to " << device_id 
                      << ": " << ec.message() << std::endl;
        } else {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Sent subscribe message to " << device_id << std::endl;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Exception sending to device " << device_id 
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
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Error sending subscribe to " << device_id 
                      << ": " << ec.message() << std::endl;
        } else {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Sent subscribe message to " << device_id << std::endl;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Exception sending to device " << device_id 
                  << ": " << e.what() << std::endl;
    }
}

bool QDSDeviceManager::sendCommand(const std::string& device_id, const std::string& scriptName, const std::string& script, const std::string& method)
{
	std::shared_ptr<WebSocketConnect> conn = nullptr;
	{
		std::lock_guard<std::mutex> lock(manager_mutex_);
		auto conn_it = connections_.find(device_id);
		if (conn_it == connections_.end() || conn_it->second->stopping) {
			return false;
		}
		conn = conn_it->second;
	}

	if (!conn) return false;

	json subscribe_msg = {
		{"id", std::atoi(device_id.c_str())},
		{"method", method},
		{"jsonrpc", "2.0"},
		{"params", {
			{scriptName, script}
		}}
	};

	try {
		websocketpp::lib::error_code ec;
		conn->client.send(conn->connection_hdl,
			subscribe_msg.dump(),
			websocketpp::frame::opcode::text,
			ec);
		if (ec) {
			BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Error sending subscribe to " << device_id
				<< ": " << ec.message() << std::endl;
			return false;
		}
		BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Sent subscribe message to " << device_id << std::endl;
		return true;
	}
	catch (const std::exception& e) {
		BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Exception sending to device " << device_id
			<< ": " << e.what() << std::endl;
		return false;
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
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Error sending subscribe to " << device_id 
                      << ": " << ec.message() << std::endl;
        } else {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Sent subscribe message to " << device_id << std::endl;
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << "[Send] Exception sending to device " << device_id 
                  << ": " << e.what() << std::endl;
    }
}

void QDSDeviceManager::handleDeviceMessage(const std::string& device_id, const json& message) {
    std::shared_ptr<QDSDevice> device = getDevice(device_id);
    if (!device) {
        return;
    }

    // if(!device->is_selected.load()){
    //     std::thread([this, device_id]() {
    //         std::this_thread::sleep_for(std::chrono::milliseconds(10));
    //         stopConnection(device_id);
    //     }).detach();
    // }

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
        
        // if(!device->is_selected.load())
        //     return;
        //BOOST_LOG_TRIVIAL(trace) << device << " : " << message;
		if (message.contains("method") && message.contains("params") 
            ) {
            if (message.at("method").get<std::string>() == "notify_proc_stat_update" && message.at("params").is_array()) {
                
                const json& result = message.at("params").at(0);

                if(result.contains("config_items")){
                    device->m_enable_polar_cooler = result["config_items"]["printing.polar_cooler"].get<std::string>() == "1" ? true : false;
                    
                    //y78
                    if(result["config_items"].contains("nozzle.diameter")){
                        device->m_nozzle_diameter.clear();
                        if (result["config_items"]["nozzle.diameter"].is_array()) {
                            for (const auto& item : result["config_items"]["nozzle.diameter"]) {
                                device->m_nozzle_diameter.push_back(std::stof(item.get<std::string>()));
                            }
                        } else {
                            device->m_nozzle_diameter.push_back(std::stof(result["config_items"]["nozzle.diameter"].get<std::string>()));
                        }
                    }
                }

            }
        }
        
        // 处理状态更新
        if (message.contains("result")) {
            
            if(message.at("result").contains("files")){
                const json& result = message.at("result");
                updateDeviceFileInfo(device, result);
                is_file_info_update = true;
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

// 			if (result.contains("files")) {
// 				updateDeviceFileInfo(device, result);
// 			}
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
}

void QDSDeviceManager::updateDeviceData(std::shared_ptr<QDSDevice>& device,
                                        const json& result,
                                        std::string& new_status,
                                        bool& is_update) {

    //cj_4
    // Capture pre-call state for diff detection.
    std::string old_status = device->m_status;
    std::string old_print_duration = device->m_print_duration;

    // Delegate all Klipper status field parsing to device.
    device->updateByJsonData(result);

    // --- Post-processing that belongs to manager, not device ---

    // Status transition → report via connection callback.
    if (device->m_status != old_status) {
        new_status = device->m_status;
    }

    // Propagate device update flag to caller.
    if (device->is_update) {
        is_update = true;
    }

    // When print_duration changes, refresh thumb URL if not already loaded.e
    if (device->m_print_duration != old_print_duration
        /*&& device->m_print_png_url.empty()*/) {
        updatePrintThumbUrlWithOutMsg(device);
    }
}

//cj_2
std::string extractAfterGcodes(const std::string& fullPath) {
	std::string keyword = "/gcodes/";

	size_t pos = fullPath.find(keyword);
	if (pos != std::string::npos) {
		// 从 keyword 后面开始截取
		return fullPath.substr(pos + keyword.length());
	}

	return "";  // 没找到返回空字符串
}

void QDSDeviceManager::updateDeviceFileInfo(std::shared_ptr<QDSDevice>& device, const json& result){
    device->file_info.clear();
    //y78
    const auto& result_array = result["result"];
    for(const auto& file_item : result_array){
        GCodeFileInfo file_info;
        //cj_2 filter cache file
        file_info.file_path = file_item["filepath"].get<std::string>();
        if (file_info.file_path.find("/.cache/")!= std::string::npos) {
            continue;
        }
        file_info.extension = file_item["extension"].get<std::string>();
		//file_info.file_name = file_item["filename"].get<std::string>();
		file_info.file_name = extractAfterGcodes(file_item["filepath"].get<std::string>());
        file_info.plate_count = file_item["plate_count"].get<std::string>();
        file_info.show_filament_weight = file_item["show_filament_weight"].get<std::string>();
        file_info.show_print_time = file_item["show_print_time"].get<std::string>();
        
        int plate_count = std::stoi(file_info.plate_count);
        if(plate_count > 0){
            auto plates_array = file_item["plates"];
            for(const auto& plate_item : plates_array){
                
                PlateInfo plate_info;
                plate_info.index = plate_item["plate_index"].get<std::string>();
                boost::split(plate_info.filament_colours, plate_item["filament_colour"].get<std::string>(), boost::is_any_of(";"));
                boost::split(plate_info.filament_types, plate_item["filament_type"].get<std::string>(), boost::is_any_of(";"));
                boost::split(plate_info.used_extruders, plate_item["used_extruders"].get<std::string>(), boost::is_any_of(";"));
                plate_info.filament_weight = plate_item["filament_weight"].get<std::string>();
                plate_info.print_time = plate_item["print_time"].get<std::string>();
                plate_info.nozzle_diameter = plate_item["nozzle_diameter"].get<std::string>();

                // Only strip .3mf extension; keep other extensions intact
                std::string name_without_extension = file_info.file_name;
                const std::string ext_3mf = ".3mf";
                if (name_without_extension.size() > ext_3mf.size()) {
                    std::string suffix = name_without_extension.substr(name_without_extension.size() - ext_3mf.size());
                    std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
                    if (suffix == ext_3mf) {
                        name_without_extension = name_without_extension.substr(0, name_without_extension.size() - ext_3mf.size());
                    }
                }

				plate_info.thumb_url = device->m_frp_url + "/server/files/gcodes/.thumbs/" + name_without_extension + "/plate_" + plate_info.index + ".png";

                wxBitmap bitmap = ScalableBitmap(nullptr, "monitor_placeholder", 160).bmp();
                wxImage image = bitmap.ConvertToImage();
                if (image.IsOk()) {
                    wxMemoryOutputStream mos;
                    if (image.SaveFile(mos, wxBITMAP_TYPE_PNG)) {
						const size_t len = mos.GetSize();
                        plate_info.thumbnailData.pixels.resize(len);
                        if (len > 0) {
                            mos.CopyTo(plate_info.thumbnailData.pixels.data(), len);
                        }
                    }
                }

                file_info.plates.emplace_back(plate_info);

                DownloadManager::getInstance().downloadThumbnail(
                    UrlEncodeForFilename(plate_info.thumb_url),
                    file_info.file_name,
                    [device](ThumbnailResult result) {
                        if (!result.success)
                            return;

                        for (auto& file_info_item : device->file_info) {
                            for (auto& plate_info_item : file_info_item.plates) {
                                if (UrlEncodeForFilename(plate_info_item.thumb_url) == result.url) {
                                    plate_info_item.thumbnailData.pixels.assign(result.png_data.begin(), result.png_data.end());
                                    return;
                                }
                            }
                        }
                    }
                );
            }
        }
        file_info.show_thumb_url = file_info.plates.empty() ? "" : file_info.plates[0].thumb_url;
        
        
        const auto& thumbnails = file_item["thumbnails"];
        for (const auto& thumbnailItem : thumbnails) {
            file_info.thumbnailsSize = thumbnailItem["data_size"].get<int>();
            break;
        }


        device->file_info.emplace_back(file_info);
    }       
    device->m_fresh_file_info = true;
}

void QDSDeviceManager::updatePrintThumbUrl(std::shared_ptr<QDSDevice>& device, const json& message){
    const json& result = message.at("params")[0];
    if(result["action"] == "added"){
        BOOST_LOG_TRIVIAL(trace) << result;
		device->m_print_filename = result["job"]["filename"];
		std::string thumb_path = result["job"]["metadata"]["thumbnails"]["relative_path"];
		device->m_print_png_url = device->m_frp_url + "/server/files/gcodes/" + thumb_path;
	}
	else if (result["action"] == "finished") {
        device->m_print_filename = "";
		device->m_print_png_url = "";
	}
}

void QDSDeviceManager::updatePrintThumbUrlWithOutMsg(std::shared_ptr<QDSDevice>& device){
        if(!device->file_info.empty() /*&& device->m_print_png_url.empty()*/){
        std::string print_file_name = device->m_print_filename;
        std::vector<GCodeFileInfo> files_info = device->file_info;
        for(auto file_ : files_info){
			if (file_.file_name == print_file_name)
            {
                
                device->m_print_png_url = UrlEncodeForFilename(file_.show_thumb_url);
            }
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
        //cj_4
        device->m_polar_cooler_dirty_for_ui = true;
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
    
    //y79
    std::set<std::pair<std::string, std::string>> mapping;

    auto vendor_presets = wxGetApp().preset_bundle->printers.get_presets();
    for(auto preset : vendor_presets){
        std::string printer_model = preset.config.opt_string("printer_model");
        std::string box_id = preset.config.opt_string("box_id");

        if (!printer_model.empty() && !box_id.empty()) {
            mapping.emplace(printer_model, box_id);
        }
    }

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

                std::string test_type = "";

                auto it = std::find_if(mapping.begin(), mapping.end(),
                    [&](const std::pair<std::string, std::string>& pair) {
                        return pair.first == device->m_type;
                    });

                if (it != mapping.end()) {
                    test_type = it->second;
                }

                std::string test_vendor = slot_vendor == "QIDI" ? "1" : "0";
                std::string tset_idx = std::to_string(device->m_boxData[i].filament_idex);
                std::string test_id = "QD_" + test_type + "_" +  test_vendor + "_" + tset_idx;
                filament_id[i] = test_id;
            }
        }
        box_count = device->m_box_count;
        auto_reload_detect = device->m_auto_reload_detect;
        box_list_preset_name = device->m_type;
    }

    wxGetApp().plater()->box_msg.slot_state = slot_state;
    wxGetApp().plater()->box_msg.filament_id = filament_id;
    wxGetApp().plater()->box_msg.filament_colors = filament_colors;
    wxGetApp().plater()->box_msg.box_count = box_count;
    wxGetApp().plater()->box_msg.filament_type = filament_type;
    wxGetApp().plater()->box_msg.slot_id = slot_id;
    wxGetApp().plater()->box_msg.auto_reload_detect = auto_reload_detect;
    wxGetApp().plater()->box_msg.box_list_preset_name = box_list_preset_name;
    //y78
    if(box_count > 0)
        wxGetApp().plater()->sidebar().box_list_printer_ip = device->m_ip;
    else
        wxGetApp().plater()->sidebar().box_list_printer_ip = "";

    GUI::wxGetApp().sidebar().load_box_list();
}

//y78
void QDSDeviceManager::getFileInfo(const std::string& device_id){
    new std::thread([this,device_id]() {
        std::shared_ptr<QDSDevice> device = getDevice(device_id);
        if (!device) {
            return;
        }

        std::string api_url = device->m_frp_url + "/api/qidiclient/files/list";

        auto http = Http::get(std::move(api_url));

        http.on_error([&](std::string body, std::string error, unsigned status) {
            //BOOST_LOG_TRIVIAL(trace) << boost::format("Error getting version: %1%, HTTP %2%, body: `%3%`") % error % status % body;

            })
            .on_complete([&, this](std::string body, unsigned) {
                try {
                    json bodyJson = json::parse(body);
                    if (bodyJson.contains("result"))
                        updateDeviceFileInfo(device, bodyJson);
                }
                catch (const std::exception& error) {
                    BOOST_LOG_TRIVIAL(trace) << "json error " << error.what();
                };
                })
                .perform_sync();

        //cj_3
        const std::string timelapse_dir_url =
            device->m_frp_url + "/server/files/directory?root=timelapse&path=timelapse&extended=true";
        auto http_timelapse = Http::get(timelapse_dir_url);
        http_timelapse
            .on_error([&](std::string body, std::string error, unsigned status) {
            (void)body;
            (void)error;
            (void)status;
                })
            .on_complete([&, this](std::string body, unsigned) {
                    updateDeviceTimelapseFileInfo(device, body);
                })
                    .perform_sync();

        auto file_cb = getFileInfoUpdateCallback();
        if (file_cb) {
            file_cb(device_id);
        }
        });
}

//cj_3
void QDSDeviceManager::updateDeviceTimelapseFileInfo(std::shared_ptr<QDSDevice>& device, const std::string& response_body)
{
    if (!device) {
        return;
    }

    device->timelapse_file_info.clear();

    try {
        json bodyJson = json::parse(response_body);

        //cj_3
        if (!bodyJson.contains("result") || !bodyJson["result"].is_object()) {
            device->m_fresh_timelapse_file_info = true;
            return;
        }
        const json& res = bodyJson["result"];
        if (!res.contains("files") || !res["files"].is_array()) {
            device->m_fresh_timelapse_file_info = true;
            return;
        }
        const json& files = res["files"];

        std::unordered_set<std::string> name_set;
        for (const auto& f : files) {
            if (f.is_object() && f.contains("filename") && f["filename"].is_string()) 
                name_set.insert(f["filename"].get<std::string>());
            
        }

        for (const auto& f : files) {
            if (!f.is_object() || !f.contains("filename") || !f["filename"].is_string())
                continue;
            const std::string fname = f["filename"].get<std::string>();
            const size_t dot = fname.rfind('.');
            if (dot == std::string::npos || dot + 4 > fname.size())
                continue;
            std::string ext = fname.substr(dot);
            for (char& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".mp4")
                continue;

            TimelapseFileInfo info;
            info.file_name = fname;
            if (f.contains("size")) {
                std::uint64_t size_bytes = 0;
                bool            have_size = false;
                if (f["size"].is_number_integer()) {
                    const auto v = f["size"].get<std::int64_t>();
                    size_bytes = v > 0 ? static_cast<std::uint64_t>(v) : 0;
                    have_size  = true;
                } else if (f["size"].is_number_unsigned()) {
                    size_bytes = f["size"].get<std::uint64_t>();
                    have_size  = true;
                } else if (f["size"].is_number_float()) {
                    const double d = f["size"].get<double>();
                    if (d > 0 && std::isfinite(d))
                        size_bytes = static_cast<std::uint64_t>(d);
                    have_size = true;
                }
                if (have_size)
                    info.file_size = format_timelapse_file_size_b_kb_mb(size_bytes);
            }
            if (f.contains("modified") && f["modified"].is_number()) {
                const double mod = f["modified"].get<double>();
                wxDateTime dt(static_cast<time_t>(std::llround(mod)));
                info.modified_time = std::string(dt.Format("%Y/%m/%d %H:%M").utf8_string());
            }

            const std::string jpg_name = fname.substr(0, dot) + ".jpg";
            
            if (name_set.find(jpg_name) != name_set.end()) {
                info.thumb_url = device->m_frp_url + "/server/files/timelapse/" + UrlEncodeForFilename(jpg_name);
            }

            device->timelapse_file_info.push_back(std::move(info));
        }
    }
    catch (const std::exception& err) {
        BOOST_LOG_TRIVIAL(trace) << "timelapse directory json error " << err.what();
    }

    device->m_fresh_timelapse_file_info = true;
}

void QDSDeviceManager::resetBoxUpdateStatus(const std::string& device_id) {
    std::shared_ptr<QDSDevice> device = getDevice(device_id);
    if (device) {
        device->reset_update_status();
    }
}
}}
