#include "QDSDeviceManager.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/asio.hpp>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "Iphlpapi.lib")
#endif
#include <random>
#include "libslic3r/Utils.hpp"
#include "GUI_App.hpp"
#include "DeviceSettingDialog.hpp"
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
#include <mutex>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <future>
#include <functional>
#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <wx/datetime.h>。
#include <wx/evtloop.h>

//y84
#include "DeviceCore/DevConfig.h"

//cj_2
#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#include "../QIDI/P2PManager.hpp"
#endif

#include "../QIDI/QIDIDeviceApi.hpp"
#include "../QIDI/QIDIFileManager.hpp"

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
    BOOST_LOG_TRIVIAL(info)
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
                            BOOST_LOG_TRIVIAL(info)
                                << "[SSDP] discovered device: serial="
                                << dev.serial_number << " ip=" << dev.ip
                                << " model=" << dev.model
                                << " name=" << dev.name;
                            mergeDevice(std::move(dev));
                        }
                        start_receive(sock);
                    });
            };

            //cj_5 Enumerate local IPv4 addresses and create a socket per interface.
            // Binding to 0.0.0.0 causes the OS to pick a single default interface,
            // which is wrong on multi-homed machines (WiFi+Ethernet+VPN).
            std::vector<asio::ip::address_v4> local_addrs;
#ifdef _WIN32
            {
                ULONG bufLen = 0;
                GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &bufLen);
                if (bufLen > 0) {
                    std::vector<BYTE> buf(bufLen);
                    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
                    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &bufLen) == NO_ERROR) {
                        for (auto* a = adapters; a; a = a->Next) {
                            if (a->OperStatus != IfOperStatusUp) continue;
                            for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
                                if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                                    auto* sin = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
                                    asio::ip::address_v4 ip(ntohl(sin->sin_addr.s_addr));
                                    if (!ip.is_loopback()) {
                                        bool dup = false;
                                        for (auto& ex : local_addrs) { if (ex == ip) { dup = true; break; } }
                                        if (!dup) local_addrs.push_back(ip);
                                    }
                                }
                            }
                        }
                    }
                }
            }
#endif
            if (local_addrs.empty())
                local_addrs.push_back(asio::ip::address_v4::any());

            std::vector<std::unique_ptr<udp::socket>> socks;
            boost::system::error_code ec;
            const auto mcast_addr = asio::ip::make_address(SSDP_MCAST_ADDR).to_v4();
            for (const auto& iface_addr : local_addrs) {
                auto sock = std::make_unique<udp::socket>(io);
                sock->open(udp::v4(), ec);
                if (ec) continue;
                sock->set_option(asio::socket_base::reuse_address(true), ec);
                sock->bind(udp::endpoint(iface_addr, SSDP_MCAST_PORT), ec);
                if (ec) {
                    BOOST_LOG_TRIVIAL(info) << "[SSDP] bind " << iface_addr.to_string()
                                            << " failed: " << ec.message() << " (continuing)";
                    continue;
                }
                try {
                    sock->set_option(asio::ip::multicast::join_group(mcast_addr, iface_addr));
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(info) << "[SSDP] join_group " << iface_addr.to_string()
                                            << " failed: " << e.what() << " (continuing)";
                    continue;
                }
                BOOST_LOG_TRIVIAL(info) << "[SSDP] listening on " << iface_addr.to_string()
                                        << ":" << SSDP_MCAST_PORT;

                start_receive(sock.get());
                sock->send_to(asio::buffer(msearch),
                    udp::endpoint(asio::ip::make_address(SSDP_MCAST_ADDR), SSDP_MCAST_PORT), 0, ec);
                socks.push_back(std::move(sock));
            }

            // Retry loop — send M-SEARCH on all sockets
            for (int retry = 0; retry < SSDP_RETRIES && !p->stopping; ++retry) {
                if (retry > 0) {
                    for (auto& s : socks) {
                        s->send_to(asio::buffer(msearch),
                            udp::endpoint(asio::ip::make_address(SSDP_MCAST_ADDR), SSDP_MCAST_PORT), 0, ec);
                    }
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

namespace {

template<typename T> struct strip_atomic { using type = T; };
template<typename X> struct strip_atomic<std::atomic<X>> { using type = X; };
template<typename T> using strip_atomic_t = typename strip_atomic<T>::type;

template<typename T>
bool json_get(const json& j, T& target, std::initializer_list<std::string> path)
{
    using U = strip_atomic_t<std::decay_t<T>>;
    if (path.size() == 0) return false;
    const json* cur = &j;
    auto it = path.begin();
    for (; std::next(it) != path.end(); ++it) {
        if (!cur->contains(*it) || !(*cur)[*it].is_object()) return false;
        cur = &(*cur)[*it];
    }
    const std::string& last = *it;
    if (!cur->contains(last)) return false;
    const json& v = (*cur)[last];
    if (v.is_null()) return false;

    if constexpr (std::is_same_v<U, bool>) {
        if (v.is_boolean()) {
            bool val = v.get<bool>();
            if (target != val) { target = val; return true; }
            return false;
        }
        if (v.is_number()) {
            bool val = v.is_number_integer() ? (v.get<int64_t>() != 0) : (v.get<double>() != 0.0);
            if (target != val) { target = val; return true; }
            return false;
        }
        return false;
    }
    else if constexpr (std::is_arithmetic_v<U>) {
        if (!v.is_number()) return false;
        U val = v.is_number_integer()
            ? static_cast<U>(v.get<int64_t>())
            : static_cast<U>(v.get<double>());
        if (target != val) { target = val; return true; }
        return false;
    }
    else {
        if (!v.is_string()) return false;
        std::string val = v.get<std::string>();
        if (target != val) { target = val; return true; }
        return false;
    }
}

bool json_get_int_str(const json& j, std::string& target, std::initializer_list<std::string> path)
{
    if (path.size() == 0) return false;
    const json* cur = &j;
    auto it = path.begin();
    for (; std::next(it) != path.end(); ++it) {
        if (!cur->contains(*it) || !(*cur)[*it].is_object()) return false;
        cur = &(*cur)[*it];
    }
    const std::string& last = *it;
    if (!cur->contains(last)) return false;
    const json& v = (*cur)[last];
    if (v.is_null() || !v.is_number()) return false;
    int val = v.is_number_integer() ? v.get<int>() : (int)v.get<double>();
    std::string s = std::to_string(val);
    if (target != s) { target = s; return true; }
    return false;
}

} // namespace

//y84
QDSDevice::QDSDevice(const std::string dev_id, const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type, const std::string& model_id, const std::string& firmware_version)
    : m_id(dev_id), m_name(dev_name), m_ip(dev_ip), m_type(dev_type)
    , m_boxData(17), m_boxTemperature(4, 0.0), m_boxHumidity(4, 0), m_model_id(model_id), m_boxState(4, 0), m_boxEndTime(4, 0)
    , m_firmware_version(firmware_version)
{
    //y79
    m_url = "ws://" + dev_url + ":7125/websocket";

    last_update = std::chrono::steady_clock::now();

//y84
    m_config = new DevConfig(this);

    if (!m_firmware_version.empty()) {
        update_config_from_file(model_id);
    }
//y84

    updateFilamentConfig();
}

//y84
QDSDevice::~QDSDevice(){
    m_stop = true;
    if (m_cfg_thread.joinable())
        m_cfg_thread.join();
    delete m_config;
    m_config = nullptr;
}

//y84
json deep_merge(const json& base, const json& override) {
    json result = base;
    
    for (auto it = override.begin(); it != override.end(); ++it) {
        const std::string& key = it.key();
        const json& value = it.value();
        
        if (result.contains(key) && result[key].is_object() && value.is_object()) {
            result[key] = deep_merge(result[key], value);
        } else {
            result[key] = value;
        }
    }
    
    return result;
}

//y84
std::vector<int> parse_version(const std::string& v) {
    std::vector<int> parts;
    std::stringstream ss(v);
    std::string item;
    while (std::getline(ss, item, '.')) {
        if (item.empty()) {
            parts.push_back(0);
            continue;
        }
        try { parts.push_back(std::stoi(item)); }
        catch (...) { parts.push_back(0); }
    }
    while (parts.size() < 4) parts.push_back(0);
    return parts;
}

//y84
bool version_less_equal(const std::string& a, const std::string& b) {
    std::vector<int> va = parse_version(a);
    std::vector<int> vb = parse_version(b);
    size_t n = std::max(va.size(), vb.size());
    for (size_t i = 0; i < n; ++i) {
        int x = (i < va.size()) ? va[i] : 0;
        int y = (i < vb.size()) ? vb[i] : 0;
        if (x != y) return x < y;
    }
    return true;
}

//y84
json merge_versioned_config(const json& config_data, const std::string& device_version) {
    std::vector<std::string> versions;
    for (auto it = config_data.begin(); it != config_data.end(); ++it) {
        versions.push_back(it.key());
    }
    std::sort(versions.begin(), versions.end());
    
    json merged_config;
    for (const auto& version : versions) {
        if (!device_version.empty() && !version_less_equal(version, device_version))
            continue;
        merged_config = deep_merge(merged_config, config_data[version]);
    }
    
    return merged_config;
}

//y84
void QDSDevice::update_config_from_file(std::string model_id){
    std::lock_guard<std::shared_mutex> lock(m_config_mtx);
    std::string config_file = resources_dir() + "/printers/" + model_id + ".json";
    boost::nowide::ifstream json_file(config_file.c_str());
    try{
        json result_json;
        if(json_file.is_open()){
            json_file >> result_json;
            json merged = merge_versioned_config(result_json, m_firmware_version);
            update_device_config(merged);
        }
    }
    catch(...){
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ <<" failed"; 
    }
}

//y84
void QDSDevice::update_device_config(json result_json){
    if(result_json.contains("print")){
        json jj = result_json["print"];
        m_config->ParseConfig(jj);

        if (jj.contains("support_no_sse"))
            support_no_sse = jj["support_no_sse"].get<bool>();
    }
}

void QDSDevice::updateByJsonData(const json& status)
{
    // cj_5  When 'main_status' exists, use 'main_status'; otherwise, use 'sub_status'.
	extract(m_print_msg, status, { "print_stats_manager", "sub_status" });
    std::string main_status;
    json_get(status, main_status, { "print_stats_manager", "main_status" });
    if (main_status == "printing") {
        m_print_msg = "Printing";
    }

	if (extract(m_status, status, { "print_stats", "state" })) {
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
			m_print_msg = "";
		}
	}

	extract(m_print_total_layer, status, { "print_stats", "info", "total_layer" });
	extract(m_print_cur_layer, status, { "print_stats", "info", "current_layer" });

	//cj_4
	{
		std::string plate_idx_str;
		if (json_get(status, plate_idx_str, { "print_stats", "plateindex" }) && !plate_idx_str.empty()) {
			int plate_idx = std::stoi(plate_idx_str);
			if (m_plate_index != plate_idx) {
				is_update = true;
				m_plate_index = plate_idx;
			}
		}
	}
	extract(m_print_filename, status, { "print_stats", "filename" });
    if(!m_print_filename.empty() && m_print_png_url.empty()){
        size_t last_dot = m_print_filename.find_last_of('.');
        if (last_dot != std::string::npos && m_print_filename.size() - last_dot == 4) {
            std::string ext = m_print_filename.substr(last_dot);
            if (ext == ".3mf")
                m_print_png_url = m_ip + "/server/files/.temp/plate_" + std::to_string(m_plate_index) + ".png";
            else
                m_print_png_url = m_ip + "server/files/.temp/plate_.png";
        } else {
            m_print_png_url = m_ip + "server/files/.temp/plate_.png";
        }
    }

	extract_int_str(m_print_total_duration, status, { "print_stats", "total_duration" });
	extract_int_str(m_print_duration, status, { "print_stats", "print_duration" });

	extract_int_str(m_bed_temperature, status, { "heater_bed", "temperature" });
	extract_int_str(m_target_bed, status, { "heater_bed", "target" });
	extract_int_str(m_extruder_temperature, status, { "extruder", "temperature" });
	extract_int_str(m_target_extruder, status, { "extruder", "target" });
	extract_int_str(m_chamber_temperature, status, { "heater_generic chamber", "temperature" });
	extract_int_str(m_target_chamber, status, { "heater_generic chamber", "target" });



	extract(m_print_progress_float, status, { "display_status", "progress" });
	extract_int_str(m_print_progress, status, { "display_status", "progress" });
    if (status.contains("save_variables") && status["save_variables"].is_object()) {
        //y84
        const json& sv = status["save_variables"];
        const json& vars = (sv.contains("variables") && sv["variables"].is_object()) ? sv["variables"] : sv;
        json_get(vars, is_support_detect_spaghetti, { "enable_noodle_detection" });
        json_get(vars, is_support_detect_foreign, { "enable_pre_print_model_check" });
        extract(spaghetti_level, vars, { "noodle_sensitivity_level" });
    }

    //y84
    if (status.contains("multi_color_controller") && status["multi_color_controller"].is_object()) {
        updateBoxDataByJson(status);
    }

	{
		bool pin_on = false;
		if (json_get(status, pin_on, { "output_pin caselight", "value" }) && m_case_light != pin_on) {
			is_update = true;
			m_case_light = pin_on;
		}
	}

	//cj_3
	{
		bool pin_on = false;
		if (json_get(status, pin_on, { "output_pin polar_cooler", "value" }) && m_polar_cooler.load() != pin_on) {
			is_update = true;
			m_polar_cooler = pin_on;
			//cj_4
			m_polar_cooler_dirty_for_ui = true;
		}
	}
	extract(m_auxiliary_fan_speed, status, { "fan_generic auxiliary_cooling_fan", "speed" });
	extract(m_chamber_fan_speed, status, { "fan_generic chamber_circulation_fan", "speed" });
	extract(m_cooling_fan_speed, status, { "fan_generic cooling_fan", "speed" });
	extract(m_home_axes, status, { "toolhead", "homed_axes" });
	extract(m_extruder_filament, status, { "filament_switch_sensor filament_switch_sensor", "filament_detected" });

    apply_gcode_move_speed_percent(*this, status, nullptr);

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

//y84
void QDSDevice::updateBoxDataByJson(const json status)
{
    int count = -1;
    bool has_box = false;
    std::string last_load_slot;

    //y84
    if (!m_firmware_version.empty() && !support_no_sse && is_first_update && !is_net_device && is_selected) {
        wxGetApp().show_dialog(wxString::Format(_L("The firmware version of the device(%s) is too low. Please upgrade it as soon as possible."), m_name));
        is_first_update = false;
    }

    //y84
    bool config_ready = false;
    {
        std::lock_guard<std::mutex> plk(m_pending_mtx);
        config_ready = m_is_init_filamentConfig.load();
        if (!config_ready) {
            m_pending_save_variables = status;
        }
    }
    if (!config_ready) {
        updateFilamentConfig();
    }

    const json& mcc = status["multi_color_controller"];
    json boxContainer = (mcc.contains("box") && mcc["box"].is_object()) ? mcc["box"] : mcc;

    //y84
    // QDS gen-2 box marker (e.g. "box_v2"). Drives the C++ AMSControl rule that
    // hides the Unload button. Reported at the multi_color_controller level.
    if (mcc.contains("identity") && mcc["identity"].is_string())
        m_box_identity = mcc["identity"].get<std::string>();
    else if (boxContainer.contains("identity") && boxContainer["identity"].is_string())
        m_box_identity = boxContainer["identity"].get<std::string>();

    if (json_get(mcc, count, { "box_count" }))
        m_box_count = count;
    else if (json_get(boxContainer, count, { "box_count" }))
        m_box_count = count;
    has_box = m_box_count > 0;

    json_get(mcc, last_load_slot, { "last_load_slot" }) || json_get(boxContainer, last_load_slot, { "last_load_slot" });

    std::vector<json> boxes;
    if (boxContainer.contains("boxes") && boxContainer["boxes"].is_array()) {
        for (const auto& b : boxContainer["boxes"]) {
            if (b.is_object())
                boxes.push_back(b);
        }
    }

    for(int i = 0; i < boxes.size(); ++i){
        const json& box = boxes[i];
    //dk10    
        json_get(box, m_boxTemperature[i], { "aht20_temp" });
        json_get(box, m_boxHumidity[i], { "aht20_humidity" });
        json_get(box, m_boxState[i], { "dry_state" });
        json_get(box, m_boxEndTime[i], { "end_time" });

        if (box.contains("filament_info") && box["filament_info"].is_array()){
            int slot_num = (int)box["filament_info"].size();
            for (int slotInBox = 0; slotInBox < slot_num; ++slotInBox) {
                int boxData_index = i * 4 + slotInBox;

                bool inserted = false;
                std::string bInsKey = "box_inserted_" + std::to_string(slotInBox);
                int ins_val = 0;
                if (json_get(box, ins_val, { bInsKey }))
                    inserted = (ins_val != 0);
                m_boxData[boxData_index].hasMaterial = has_box ? inserted : false;
                if(i >= (int)m_boxData.size() || !has_box || !inserted){
                    m_boxData[boxData_index].hasMaterial = false;
                    continue;
                }

                const json& fi = box["filament_info"][slotInBox];
                json_get(fi, m_boxData[boxData_index].name, { "filament_str" });
                json_get(fi, m_boxData[boxData_index].type, { "filament_type" });
                json_get(fi, m_boxData[boxData_index].vendor, { "vendor_str" });
                if (fi.contains("color_rgb_str") && fi["color_rgb_str"].is_array()
                    && fi["color_rgb_str"].size() > 0 && fi["color_rgb_str"][0].is_string())
                    m_boxData[boxData_index].colorHexCode = fi["color_rgb_str"][0].get<std::string>();
                json_get(fi, m_boxData[boxData_index].minTemp, { "min_temp" });
                json_get(fi, m_boxData[boxData_index].maxTemp, { "max_temp" });
                json_get(fi, m_boxData[boxData_index].boxMinTemp, { "box_min_temp" });
                json_get(fi, m_boxData[boxData_index].boxMaxTemp, { "box_max_temp" });
                json_get(fi, m_boxData[boxData_index].filament_id, { "filament_id" });
            }
        }
    }

    if (mcc.contains("extra") && mcc["extra"].contains("filament_info")
        && mcc["extra"]["filament_info"].is_object()) {
        const json& ex = mcc["extra"]["filament_info"];
        json_get(ex, m_boxData[16].name, { "filament_str" });
        json_get(ex, m_boxData[16].type, { "filament_type" });
        json_get(ex, m_boxData[16].vendor, { "vendor_str" });
        if (ex.contains("color_str") && ex["color_str"].is_array()
            && ex["color_str"].size() > 0 && ex["color_str"][0].is_string())
            m_boxData[16].colorHexCode = ex["color_str"][0].get<std::string>();
        json_get(ex, m_boxData[16].filament_id, { "filament_id" });
    }
    m_boxData[16].hasMaterial = true;

    if (!last_load_slot.empty())
        m_cur_slot = last_load_slot;

    int autoReadInt = -1;
    if (json_get(mcc, autoReadInt, { "auto_read_rfid" }))
        m_auto_read_rfid = bool(autoReadInt);

    int initDetctInt = -1;
    if (json_get(mcc, initDetctInt, { "auto_init_detect" }))
        m_init_detect = bool(initDetctInt);

    int autoReloadInt = -1;
    if (json_get(mcc, autoReloadInt, { "auto_reload_detect" }))
        m_auto_reload_detect = bool(autoReloadInt);

    //y78
    std::vector<int> slot_state(17);
    std::vector<int> slot_id(17);
    std::vector<std::string> filament_id(17);
    std::vector<std::string> filament_colors(17);
    std::vector<std::string> filament_type(17);

    for(int i = 0; i < 17; ++i){
        if(m_boxData[i].hasMaterial){
            slot_state[i] = m_boxData[i].hasMaterial;
            slot_id[i] = i;
            filament_type[i] = m_boxData[i].type;
            filament_colors[i] = m_boxData[i].colorHexCode;
            filament_id[i] = m_boxData[i].filament_id;
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "get msg from box by socket : "<< 
                        ", filament_colors " << filament_colors[i] << ", filament_type " << filament_type[i] << ", filament_id " << filament_id[i] <<
                        ", slot_id " << slot_id[i] << ", slot_state " << slot_state[i];
        }
    }
    m_filament_colors = filament_colors;
    m_filament_type = filament_type;
    m_filament_id = filament_id;
    m_slot_id = slot_id;
    m_slot_state = slot_state;

    //y84
    if(!box_info_is_ready)
        box_info_is_ready = true;

    //y83
    std::string sig;
    sig.reserve(384);
    for (int i = 0; i < 17; ++i) {
        const Filament& f = m_boxData[i];
        sig += (f.hasMaterial ? '1' : '0');
        sig += ':';
        sig += std::to_string(f.filament_idex);
        sig += ':';
        sig += f.name;
        sig += ':';
        sig += f.type;
        sig += ':';
        sig += f.vendor;
        sig += ':';
        sig += f.colorHexCode;
        sig += ';';
    }
    sig += std::to_string(m_box_count);
    sig += '|';
    sig += m_cur_slot;
    sig += '|';
    sig += (m_auto_read_rfid ? '1' : '0');
    sig += (m_init_detect ? '1' : '0');
    sig += (m_auto_reload_detect ? '1' : '0');
    if (sig != m_box_signature) {
        m_box_signature = std::move(sig);
        box_is_update = true;
    }
}

void QDSDevice::updateFilamentConfig()
{
    {
        std::shared_lock<std::shared_mutex> lk(m_config_mtx);
        if (m_is_init_filamentConfig)
            return;
    }

    if (m_fetching.exchange(true))
       return;

    if (m_cfg_thread.joinable())
        m_cfg_thread.join();

    m_cfg_thread = std::thread([this] {
        struct FetchGuard { std::atomic<bool>& f; ~FetchGuard(){ f.store(false); } } guard{ m_fetching };
        std::vector<Filament> localCfg;

        const std::string printer_type = m_type;
        std::string nozzle_str;
        {
            if (!m_nozzle_diameter.empty()) {
                std::ostringstream ns;
                ns << std::fixed << std::setprecision(1) << m_nozzle_diameter.front();
                nozzle_str = ns.str();
            }
        }
        if (nozzle_str.empty())
            nozzle_str = "0.4";

        bool ok = false;
        PresetBundle *bundle = wxGetApp().preset_bundle;
        if (bundle != nullptr && !printer_type.empty()) {
            std::set<std::string> printer_names =
                bundle->get_printer_names_by_printer_type_and_nozzle(printer_type, nozzle_str, /*system_only=*/true);
            const Preset *printer_preset = printer_names.empty() ? nullptr
                                          : bundle->printers.find_preset(*printer_names.begin(), false);
            if (printer_preset != nullptr) {
                PresetWithVendorProfile printer_pvp(*printer_preset, printer_preset->vendor);
                localCfg.push_back(Filament{});
                for (const Preset &filament : bundle->filaments) {
                    PresetWithVendorProfile fil_pvp(filament, filament.vendor);
                    if (!is_compatible_with_printer(fil_pvp, printer_pvp))
                        continue;

                    Filament f;
                    if (auto *t = dynamic_cast<const ConfigOptionStrings *>(filament.config.option("filament_type")))
                        f.type = t->get_at(0);
                    if (auto *v = dynamic_cast<const ConfigOptionStrings *>(filament.config.option("filament_vendor")))
                        f.vendor = v->get_at(0);
                    if (auto *lo = dynamic_cast<const ConfigOptionInts *>(filament.config.option("nozzle_temperature_range_low")))
                        f.minTemp = lo->values.empty() ? 0 : lo->values.front();
                    if (auto *hi = dynamic_cast<const ConfigOptionInts *>(filament.config.option("nozzle_temperature_range_high")))
                        f.maxTemp = hi->values.empty() ? 0 : hi->values.front();
                    if (auto *lo = dynamic_cast<const ConfigOptionInts *>(filament.config.option("box_temperature_range_low")))
                        f.boxMinTemp = lo->values.empty() ? 0 : lo->values.front();
                    if (auto *hi = dynamic_cast<const ConfigOptionInts *>(filament.config.option("box_temperature_range_high")))
                        f.boxMaxTemp = hi->values.empty() ? 0 : hi->values.front();
                    f.filament_id = filament.filament_id;
                    std::string full_f_name = filament.name;
                    f.name = full_f_name.substr(0, full_f_name.find('@'));
                    f.name.erase(f.name.find_last_not_of(' ') + 1);
                    localCfg.push_back(std::move(f));
                }
                ok = localCfg.size() > 1;
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " local filament count=" << (localCfg.size() - 1);
            } else {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " no printer preset for type=" << printer_type
                                           << " nozzle=" << nozzle_str;
            }
        }

        json pending;
        if (ok) {
            std::lock_guard<std::mutex> plk(m_pending_mtx);
            if (!m_stop) {
                m_filamentConfig = std::move(localCfg);
                m_is_init_filamentConfig.store(true);
            }
            pending = std::move(m_pending_save_variables);
            m_pending_save_variables = nullptr;
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " device name " << m_name <<  "updateFilamentConfig ok";
        } else {
            std::lock_guard<std::mutex> plk(m_pending_mtx);
            pending = std::move(m_pending_save_variables);
            m_pending_save_variables = nullptr;
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " device name " << m_name <<" updateFilamentConfig failed";
        }

        if (ok && !m_stop) {
            // if (!pending.is_null())
            //     updateBoxDataByJson(pending);
            reset_update_status();
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " device name " << m_name
                                << (ok ? " updateFilamentConfig ok" : " updateFilamentConfig failed");
    });
}

bool QDSDevice::is_online(){
    return m_status!= "offline";
}

template<typename T>
bool QDSDevice::extract(T& target, const json& j, std::initializer_list<std::string> path)
{
    if (json_get(j, target, path)) {
        is_update = true;
        return true;
    }
    return false;
}

bool QDSDevice::extract_int_str(std::string& target, const json& j, std::initializer_list<std::string> path)
{
    if (json_get_int_str(j, target, path)) {
        is_update = true;
        return true;
    }
    return false;
}

std::vector<float> QDSDevice::getNozzleDiameter(){
    return m_nozzle_diameter;
}

//y79
void QDSDevice::updatePrinterStatusData(json& status){

    if (m_print_msg == "Printing")
        return;

    std::lock_guard<std::mutex> lock(m_process_state_mtx);
    maker_job_is_update = true;
    maker_job_state = status.contains("jobState") ? status["jobState"].get<std::string>() : maker_job_state;
    maker_job_progress = status.contains("progress") ? status["progress"].get<std::string>() : maker_job_progress;

    if(maker_job_state == "Generating_Gcode"){
        m_print_msg = maker_job_state + " layer : " + maker_job_progress;
    } else if(maker_job_state == "SLICING_FINISHED"){
        m_print_msg = maker_job_progress;
    } else {
        m_print_msg = maker_job_state + " : " + maker_job_progress + "%";
    }
    is_update = true;

    if(status.contains("failCause") && !status["failCause"].empty()){
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "some error is " << status << std::endl;
        maker_job_is_update = false;
    }
}

std::string QDSDevice::getMakerJobState(){
    std::lock_guard<std::mutex> lock(m_process_state_mtx);
    return maker_job_state;
}

std::string QDSDevice::getMakerJobProgress(){
    std::lock_guard<std::mutex> lock(m_process_state_mtx);
    return maker_job_progress;
}

void QDSDevice::setMakerJobIsUpdate(bool value) {
    std::lock_guard<std::mutex> lock(m_process_state_mtx);
    maker_job_is_update = value;
}

void QDSDevice::updateAllErrorData(json& jsonData)
{
    {
        std::lock_guard<std::mutex> lock(m_errorData_mtx);
        m_errorData.clear();
    }
    if (jsonData.contains("results") && jsonData["results"].is_array()) {
        for (auto& obj : jsonData["results"]) {
            updateErrorDataSingle(obj, "");
        }
    }
}

void QDSDevice::updateErrorDataForNotiry(json& jsonData)
{
    if (jsonData.contains("data") && jsonData["data"].is_object()) {
        std::string event_value = "";
        if(jsonData["data"].contains("event")){
            event_value = jsonData["data"]["event"].get<std::string>();
        }
        if (jsonData["data"].contains("results") && jsonData["data"]["results"].is_object()) {
            updateErrorDataSingle(jsonData["data"]["results"], event_value);
        }
    } 
}

void QDSDevice::updateErrorDataSingle(json& jsonData, std::string event_value)
{
    QDSDeviceErrorData errorData;
    errorData.event_value = event_value;
	json_get(jsonData, errorData.error_code, { "error_code" });
	json_get(jsonData, errorData.error_message, { "error_message" });
	json_get(jsonData, errorData.error_popup, { "error_popup" });
	json_get(jsonData, errorData.error_type, { "error_type" });
	json_get(jsonData, errorData.error_weight, { "error_weight" });
	json_get(jsonData, errorData.prossess_message, { "prossess_message" });

    std::lock_guard<std::mutex> lock(m_errorData_mtx);

    if(errorData.error_type == 0)
        return;

    if (errorData.event_value.empty() || errorData.event_value == "add") {
        m_errorData.push_back(errorData);
        m_needUpdateErrorData = true;
    }
    else if(errorData.event_value == "update"){
        bool found = false;
        for(auto &err : m_errorData){
            if(err.error_code == errorData.error_code){
                err = errorData;
                found = true;
                break;
            }
        }
        if (!found)
            m_errorData.push_back(errorData);
        m_needUpdateErrorData = true;
    } else if(errorData.event_value == "remove"){
        for (auto it = m_errorData.begin(); it != m_errorData.end(); ) {
            if (it->error_code == errorData.error_code) {
                it = m_errorData.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::sort(m_errorData.begin(), m_errorData.end(),
        [](const QDSDeviceErrorData& a, const QDSDeviceErrorData& b) {
            return a.error_type < b.error_type;
        });
}

//y79
static std::string build_model_list_signature(const json& arr);
static std::string build_timelapse_list_signature(const json& arr);

namespace {
static std::vector<char> g_monitor_placeholder_png;
static std::once_flag    g_monitor_placeholder_once;

const std::vector<char>& get_monitor_placeholder_png()
{
    std::call_once(g_monitor_placeholder_once, []() {
        wxBitmap bitmap = ScalableBitmap(nullptr, "monitor_placeholder", 160).bmp();
        wxImage  image  = bitmap.ConvertToImage();
        if (image.IsOk()) {
            wxMemoryOutputStream mos;
            if (image.SaveFile(mos, wxBITMAP_TYPE_PNG)) {
                const size_t len = mos.GetSize();
                g_monitor_placeholder_png.resize(len);
                if (len > 0)
                    mos.CopyTo(g_monitor_placeholder_png.data(), len);
            }
        }
    });
    return g_monitor_placeholder_png;
}

} // namespace

QDSDeviceManager::QDSDeviceManager() {
    m_ws = std::make_unique<QIDIDeviceWebSocket>();
    // Route websocket events back into the manager's device-state logic.
    m_ws->on_status_changed = [this](const std::string& device_id, const std::string& status) {
        updateDeviceStatus(device_id, status);
    };
    m_ws->on_message_received = [this](const std::string& device_id, const nlohmann::json& message) {
        if (auto dev = getDevice(device_id))
            dev->last_update = std::chrono::steady_clock::now();
        handleDeviceMessage(device_id, message);
    };

    health_check_running_ = true;
    health_check_thread_ = std::thread(&QDSDeviceManager::healthCheckLoop, this);

    get_monitor_placeholder_png();
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
            // 检查设备状态
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
                // 1. 检查设备状态
                if (device->m_status == "Unauthorized")
                    continue;
                if (device->m_status == "offline" || device->m_status == "error") {
                    needs_reconnect = true;
                    reason = "status is " + device->m_status;
                }
                // 2. 检查最后更新时间（超过30秒无更新认为连接假死）
                else if (std::chrono::duration_cast<std::chrono::seconds>(now - device->last_update).count() > 30) {
                    needs_reconnect = true;
                    reason = "no update for " +
                        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(now - device->last_update).count()) + " seconds";
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "device last update : " << (device->last_update).time_since_epoch().count() << std::endl;
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "now time is  : " << now.time_since_epoch().count() << std::endl;
                }
            }

            if (needs_reconnect) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[HealthCheck] Device " << device_id << "device name is " << device->m_name << " needs reconnect: " << reason << std::endl;
                devices_to_reconnect.push_back(device_id);
            }
        }
    }

    // 重新连接需要重连的设备
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
    //y84
    std::thread([this, device]() {
        struct ReconnectGuard {
            std::shared_ptr<QDSDevice> dev;
            ~ReconnectGuard() { if (dev) dev->reconnecting = false; }
        } guard { device };

        device->last_reconnect = std::chrono::steady_clock::now();
        BOOST_LOG_TRIVIAL(info) << "[Reconnect] Reconnecting device " << device->m_id << "..." << std::endl;

        stopConnection(device->m_id);
        connectDevice(device->m_id);
    }).detach();
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

std::shared_ptr<QDSDevice> QDSDeviceManager::getSelectedDevice(){
    std::lock_guard<std::mutex> lock(manager_mutex_);
    for(const auto& [device_id, device] : devices_){
        if(device->is_selected)
            return device;
    }
    return nullptr;
}

void QDSDeviceManager::stopConnection(const std::string& device_id) {
    m_ws->stopConnection(device_id);
}



std::string QDSDeviceManager::addDevice(const std::string& dev_name, const std::string& dev_ip, const std::string& dev_url, const std::string& dev_type, const std::string& model_id) {
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

        auto device = std::make_shared<QDSDevice>(device_id, dev_name, dev_ip, dev_url, dev_type, model_id, "");
        //y79
        device->m_frp_url = "http://" + dev_url ;

        devices_[device_id] = device;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[Manager] Device added: " << device_id << std::endl;
    }

    //y84
    getDeviceInfo(device_id);

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
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "device :" << device << "exit" << std::endl;
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
        //cj_5 Clear Plater sync status badges after device removal
        GUI::wxGetApp().plater()->update_machine_sync_status();
        auto callback = getDeleteDeviceIDCallback();
        if (callback) {
            callback(device_id);
        }
    }
    return removed;
}

bool QDSDeviceManager::connectDevice(const std::string device_id) {
    return m_ws->connect(device_id, getDevice(device_id));
}

bool QDSDeviceManager::disconnectDevice(const std::string& device_id) {
    stopConnection(device_id);
    return true;
}




void QDSDeviceManager::sendSubscribeMessage(const std::string& device_id) {
    m_ws->sendSubscribeMessage(device_id);
}

void QDSDeviceManager::getAllErrorList(const std::string& device_id) {
    m_ws->getAllErrorList(device_id);
}

void QDSDeviceManager::sendCommand(const std::string& device_id, const std::string& script) {
    m_ws->sendCommand(device_id, script);
}

bool QDSDeviceManager::sendCommand(const std::string& device_id, const std::string& scriptName, const std::string& script, const std::string& method) {
    return m_ws->sendCommand(device_id, scriptName, script, method);
}

//y84
bool QDSDeviceManager::sendCommand(const std::string& device_id, const json& params, const std::string& method) {
    return m_ws->sendCommand(device_id, params, method);
}

void QDSDeviceManager::sendActionCommand(const std::string& device_id, const std::string& action_type) {
    m_ws->sendActionCommand(device_id, action_type);
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
    bool resubscribe = false;
    std::shared_ptr<QDSDevice> device = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto dev_it = devices_.find(device_id);
        if (dev_it == devices_.end()) {
            return;
        }
        device = dev_it->second;
        
        //y84
		if (message.contains("method") && message.contains("params")) {
            if (message.at("method").get<std::string>() == "notify_proc_stat_update" && message.at("params").is_array()) {
                const json result = message.at("params").at(0);
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
                        //y84
                        if(result["config_items"].contains("user.alias")){
                            device->m_machine_name = result["config_items"]["user.alias"];
                        }
                    }
                }
                //y84
                if(result.contains("timelapse")){
                    device->timelapse_state = result["timelapse"]["enabled"];
                }
            }
            else if (message.at("method").get<std::string>() == "notify_agent_event" && message.at("params").is_array()) {
                json result = message.at("params").at(0);
                device->updateErrorDataForNotiry(result);
            } else if(message.at("method").get<std::string>() == "notify_status_update" && message.at("params").is_array()){
                const json result = message.at("params").at(0);
                updateDeviceData(device, result, new_status, is_update);
            } else if(message["method"].get<std::string>() == "notify_klippy_ready"){
                resubscribe = true;
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[WS] Klippy ready, re-subscribe device " << device_id << std::endl;
            } else if(message["method"].get<std::string>() == "notify_klippy_disconnected" || message["method"].get<std::string>() == "notify_klippy_shutdown"){
                device->m_status = "offline";
                new_status = "offline";
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[WS] Klippy error" << " for device " << device_id << std::endl;
            }
        }

        
        // 处理状态更新
        if (message.contains("result")) {
            if (message.at("result").contains("files")) {
                const json result = message.at("result");
                json wsArr = json::array();
                if (result.is_object() && result.contains("result") && result["result"].is_array())
                    wsArr = result["result"];
                std::string wsSig = build_model_list_signature(wsArr);
                bool skip_rebuild = false;
                if (!wsSig.empty()) {
                    std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
                    skip_rebuild = (wsSig == device->m_last_model_sig);
                }
                if (!skip_rebuild) {
                    if (updateDeviceFileInfo(device, result)) {
                        std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
                        device->m_last_model_sig = wsSig;
                    }
                }
                is_file_info_update = true;
            }
            if (message.at("result").contains("status")) {
                const json& result = message.at("result").at("status");

                updateDeviceData(device, result, new_status, is_update);
            }

            if (message.at("result").contains("event") && message["result"]["event"].is_string()
                    && message["result"]["event"].get<std::string>() == "GetAll") {
                json jsonResult = message["result"];
                device->updateAllErrorData(jsonResult);
            }
        }
        


        // 处理错误
        if (message.contains("error") && 
            message["error"].contains("message") &&
            message["error"]["message"] == "Unauthorized") {
            device->m_status = "Unauthorized";
            new_status = "Unauthorized";
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "[WS] Unauthorized error for device " << device_id << std::endl;
        }
        
        if (is_update) {
            device->last_update = std::chrono::steady_clock::now();
        }
    }
    
    // 触发回调
    if (!new_status.empty()) {
        updateDeviceStatus(device_id, new_status);
    }
    
    // Klipper restart or reconnect, re-subscribe to the device.
    if (resubscribe) {
        sendSubscribeMessage(device_id);
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

    // y83
    if (device->is_update.exchange(false)) {
        is_update = true;
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


// y84
static void qds_call_after_safe(std::function<void()> fn)
{
    wxApp* app = wxTheApp;
    if (app != nullptr && wxEventLoopBase::GetActive() != nullptr)
        app->CallAfter(std::move(fn));
}

// y84
static std::string json_get_string(const json& obj, const char* key, const std::string& def = std::string())
{
    if (!obj.is_object() || !obj.contains(key))
        return def;
    const json& v = obj[key];
    try {
        if (v.is_string())
            return v.get<std::string>();
        if (v.is_number_unsigned())
            return std::to_string(v.get<uint64_t>());
        if (v.is_number_integer())
            return std::to_string(v.get<int64_t>());
        if (v.is_boolean())
            return v.get<bool>() ? "1" : "0";
        // y86-opt2: 浮点字段（如延时列表的 size/modified）也转成字符串，供列表签名比较使用。
        if (v.is_number_float())
            return std::to_string(v.get<double>());
    } catch (const std::exception&) {
        return def;
    }
    return def;
}

// y84
static std::string build_model_list_signature(const json& arr)
{
    std::string sig;
    if (!arr.is_array())
        return sig;
    for (const auto& f : arr) {
        if (!f.is_object())
            continue;
        sig += json_get_string(f, "filepath");
        sig += '\x1f';
        sig += json_get_string(f, "plate_count", "0");
        sig += '\x1f';
        sig += json_get_string(f, "show_filament_weight");
        sig += '\x1f';
        sig += json_get_string(f, "show_print_time");
        sig += '\n';
    }
    return sig;
}

// y84
static std::string build_timelapse_list_signature(const json& arr)
{
    std::string sig;
    if (!arr.is_array())
        return sig;
    for (const auto& f : arr) {
        if (!f.is_object())
            continue;
        sig += json_get_string(f, "filename");
        sig += '\x1f';
        sig += json_get_string(f, "size");
        sig += '\x1f';
        sig += json_get_string(f, "modified");
        sig += '\n';
    }
    return sig;
}

// y84
std::vector<GCodeFileInfo> QDSDevice::snapshotModelFiles()
{
    std::lock_guard<std::mutex> lock(m_file_info_mtx);
    return file_info;
}

std::vector<TimelapseFileInfo> QDSDevice::snapshotTimelapseFiles()
{
    std::lock_guard<std::mutex> lock(m_timelapse_mtx);
    return timelapse_file_info;
}

// y84
std::vector<std::pair<std::string, std::string>> QDSDeviceManager::collectMissingModelThumbRequests(
    std::shared_ptr<QDSDevice>& device,
    const std::vector<std::pair<std::string, std::string>>& reqs)
{
    std::vector<std::pair<std::string, std::string>> out;
    if (!device)
        return out;

    std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
    for (const auto& req : reqs) {
        bool need = true;
        for (const auto& fi : device->file_info) {
            if (fi.file_path != req.first) continue;
            for (const auto& pi : fi.plates) {
                if (pi.index == req.second && pi.thumbnailLoaded) { need = false; break; }
            }
            if (!need) break;
        }
        if (need) out.push_back(req);
    }
    return out;
}

std::vector<std::string> QDSDeviceManager::collectMissingTimelapseThumbRequests(
    std::shared_ptr<QDSDevice>& device,
    const std::vector<std::string>& jpg_reqs)
{
    std::vector<std::string> out;
    if (!device)
        return out;

    std::lock_guard<std::mutex> lock(device->m_timelapse_mtx);
    for (const auto& jpgReq : jpg_reqs) {
        std::string reqBase = jpgReq;
        size_t rd = reqBase.rfind('.');
        if (rd != std::string::npos) reqBase = reqBase.substr(0, rd);
        bool need = true;
        for (const auto& info : device->timelapse_file_info) {
            std::string infoBase = info.file_name;
            size_t d = infoBase.rfind('.');
            if (d != std::string::npos) infoBase = infoBase.substr(0, d);
            if (infoBase == reqBase && info.thumbnailLoaded) { need = false; break; }
        }
        if (need) out.push_back(jpgReq);
    }
    return out;
}

//y84
bool QDSDeviceManager::updateDeviceFileInfo(std::shared_ptr<QDSDevice>& device, const json& result, bool support_p2p, const std::map<std::string, std::vector<char>>* p2p_thumbnails){
    if (!device) {
        return false;
    }

    json list_array;
    if (support_p2p) {
        if (result.is_object() && result.contains("error")) {
            BOOST_LOG_TRIVIAL(error) << "QDSDeviceManager: updateDeviceFileInfo got error json, skip";
            return false;
        }
        list_array = result.is_array() ? result
                   : (result.is_object() && result.contains("result") && result["result"].is_array())
                         ? result["result"]
                         : json::array();
        if (!(result.is_array()
              || (result.is_object() && result.contains("result") && result["result"].is_array()))) {
            BOOST_LOG_TRIVIAL(error) << "QDSDeviceManager: updateDeviceFileInfo got unexpected json shape, keep existing list";
            return false;
        }
    } else {
        list_array = result["result"];
    }

    std::vector<GCodeFileInfo> new_file_info;
    std::vector<std::pair<std::string, std::string>> pending_thumbnails;

    std::map<std::string, QDSPlateInfo> old_plate_thumbs;
    {
        std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
        for (const auto& old_fi : device->file_info) {
            for (const auto& old_pi : old_fi.plates) {
                if (old_pi.thumbnailLoaded)
                    old_plate_thumbs[old_fi.file_path + "|" + old_pi.index] = old_pi;
            }
        }
    }

    const auto& result_array = list_array;
    //y78
    for(const auto& file_item : result_array){
        GCodeFileInfo file_info;
        //cj_2 filter cache file
        file_info.file_path = json_get_string(file_item, "filepath");
        if (file_info.file_path.find("/.cache/")!= std::string::npos) {
            continue;
        }
        file_info.extension = json_get_string(file_item, "extension");
        //file_info.file_name = file_item["filename"].get<std::string>();
        file_info.file_name = extractAfterGcodes(json_get_string(file_item, "filepath"));
        file_info.plate_count = json_get_string(file_item, "plate_count", "0");
        file_info.show_filament_weight = json_get_string(file_item, "show_filament_weight");
        file_info.show_print_time = json_get_string(file_item, "show_print_time");

        int plate_count = 0;
        try { plate_count = std::stoi(file_info.plate_count); }
        catch (const std::exception&) { plate_count = 0; }
        if(plate_count > 0 && file_item.contains("plates") && file_item["plates"].is_array()){
            auto plates_array = file_item["plates"];
            for(const auto& plate_item : plates_array){

                QDSPlateInfo plate_info;
                plate_info.index = json_get_string(plate_item, "plate_index", "0");
                {
                    std::string tmp;
                    tmp = json_get_string(plate_item, "filament_colour");
                    if (!tmp.empty()) boost::split(plate_info.filament_colours, tmp, boost::is_any_of(";"));
                    tmp = json_get_string(plate_item, "filament_type");
                    if (!tmp.empty()) boost::split(plate_info.filament_types, tmp, boost::is_any_of(";"));
                    tmp = json_get_string(plate_item, "used_extruders");
                    if (!tmp.empty()) boost::split(plate_info.used_extruders, tmp, boost::is_any_of(";"));
                }
                plate_info.filament_weight = json_get_string(plate_item, "filament_weight");
                plate_info.print_time = json_get_string(plate_item, "print_time");
                plate_info.nozzle_diameter = json_get_string(plate_item, "nozzle_diameter");

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

                const std::vector<char>& placeholder = get_monitor_placeholder_png();
                if (!placeholder.empty()) {
                    plate_info.thumbnailData.pixels.assign(placeholder.begin(), placeholder.end());
                }

                bool thumb_carried = false;
                auto oldThumbIt = old_plate_thumbs.find(file_info.file_path + "|" + plate_info.index);
                if (oldThumbIt != old_plate_thumbs.end()) {
                    plate_info.thumbnailData    = oldThumbIt->second.thumbnailData;
                    plate_info.thumbnailFromP2P = oldThumbIt->second.thumbnailFromP2P;
                    plate_info.thumbnailLoaded  = true;
                    thumb_carried               = true;
                }

                file_info.plates.emplace_back(plate_info);

                // y83
                if (support_p2p) {
                    const std::map<std::string, std::vector<char>>& thumb_map =
                        (p2p_thumbnails != nullptr) ? *p2p_thumbnails : m_p2p_thumbnails;
                    std::string p2pKey = file_info.file_path + "|" + plate_info.index;
                    auto it = thumb_map.find(p2pKey);
                    if (it != thumb_map.end() && !it->second.empty()) {
                        // Update the already-emplaced plate_info's thumbnailData
                        auto &emplaced = file_info.plates.back();
                        emplaced.thumbnailData.pixels.assign(
                            (const unsigned char *)it->second.data(),
                        (const unsigned char *)it->second.data() + it->second.size());
                        //y84
                        emplaced.thumbnailFromP2P = true;
                        emplaced.thumbnailLoaded  = true;
                        BOOST_LOG_TRIVIAL(trace) << "QDSDeviceManager: use P2P thumbnail for "
                            << p2pKey << " (" << it->second.size() << " bytes)";
                    } else if (thumb_carried) {
                        BOOST_LOG_TRIVIAL(trace) << "QDSDeviceManager: reuse device-cached thumbnail for " << p2pKey;
                    } else {
                        BOOST_LOG_TRIVIAL(trace) << "QDSDeviceManager: no P2P thumbnail for "
                            << p2pKey << " (available=" << thumb_map.size();
                    }
            } else if (!thumb_carried) {
                    pending_thumbnails.emplace_back(UrlEncodeForFilename(plate_info.thumb_url), file_info.file_name);
                }
            }
        }
        file_info.show_thumb_url = file_info.plates.empty() ? "" : file_info.plates[0].thumb_url;

        if (file_item.contains("thumbnails")) {
            const auto& thumbnails = file_item["thumbnails"];
            if (thumbnails.is_array()) {
                for (const auto& thumbnailItem : thumbnails) {
                    file_info.thumbnailsSize = thumbnailItem.value("data_size", 0);
                    break;
                }
            }
        }


        new_file_info.emplace_back(file_info);
    }

    {
        std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
        device->file_info = std::move(new_file_info);
    }
    device->m_fresh_file_info = true;
    device->m_model_list_loaded = true;

    for (const auto& thumb_req : pending_thumbnails) {
        DownloadManager::getInstance().downloadThumbnail(
            thumb_req.first,
            thumb_req.second,
            [device, this](ThumbnailResult result) {
                if (!result.success)
                    return;

                std::string cb_file;
                std::vector<uint8_t> cb_png;
                {
                    std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
                    for (auto& file_info_item : device->file_info) {
                        for (auto& plate_info_item : file_info_item.plates) {
                            if (UrlEncodeForFilename(plate_info_item.thumb_url) == result.url) {
                                plate_info_item.thumbnailData.pixels.assign(result.png_data.begin(), result.png_data.end());
                                plate_info_item.thumbnailFromP2P = false;
                                plate_info_item.thumbnailLoaded = true;
                                cb_file = file_info_item.file_name;
                                cb_png.assign(result.png_data.begin(), result.png_data.end());
                                break;
                            }
                        }
                        if (!cb_file.empty())
                            break;
                    }
                }
                auto thumb_cb = getFileThumbnailReadyCallback();
                if (thumb_cb && !cb_file.empty()) {
                    const uint64_t gen = device->m_file_gen.load();
                    qds_call_after_safe([thumb_cb, device_id = device->m_id, cb_file, cb_png, gen, this]() {
                        auto dev = getDevice(device_id);
                        if (!dev || dev->m_file_gen.load() != gen)
                            return;
                        thumb_cb(device_id, /*is_timelapse=*/false, cb_file, cb_png);
                    });
                }
            }
        );
    }
    return true;
}

void QDSDeviceManager::updateDeviceStatus(const std::string& device_id, std::string new_status) {
    bool should_callback = false;

    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        auto dev_it = devices_.find(device_id);
        if (dev_it != devices_.end()) {
            if (!new_status.empty() && dev_it->second->m_status != new_status) {
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
    std::vector<std::string> device_ids = m_ws->getConnectionIds();
    if (device_ids.empty()) return;

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
    
    {
        std::lock_guard<std::mutex> lock(manager_mutex_);
        for (int i = 0; i < 17; ++i) {
            if (device->m_boxData[i].hasMaterial) {
                slot_state[i] = device->m_boxData[i].hasMaterial;
                slot_id[i] = i;
                filament_type[i] = device->m_boxData[i].type;
                filament_colors[i] = device->m_boxData[i].colorHexCode;
                filament_id[i] = device->m_boxData[i].filament_id;
            }
        }
        box_count = device->m_box_count;
        auto_reload_detect = device->m_auto_reload_detect;
        box_list_preset_name = device->m_type;
    }

    GUI::wxGetApp().sidebar().update_sync_status(device, /*defer_combo_refresh=*/true);

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

//y84
static bool p2p_reply_is_error(const std::string& text)
{
    if (text.empty())
        return false;
    try {
        json j = json::parse(text);
        if (j.is_object() && j.contains("error"))
            return true;
    } catch (...) {
        return false;
    }
    return false;
}

//y84
bool QDSDeviceManager::getFileInfoViaP2P(std::shared_ptr<QDSDevice> device, std::vector<std::pair<std::string, std::string>>& out_thumb_reqs, std::string& out_text)
{
#if QDT_RELEASE_TO_PUBLIC
    if (!device)
        return false;
    QIDIFileManager qdsfmsg(device);

    // ── Step 1: fetch file list (text command) ──
    auto reply = qdsfmsg.fetchModelList();
    if (!reply.ok) {
        BOOST_LOG_TRIVIAL(info) << "QDSDeviceManager: "
            << (reply.timed_out ? "fetch_model_list timeout"
                                : "failed to send fetch_model_list");
        if (!reply.timed_out)
            P2PManager::instance().triggerReconnect();
        return false;
    }
    if (p2p_reply_is_error(reply.text)) {
        BOOST_LOG_TRIVIAL(info) << "QDSDeviceManager: fetch_model_list returned error: " << reply.text;
        return false;
    }
    std::string fileListJson = std::move(reply.text);

    out_text = fileListJson;

    try {
        json parsed = json::parse(fileListJson);
        json arr = parsed.is_array() ? parsed : (parsed.contains("result") ? parsed["result"] : parsed);
        if (arr.is_array()) {
            for (const auto &file : arr) {
                std::string filePath = json_get_string(file, "filepath");
                if (filePath.find("/.cache/") != std::string::npos)
                    continue;
                if (file.contains("plates") && file["plates"].is_array()) {
                    for (const auto &plate : file["plates"]) {
                        out_thumb_reqs.emplace_back(filePath, json_get_string(plate, "plate_index", "0"));
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "QDSDeviceManager: parse file list failed: " << e.what();
        return false;
    }
#endif
    return true;
}

// y84
void QDSDeviceManager::fetchModelThumbnailsP2P(std::shared_ptr<QDSDevice> device,
                                              const std::string& device_id,
                                              const std::vector<std::pair<std::string, std::string>>& thumb_reqs,
                                              uint64_t gen)
{
#if QDT_RELEASE_TO_PUBLIC
    if (!device || thumb_reqs.empty())
        return;
    if (device->m_file_gen.load() != gen)
        return;

    QIDIFileManager qdsfmsg(device);

    P2PManager::FileTransferOptions thumbOpt;
    thumbOpt.timeout                = std::chrono::seconds(5);
    thumbOpt.send_retries           = 1;
    thumbOpt.reassemble_by_sequence = false;
    thumbOpt.strip_chunk_header     = true;

    BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] fetchModelThumbnailsP2P enter: device_id=" << device_id
                             << " reqs=" << thumb_reqs.size() << " gen=" << gen;
    for (const auto& req : thumb_reqs) {
        if (device->m_file_gen.load() != gen)
            return;
        if (!device->active_p2p.load()) {
            P2PManager::instance().triggerReconnect();
            return;
        }

        BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] fetch start: file=" << req.first << " plate=" << req.second;
        std::vector<char> data;
        bool              ok       = false;
        bool              timedOut = false;
        {
            std::lock_guard<std::mutex> xfer_lock(device->m_p2p_xfer_mtx);
            auto thumb = qdsfmsg.fetchModelThumbnail(req.first, req.second, thumbOpt);
            ok       = thumb.ok;
            timedOut = thumb.timed_out;
            data     = std::move(thumb.data);
        }
        BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] fetch done: file=" << req.first << " plate=" << req.second
                                 << " ok=" << ok << " timedOut=" << timedOut << " bytes=" << data.size();
        if (!ok) {
            if (!timedOut) {
                BOOST_LOG_TRIVIAL(warning) << "QDSDeviceManager: thumbnail send failed for "
                                           << req.first << " plate=" << req.second
                                           << ", trigger P2P reconnect";
                P2PManager::instance().triggerReconnect();
                return;
            }
            continue;
        }
        if (device->m_file_gen.load() != gen)
            return;

        bool        wrote = false;
        std::string wrote_file_name;
        {
            std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
            for (auto& fi : device->file_info) {
                if (fi.file_path != req.first) continue;
                for (auto& pi : fi.plates) {
                    if (pi.index == req.second) {
                        pi.thumbnailData.pixels.assign(
                            (const unsigned char*)data.data(),
                            (const unsigned char*)data.data() + data.size());
                        pi.thumbnailFromP2P = true;
                        pi.thumbnailLoaded  = true;
                        wrote           = true;
                        wrote_file_name = fi.file_name;
                        break;
                    }
                }
                if (wrote) break;
            }
        }
        BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] write back: file=" << req.first << " plate=" << req.second
                                 << " wrote=" << wrote << " bytes=" << data.size()
                                 << (wrote ? (" name=" + wrote_file_name) : " (no matching plate in device->file_info)");

        if (wrote && device->m_file_gen.load() == gen) {
            std::vector<uint8_t> png;
            png.assign(data.begin(), data.end());
            auto thumb_cb = getFileThumbnailReadyCallback();
            BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] thumb_cb=" << (thumb_cb ? "registered" : "null(fallback full refresh)")
                                     << " name=" << wrote_file_name << " png_bytes=" << png.size();
            if (thumb_cb) {
                qds_call_after_safe([thumb_cb, device_id, wrote_file_name, png, gen, this]() {
                    auto dev = getDevice(device_id);
                    if (!dev || dev->m_file_gen.load() != gen) return;
                    thumb_cb(device_id, /*is_timelapse=*/false, wrote_file_name, png);
                });
            } else {
                device->m_fresh_file_info = true;
                qds_call_after_safe([this, device_id, gen]() {
                    auto dev = getDevice(device_id);
                    if (!dev || dev->m_file_gen.load() != gen) return;
                    auto cb = getFileInfoUpdateCallback();
                    if (cb) cb(device_id);
                });
            }
        }
    }
#endif
}

//y84
bool QDSDeviceManager::getTimelapseInfoP2P(std::shared_ptr<QDSDevice> device, std::vector<std::string>& out_jpg_reqs, std::string& out_text){
#if QDT_RELEASE_TO_PUBLIC
    if (!device)
        return false;
    QIDIFileManager qdsfmsg(device);

    // ── Step 1: fetch timelapse list (text command) ──
    auto reply = qdsfmsg.fetchTimelapseList();
    if (!reply.ok) {
        BOOST_LOG_TRIVIAL(error) << "QDSDeviceManager: "
            << (reply.timed_out ? "fetch_timelapse_list timeout"
                                : "failed to send fetch_timelapse_list");
        if (!reply.timed_out)
            P2PManager::instance().triggerReconnect();
        return false;
    }
    if (p2p_reply_is_error(reply.text)) {
        BOOST_LOG_TRIVIAL(error) << "QDSDeviceManager: fetch_timelapse_list returned error: " << reply.text;
        return false;
    }
    std::string fileListJson = std::move(reply.text);

    out_text = fileListJson;

    // ── Step 2: extract .jpg thumbnail filenames from the list ──
    try {
        json parsed = json::parse(fileListJson);
        // The JSON may be a direct array or wrapped in {"result": {...}}
        const json *pArr = nullptr;
        if (parsed.is_array()) {
            pArr = &parsed;
        } else if (parsed.contains("result") && parsed["result"].is_object()
                   && parsed["result"].contains("files") && parsed["result"]["files"].is_array()) {
            pArr = &parsed["result"]["files"];
        }
        if (pArr) {
            std::unordered_set<std::string> allNames;
            for (const auto &f : *pArr) {
                if (f.is_object() && f.contains("filename") && f["filename"].is_string())
                    allNames.insert(f["filename"].get<std::string>());
            }
            for (const auto &f : *pArr) {
                if (!f.is_object() || !f.contains("filename") || !f["filename"].is_string())
                    continue;
                std::string fname = f["filename"].get<std::string>();
                size_t dot = fname.rfind('.');
                if (dot == std::string::npos)
                    continue;
                std::string ext = fname.substr(dot);
                for (char &c : ext)
                    c = (char)std::tolower((unsigned char)c);
                if (ext != ".mp4")
                    continue;
                std::string jpgName = fname.substr(0, dot) + ".jpg";
                if (allNames.find(jpgName) != allNames.end()) {
                    out_jpg_reqs.push_back(std::move(jpgName));
                }
            }
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "QDSDeviceManager: parse timelapse list failed: " << e.what();
        return false;
    }
#endif
    return true;
}

// y84
void QDSDeviceManager::fetchTimelapseThumbnailsP2P(std::shared_ptr<QDSDevice> device,
                                                   const std::string& device_id,
                                                   const std::vector<std::string>& jpg_reqs,
                                                   uint64_t gen)
{
#if QDT_RELEASE_TO_PUBLIC
    if (!device || jpg_reqs.empty())
        return;
    if (device->m_file_gen.load() != gen)
        return;

    QIDIFileManager qdsfmsg(device);

    P2PManager::FileTransferOptions thumbOpt;
    thumbOpt.timeout                = std::chrono::seconds(5);
    thumbOpt.send_retries           = 1;
    thumbOpt.reassemble_by_sequence = false;
    thumbOpt.strip_chunk_header     = true;

    BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] fetchTimelapseThumbnailsP2P enter: device_id=" << device_id
                             << " reqs=" << jpg_reqs.size() << " gen=" << gen;
    for (const auto& jpgReq : jpg_reqs) {
        if (device->m_file_gen.load() != gen)
            return;
        if (!device->active_p2p.load()) {
            P2PManager::instance().triggerReconnect();
            return;
        }

        std::vector<char> data;
        bool              ok       = false;
        bool              timedOut = false;
        {
            std::lock_guard<std::mutex> xfer_lock(device->m_p2p_xfer_mtx);
            auto thumb = qdsfmsg.fetchTimelapseThumbnail("/home/qidi/printer_data/timelapse/" + jpgReq, thumbOpt);
            ok       = thumb.ok;
            timedOut = thumb.timed_out;
            data     = std::move(thumb.data);
        }
        BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] fetch done: timelapse jpgReq=" << jpgReq
                                 << " ok=" << ok << " timedOut=" << timedOut << " bytes=" << data.size();
        if (!ok) {
            if (!timedOut) {
                BOOST_LOG_TRIVIAL(warning) << "QDSDeviceManager: timelapse thumbnail send failed for "
                                           << jpgReq << ", trigger P2P reconnect";
                P2PManager::instance().triggerReconnect();
                return;
            }
            continue;
        }
        if (device->m_file_gen.load() != gen)
            return;

        std::string reqBase = jpgReq;
        size_t rd = reqBase.rfind('.');
        if (rd != std::string::npos) reqBase = reqBase.substr(0, rd);
        bool wrote = false;
        {
            std::lock_guard<std::mutex> lock(device->m_timelapse_mtx);
            for (auto& info : device->timelapse_file_info) {
                std::string infoBase = info.file_name;
                size_t d = infoBase.rfind('.');
                if (d != std::string::npos) infoBase = infoBase.substr(0, d);
                if (infoBase == reqBase) {
                    info.thumbnailData.pixels.assign(
                        (const unsigned char*)data.data(),
                        (const unsigned char*)data.data() + data.size());
                    info.thumbnailLoaded = true;
                    wrote = true;
                    break;
                }
            }
        }
        BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] write back: timelapse jpgReq=" << jpgReq
                                 << " wrote=" << wrote << " bytes=" << data.size();

        if (wrote && device->m_file_gen.load() == gen) {
            std::vector<uint8_t> jpg;
            jpg.assign(data.begin(), data.end());
            std::string wrote_file_name = reqBase + ".mp4";
            {
                std::lock_guard<std::mutex> lock(device->m_timelapse_mtx);
                for (const auto& info : device->timelapse_file_info) {
                    std::string b = info.file_name;
                    size_t d = b.rfind('.');
                    if (d != std::string::npos) b = b.substr(0, d);
                    if (b == reqBase) { wrote_file_name = info.file_name; break; }
                }
            }
            auto thumb_cb = getFileThumbnailReadyCallback();
            BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] thumb_cb(timelapse)=" << (thumb_cb ? "registered" : "null(fallback full refresh)")
                                     << " name=" << wrote_file_name << " jpg_bytes=" << jpg.size();
            if (thumb_cb) {
                qds_call_after_safe([thumb_cb, device_id, wrote_file_name, jpg, gen, this]() {
                    auto dev = getDevice(device_id);
                    if (!dev || dev->m_file_gen.load() != gen) return;
                    thumb_cb(device_id, /*is_timelapse=*/true, wrote_file_name, jpg);
                });
            } else {
                device->m_fresh_timelapse_file_info = true;
                qds_call_after_safe([this, device_id, gen]() {
                    auto dev = getDevice(device_id);
                    if (!dev || dev->m_file_gen.load() != gen) return;
                    auto cb = getFileInfoUpdateCallback();
                    if (cb) cb(device_id);
                });
            }
        }
    }
#endif
}

//y84
void QDSDeviceManager::getFileInfo(const std::string& device_id){
    std::shared_ptr<QDSDevice> device = getDevice(device_id);
    const uint64_t gen = device ? (++device->m_file_gen) : 0;

    //y83
    if(device && device->active_p2p){
        new std::thread([this, device_id, gen](){
            std::shared_ptr<QDSDevice> device = getDevice(device_id);
            if (!device) return;
            BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] getFileInfo P2P path start: device_id=" << device_id
                                     << " gen=" << gen << " active_p2p=" << device->active_p2p.load();

            std::vector<std::pair<std::string, std::string>> p2p_thumb_reqs;
            std::string p2p_text;
            bool has_p2p_result = getFileInfoViaP2P(device, p2p_thumb_reqs, p2p_text);
            BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] model list fetched: has_p2p_result=" << has_p2p_result
                                     << " p2p_thumb_reqs=" << p2p_thumb_reqs.size();
            std::vector<std::pair<std::string, std::string>> model_reqs_to_fetch;
            json bodyJson;
            if (has_p2p_result && device->m_file_gen.load() == gen) {
                try { bodyJson = json::parse(p2p_text); }
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "getFileInfo: parse model list json failed: " << e.what();
                    has_p2p_result = false;
                }
            }
            if(has_p2p_result && device->m_file_gen.load() == gen){
                json modelArr = bodyJson.is_array() ? bodyJson
                              : (bodyJson.is_object() && bodyJson.contains("result") && bodyJson["result"].is_array())
                                    ? bodyJson["result"] : json::array();
                std::string modelSig = build_model_list_signature(modelArr);
                if (updateDeviceFileInfo(device, bodyJson, true, nullptr)) {
                    {
                        std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
                        device->m_last_model_sig = modelSig;
                    }
                    device->m_file_info_load_failed = false;
                    model_reqs_to_fetch = collectMissingModelThumbRequests(device, p2p_thumb_reqs);
                    BOOST_LOG_TRIVIAL(trace) << "[P2P_THUMB] missing model thumbs to fetch=" << model_reqs_to_fetch.size();
                } else {
                    BOOST_LOG_TRIVIAL(error) << "getFileInfo: updateDeviceFileInfo rejected model list, keep existing";
                }
                auto file_cb = getFileInfoUpdateCallback();
                if (file_cb) file_cb(device_id);
            } else if (device->m_file_gen.load() == gen) {
                BOOST_LOG_TRIVIAL(error) << "getFileInfo failed!";
                device->m_file_info_load_failed = true;
                auto file_cb = getFileInfoUpdateCallback();
                if (file_cb) file_cb(device_id);
            }
            std::vector<std::string> p2p_jpg_reqs;
            bool p2p_get_timelapse_file = getTimelapseInfoP2P(device, p2p_jpg_reqs, p2p_text);
            std::vector<std::string> timelapse_reqs_to_fetch;
            json tlJson;
            if (p2p_get_timelapse_file && device->m_file_gen.load() == gen) {
                try { tlJson = json::parse(p2p_text); }
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "getFileInfo: parse timelapse list json failed: " << e.what();
                    p2p_get_timelapse_file = false;
                }
            }
            if(p2p_get_timelapse_file && device->m_file_gen.load() == gen){
                json tlFiles = json::array();
                if (tlJson.is_object() && tlJson.contains("result") && tlJson["result"].is_object()
                    && tlJson["result"].contains("files") && tlJson["result"]["files"].is_array())
                    tlFiles = tlJson["result"]["files"];
                std::string tlSig = build_timelapse_list_signature(tlFiles);
                if (updateDeviceTimelapseFileInfo(device, p2p_text, nullptr)) {
                    {
                        std::lock_guard<std::mutex> lock(device->m_timelapse_mtx);
                        device->m_last_timelapse_sig = tlSig;
                    }
                    device->m_timelapse_info_load_failed = false;
                    timelapse_reqs_to_fetch = collectMissingTimelapseThumbRequests(device, p2p_jpg_reqs);
                } else {
                    BOOST_LOG_TRIVIAL(error) << "getFileInfo: updateDeviceTimelapseFileInfo rejected list, keep existing";
                }
                auto file_cb = getFileInfoUpdateCallback();
                if (file_cb) file_cb(device_id);
            } else if (device->m_file_gen.load() == gen) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "getTimelapseInfo failed!";
                device->m_timelapse_info_load_failed = true;
                auto file_cb = getFileInfoUpdateCallback();
                if (file_cb) file_cb(device_id);
            }

            if (has_p2p_result && !model_reqs_to_fetch.empty())
                fetchModelThumbnailsP2P(device, device_id, model_reqs_to_fetch, gen);

            if (p2p_get_timelapse_file && !timelapse_reqs_to_fetch.empty())
                fetchTimelapseThumbnailsP2P(device, device_id, timelapse_reqs_to_fetch, gen);
        });
        return;
    }

    new std::thread([this, device_id, gen]() {
        std::shared_ptr<QDSDevice> device = getDevice(device_id);
        if (!device) {
            return;
        }

        std::string file_list_body;
        if (QIDIDeviceApi::get_file_list(device->m_frp_url, file_list_body)) {
            try {
                json bodyJson = json::parse(file_list_body);
                if (bodyJson.contains("result")) {
                    if (device->m_file_gen.load() == gen) {
                        device->m_file_info_load_failed = false;
                        json arr = json::array();
                        if (bodyJson["result"].is_array())
                            arr = bodyJson["result"];
                        else if (bodyJson["result"].is_object() && bodyJson["result"].contains("files")
                                 && bodyJson["result"]["files"].is_array())
                            arr = bodyJson["result"]["files"];
                        const std::string sig = build_model_list_signature(arr);
                        bool changed = true;
                        {
                            std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
                            changed = !device->m_model_list_loaded.load() || (sig != device->m_last_model_sig);
                        }
                        if (changed && updateDeviceFileInfo(device, bodyJson)) {
                            std::lock_guard<std::mutex> lock(device->m_file_info_mtx);
                            device->m_last_model_sig = sig;
                        }
                    }
                }
                else if (device->m_file_gen.load() == gen)
                    device->m_file_info_load_failed = true;
            }
            catch (const std::exception&) {
                if (device->m_file_gen.load() == gen)
                    device->m_file_info_load_failed = true;
            }
        }
        else if (device->m_file_gen.load() == gen)
            device->m_file_info_load_failed = true;
        std::string timelapse_body;
        if (QIDIDeviceApi::get_timelapse_directory(device->m_frp_url, timelapse_body)) {
            try {
                json bodyJson = json::parse(timelapse_body);
                if (bodyJson.contains("result")) {
                    if (device->m_file_gen.load() == gen) {
                        device->m_timelapse_info_load_failed = false;
                        json tlFiles = json::array();
                        if (bodyJson["result"].is_object() && bodyJson["result"].contains("files")
                            && bodyJson["result"]["files"].is_array())
                            tlFiles = bodyJson["result"]["files"];
                        const std::string tlSig = build_timelapse_list_signature(tlFiles);
                        bool changed = true;
                        {
                            std::lock_guard<std::mutex> lock(device->m_timelapse_mtx);
                            changed = !device->m_timelapse_list_loaded.load() || (tlSig != device->m_last_timelapse_sig);
                        }
                        if (changed && updateDeviceTimelapseFileInfo(device, timelapse_body)) {
                            std::lock_guard<std::mutex> lock(device->m_timelapse_mtx);
                            device->m_last_timelapse_sig = tlSig;
                        }
                    }
                }
                else {
                    BOOST_LOG_TRIVIAL(error) << "getTimelapseInfo failed: missing result";
                    if (device->m_file_gen.load() == gen)
                        device->m_timelapse_info_load_failed = true;
                }
            }
            catch (const std::exception& error) {
                BOOST_LOG_TRIVIAL(trace) << "timelapse json error " << error.what();
                if (device->m_file_gen.load() == gen)
                    device->m_timelapse_info_load_failed = true;
            }
        }
        else if (device->m_file_gen.load() == gen)
            device->m_timelapse_info_load_failed = true;

        if (device->m_file_gen.load() == gen) {
            auto file_cb = getFileInfoUpdateCallback();
            if (file_cb) {
                file_cb(device_id);
            }
        }
    });
}

//cj_3 y84
bool QDSDeviceManager::updateDeviceTimelapseFileInfo(std::shared_ptr<QDSDevice>& device, const std::string& response_body, const std::map<std::string, std::vector<char>>* p2p_timelapse_thumbnails)
{
    if (!device) {
        return false;
    }

    std::vector<TimelapseFileInfo> new_timelapse_info;
    std::map<std::string, TimelapseFileInfo> old_timelapse_thumbs;
    {
        std::lock_guard<std::mutex> lock(device->m_timelapse_mtx);
        for (const auto& old_info : device->timelapse_file_info) {
            if (old_info.thumbnailLoaded)
                old_timelapse_thumbs[old_info.file_name] = old_info;
        }
    }

    try {
        json bodyJson = json::parse(response_body);

        //cj_3
        if (!bodyJson.contains("result") || !bodyJson["result"].is_object()) {
            device->m_fresh_timelapse_file_info = true;
            return false;
        }
        const json& res = bodyJson["result"];
        if (!res.contains("files") || !res["files"].is_array()) {
            device->m_fresh_timelapse_file_info = true;
            return false;
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

            bool tl_thumb_carried = false;
            auto oldTlIt = old_timelapse_thumbs.find(fname);
            if (oldTlIt != old_timelapse_thumbs.end()) {
                info.thumbnailData   = oldTlIt->second.thumbnailData;
                info.thumbnailLoaded = true;
                tl_thumb_carried     = true;
            }

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
            

            // Try to populate thumbnail from P2P-fetched data first
            const std::map<std::string, std::vector<char>>& tl_map =
                (p2p_timelapse_thumbnails != nullptr) ? *p2p_timelapse_thumbnails : m_p2p_timelapse_thumbnails;
            auto p2pIt = tl_map.find(jpg_name);
            if (p2pIt != tl_map.end() && !p2pIt->second.empty()) {
                info.thumbnailData.pixels.assign(
                    (const unsigned char *)p2pIt->second.data(),
                    (const unsigned char *)p2pIt->second.data() + p2pIt->second.size());
                info.thumbnailLoaded = true;
                BOOST_LOG_TRIVIAL(trace) << "QDSDeviceManager: use P2P timelapse thumbnail "
                                         << jpg_name << " (" << p2pIt->second.size() << " bytes)";
            } else if (tl_thumb_carried) {
                BOOST_LOG_TRIVIAL(trace) << "QDSDeviceManager: reuse device-cached timelapse thumbnail " << jpg_name;
            }


            if (name_set.find(jpg_name) != name_set.end()) {
                info.thumb_url = device->m_frp_url + "/server/files/timelapse/" + UrlEncodeForFilename(jpg_name);
            }

            new_timelapse_info.push_back(std::move(info));
        }
    }
    catch (const std::exception& err) {
        BOOST_LOG_TRIVIAL(trace) << "timelapse directory json error " << err.what();
    }

    {
        std::lock_guard<std::mutex> lock(device->m_timelapse_mtx);
        device->timelapse_file_info = std::move(new_timelapse_info);
    }
    device->m_fresh_timelapse_file_info = true;
    device->m_timelapse_list_loaded = true;
    return true;
}

void QDSDeviceManager::resetBoxUpdateStatus(const std::string& device_id) {
    std::shared_ptr<QDSDevice> device = getDevice(device_id);
    if (device) {
        device->reset_update_status();
    }
}

//y84
void QDSDeviceManager::getDeviceInfo(const std::string& device_id){
    new std::thread([this, device_id]() {
        std::shared_ptr<QDSDevice> device = getDevice(device_id);
        if (!device) {
            return;
        }

        std::string system_info_body;
        if (QIDIDeviceApi::get_system_info(device->m_frp_url, system_info_body)) {
            try {
                json bodyJson = json::parse(system_info_body);
                std::string mac_address = bodyJson.value("result", json::object())
                                                    .value("system_info", json::object())
                                                    .value("network", json::object())
                                                    .value("wlan0", json::object())
                                                    .value("mac_address", "");
                std::string serial_number = bodyJson.value("result", json::object())
                                                    .value("system_info", json::object())
                                                    .value("cpu_info", json::object())
                                                    .value("serial_number", "");
                device->m_mac_address =  mac_address;
                device->m_serial_number = serial_number;
            }
            catch (const std::exception&) {
            }
        }

        std::string client_info_body;
        if (QIDIDeviceApi::get_client_info(device->m_frp_url, client_info_body)) {
            try {
                json bodyJson = json::parse(client_info_body);
                if(bodyJson["result"].contains("clients")){
                    for(const auto& client : bodyJson["result"]["clients"]){
                        if (client.contains("client_type") && 
                            client["client_type"].is_string() && 
                            client["client_type"] == "agent") {
                            
                            if (client.contains("client_version")) {
                                device->m_firmware_version = client["client_version"];
                                device->update_config_from_file(device->m_model_id);
                            }
                        }
                    }
                }
            }
            catch (const std::exception&) {
            }
        }

    });
}

}}
