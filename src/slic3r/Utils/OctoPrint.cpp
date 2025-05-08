#include "OctoPrint.hpp"

#include <algorithm>
#include <sstream>
#include <exception>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <curl/curl.h>

#include <wx/progdlg.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "Http.hpp"
#include "libslic3r/AppConfig.hpp"
#include "slic3r/GUI/GUI_App.hpp"

//B
#include "slic3r/GUI/format.hpp"


namespace fs = boost::filesystem;
namespace pt = boost::property_tree;


namespace Slic3r {

bool OctoPrint::m_isStop = false;
//y53
double OctoPrint::progress_percentage = 0;

#ifdef WIN32
// Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
namespace {
std::string substitute_host(const std::string& orig_addr, std::string sub_addr)
{
    // put ipv6 into [] brackets 
    if (sub_addr.find(':') != std::string::npos && sub_addr.at(0) != '[')
        sub_addr = "[" + sub_addr + "]";

#if 0
    //URI = scheme ":"["//"[userinfo "@"] host [":" port]] path["?" query]["#" fragment]
    std::string final_addr = orig_addr;
    //  http
    size_t double_dash = orig_addr.find("//");
    size_t host_start = (double_dash == std::string::npos ? 0 : double_dash + 2);
    // userinfo
    size_t at = orig_addr.find("@");
    host_start = (at != std::string::npos && at > host_start ? at + 1 : host_start);
    // end of host, could be port(:), subpath(/) (could be query(?) or fragment(#)?)
    // or it will be ']' if address is ipv6 )
    size_t potencial_host_end = orig_addr.find_first_of(":/", host_start); 
    // if there are more ':' it must be ipv6
    if (potencial_host_end != std::string::npos && orig_addr[potencial_host_end] == ':' && orig_addr.rfind(':') != potencial_host_end) {
        size_t ipv6_end = orig_addr.find(']', host_start);
        // DK: Uncomment and replace orig_addr.length() if we want to allow subpath after ipv6 without [] parentheses.
        potencial_host_end = (ipv6_end != std::string::npos ? ipv6_end + 1 : orig_addr.length()); //orig_addr.find('/', host_start));
    }
    size_t host_end = (potencial_host_end != std::string::npos ? potencial_host_end : orig_addr.length());
    // now host_start and host_end should mark where to put resolved addr
    // check host_start. if its nonsense, lets just use original addr (or  resolved addr?)
    if (host_start >= orig_addr.length()) {
        return final_addr;
    }
    final_addr.replace(host_start, host_end - host_start, sub_addr);
    return final_addr;
#else
    // Using the new CURL API for handling URL. https://everything.curl.dev/libcurl/url
    // If anything fails, return the input unchanged.
    std::string out = orig_addr;
    CURLU *hurl = curl_url();
    if (hurl) {
        // Parse the input URL.
        CURLUcode rc = curl_url_set(hurl, CURLUPART_URL, orig_addr.c_str(), 0);
        if (rc == CURLUE_OK) {
            // Replace the address.
            rc = curl_url_set(hurl, CURLUPART_HOST, sub_addr.c_str(), 0);
            if (rc == CURLUE_OK) {
                // Extract a string fromt the CURL URL handle.
                char *url;
                rc = curl_url_get(hurl, CURLUPART_URL, &url, 0);
                if (rc == CURLUE_OK) {
                    out = url;
                    curl_free(url);
                } else
                    BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to extract the URL after substitution";
            } else
                BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to substitute host " << sub_addr << " in URL " << orig_addr;
        } else
            BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to parse URL " << orig_addr;
        curl_url_cleanup(hurl);
    } else
        BOOST_LOG_TRIVIAL(error) << "OctoPrint substitute_host: failed to allocate curl_url";
    return out;
#endif
}
} //namespace
#endif // WIN32

OctoPrint::OctoPrint(DynamicPrintConfig *config, bool add_port)
    : m_host(add_port ? config->opt_string("print_host").find(":") == std::string::npos ? config->opt_string("print_host") + ":10088" :
        config->opt_string("print_host") :
        config->opt_string("print_host"))
    , m_show_ip(config->opt_string("print_host"))
    , m_apikey(config->opt_string("printhost_apikey"))
{
}

OctoPrint::OctoPrint(std::string host, std::string local_ip)
    : m_host(host), m_show_ip(local_ip) {}

const char* OctoPrint::get_name() const { return "OctoPrint"; }

bool OctoPrint::test(wxString &msg) const
{
    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure

    const char *name = get_name();

    bool res = true;
    auto url = make_url("api/version");

    // BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    BOOST_LOG_TRIVIAL(info) << boost::format("Before uploading, %1%:Test at: %2%") % name % url;
    http.on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            res = false;
            msg = format_error(body, error, status);
        })
        .on_complete([&, this](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got version: %2%") % name % body;

            try {
                std::stringstream ss(body);
                pt::ptree ptree;
                pt::read_json(ss, ptree);

                if (! ptree.get_optional<std::string>("api")) {
                    res = false;
                    return;
                }

                const auto text = ptree.get_optional<std::string>("text");
                res = validate_version_text(text);
                if (! res) {
                    msg = GUI::from_u8((boost::format(_utf8(L("Mismatched type of print host: %s"))) % (text ? *text : "OctoPrint")).str());
                }
            }
            catch (const std::exception &) {
                res = false;
                msg = "Could not parse server response";
            }
            BOOST_LOG_TRIVIAL(info) << boost::format("Get version success. The version msg is %1%") % msg;
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
            // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
            // Remember resolved address to be reused at successive REST API call.
            msg = GUI::from_u8(address);
        })
#endif // WIN32
        .perform_sync();

    return res;
}

wxString OctoPrint::get_test_ok_msg () const
{   
    // y3
    return _(L("Connection to Moonraker works correctly."));
}

wxString OctoPrint::get_test_failed_msg (wxString &msg) const
{
    return GUI::from_u8((boost::format("%s: %s\n")
        % _utf8(L("Could not connect to Moonraker"))
        % std::string(msg.ToUTF8())).str());
}

bool OctoPrint::upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const
{
    const char *name = get_name();

    const auto upload_filename = upload_data.upload_path.filename();
    const auto upload_parent_path = upload_data.upload_path.parent_path();

    // If test fails, test_msg_or_host_ip contains the error message.
    // Otherwise on Windows it contains the resolved IP address of the host.
    wxString test_msg_or_host_ip;
    if (! test(test_msg_or_host_ip)) {
        error_fn(std::move(test_msg_or_host_ip));
        return false;
    }

    std::string url;
    bool res = true;

#ifdef WIN32
    // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
    if (m_host.find("https://") == 0 || test_msg_or_host_ip.empty() ||
        m_host.find("aws") != -1 || m_host.find("aliyun") != -1)
#endif // _WIN32
    {
        // If https is entered we assume signed ceritificate is being used
        // IP resolving will not happen - it could resolve into address not being specified in cert
        url = make_url("server/files/upload");
    }
#ifdef WIN32
    else {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Curl uses easy_getinfo to get ip address of last successful transaction.
        // If it got the address use it instead of the stored in "host" variable.
        // This new address returns in "test_msg_or_host_ip" variable.
        // Solves troubles of uploades failing with name address.
        // in original address (m_host) replace host for resolved ip 
        url = substitute_host(make_url("server/files/upload"), GUI::into_u8(test_msg_or_host_ip));
        // BOOST_LOG_TRIVIAL(info) << "Upload address after ip resolve: " << url;
    }
#endif // _WIN32

    // BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Uploading file %2% at %3%, filename: %4%, path: %5%, print: %6%")
    //     % name
    //     % upload_data.source_path
    //     % url
    //     % upload_filename.string()
    //     % upload_parent_path.string()
    //     % (upload_data.post_action == PrintHostPostUploadAction::StartPrint ? "true" : "false");

    auto http = Http::post(std::move(url));
    set_auth(http);

    http.form_add("root", "gcodes");
    if (!upload_parent_path.empty())
        http.form_add("path", upload_parent_path.string());
    if (upload_data.post_action == PrintHostPostUploadAction::StartPrint)
        http.form_add("print", "true");

    //y61
    // http.form_add("plateindex", std::to_string(partplate_list.get_curr_plate_index()));

    //y53
    progress_percentage = 0;
    BOOST_LOG_TRIVIAL(info) << boost::format("Uploading the file to device %1%, url: %2%") % name % url;
    http.form_add_file("file", upload_data.source_path.string(), upload_filename.string())
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: File uploaded: HTTP %2%: %3%") % name % status % body;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            //y40 y53
            if (progress_percentage < 0.99) {
                if (status == 404)
                {
                    body = ("Network connection fails.");
                    if (body.find("AWS") != std::string::npos)
                        body += ("Unable to get required resources from AWS server, please check your network settings.");
                    else
                        body += ("Unable to get required resources from Aliyun server, please check your network settings.");
                }
                BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error uploading file: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
                error_fn(format_error(body, error, status));
                res = false;
            }
            else
                res = true;
        })
        .on_progress([&](Http::Progress progress, bool &cancel) {
            prorgess_fn(std::move(progress), cancel);
            if (cancel) {
                // Upload was canceled
                BOOST_LOG_TRIVIAL(info) << "Octoprint: Upload canceled";
                res = false;
            }
        })
#ifdef WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif
        .perform_sync();

    return res;
}

bool OctoPrint::validate_version_text(const boost::optional<std::string> &version_text) const
{
    return version_text ? boost::starts_with(*version_text, "OctoPrint") : true;
}

void OctoPrint::set_auth(Http &http) const
{
    http.header("X-Api-Key", m_apikey);

    if (!m_cafile.empty()) {
        http.ca_file(m_cafile);
    }
}

std::string OctoPrint::make_url(const std::string &path) const
{
    if (m_host.find("http://") == 0 || m_host.find("https://") == 0) {
        if (m_host.back() == '/') {
            return (boost::format("%1%%2%") % m_host % path).str();
        } else {
            return (boost::format("%1%/%2%") % m_host % path).str();
        }
    } else {
        return (boost::format("http://%1%/%2%") % m_host % path).str();
    }
}

//y40
std::string OctoPrint::get_status(wxString& msg) const
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const std::string name = get_name();
    bool res = true;
    std::string print_state = "standby";
    auto url = make_url("printer/objects/query?print_stats=state");

    // BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get status at: %2%") % name % url;
    //y36
    http.timeout_connect(4)
        .on_error([&](std::string body, std::string error, unsigned status) {
        if (status == 404)
        {
            body = ("Network connection fails.");
            if (body.find("AWS") != std::string::npos)
                body += ("Unable to get required resources from AWS server, please check your network settings.");
            else
                body += ("Unable to get required resources from Aliyun server, please check your network settings.");
        }
        print_state = "offline";
        msg = format_error(body, error, status);
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
            })
        .on_complete([&](std::string body, unsigned) {
                BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got print_stats: %2%") % name % body;

                try {
                    // All successful HTTP requests will return a json encoded object in the form of :
                    // {result: <response data>}
                    std::stringstream ss(body);
                    pt::ptree         ptree;
                    pt::read_json(ss, ptree);
                    if (ptree.front().first != "result") {
                        msg = "Could not parse server response";
                        print_state = "offline";
                        return;
                    }
                    if (!ptree.front().second.get_optional<std::string>("status")) {
                        msg = "Could not parse server response";
                        print_state = "offline";
                        return;
                    }
                    print_state = ptree.get<std::string>("result.status.print_stats.state");
                    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got state: %2%") % name % print_state;
                    ;
                }
                catch (const std::exception&) {
                    print_state = "offline";
                    msg = "Could not parse server response";
                }
            })
#ifdef _WIN32
                .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
                .on_ip_resolve([&](std::string address) {
                // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
                // Remember resolved address to be reused at successive REST API call.
                msg = wxString::FromUTF8(address);
                    })
#endif // _WIN32
                .perform_sync();

    return print_state;
}

//y40
float OctoPrint::get_progress(wxString& msg) const
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const std::string name = get_name();

    bool res = true;
    auto url = make_url("printer/objects/query?display_status=progress");
    float  process = 0;
    // BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get progress at: %2%") % name % url;
    http.timeout_connect(4)
        .on_error([&](std::string body, std::string error, unsigned status) {
        if (status == 404)
        {
            body = ("Network connection fails.");
            if (body.find("AWS") != std::string::npos)
                body += ("Unable to get required resources from AWS server, please check your network settings.");
            else
                body += ("Unable to get required resources from Aliyun server, please check your network settings.");
        }
        res = false;
        msg = format_error(body, error, status);
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status %
            body;
        })
        .on_complete([&](std::string body, unsigned) {
                try {
                    // All successful HTTP requests will return a json encoded object in the form of :
                    // {result: <response data>}
                    std::stringstream ss(body);
                    pt::ptree         ptree;
                    pt::read_json(ss, ptree);
                    if (ptree.front().first != "result") {
                        msg = "Could not parse server response";
                        res = false;
                        return;
                    }
                    if (!ptree.front().second.get_optional<std::string>("status")) {
                        msg = "Could not parse server response";
                        res = false;
                        return;
                    }
                    process = std::stof(ptree.get<std::string>("result.status.display_status.progress"));
                    BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got state: %2%") % name % process;
                }
                catch (const std::exception&) {
                    res = false;
                    msg = "Could not parse server response";
                }
                BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got display_status: %2%, msg is %3%") % name % body %msg;
            })
#ifdef _WIN32
                .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
                .on_ip_resolve([&](std::string address) {
                // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
                // Remember resolved address to be reused at successive REST API call.
                msg = wxString::FromUTF8(address);
                    })
#endif // _WIN32
                .perform_sync();

                    return process;
}


//w42
bool OctoPrint::send_command_to_printer(wxString& msg, wxString commond) const
{
   // printer_state: http://192.168.20.66/printer/objects/query?print_stats
   const char* name = get_name();
   std::string gcode = "G28";
   std::string commond_str = commond.ToStdString();
   std::string json_body = "{\"script\": \"" + commond_str + "\"}";

   auto url = make_url("printer/gcode/script");
    bool successful = false;
   Http http = Http::post(std::move(url));
   http.header("Content-Type", "application/json")
       .set_post_body(json_body)
       .timeout_connect(4)
       .on_error([&](std::string body, std::string error, unsigned status) {
       BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error sending G-code: %2%, HTTP %3%, body: %4%")
           % name % error % status % body;
           })
       .on_complete([&](std::string body, unsigned status) {
               BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: G-code sent successfully: %2%") % name % gcode;
               successful = true;
           })
#ifdef _WIN32
               .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif // _WIN32
               .perform_sync();
   
   return successful;
}

//y60
bool OctoPrint::send_timelapse_status(wxString& msg, std::string ip, bool status) const
{
   const char* name = get_name();
   std::string status_str = status ? "true" : "false";
   std::string url = (boost::format("http://%1%:7125/machine/timelapse/settings?enabled=%2%") % ip % status_str).str();
   bool successful = false;
   std::string json_body = "{}";
   Http http = Http::post(std::move(url));
   http.header("Content-Type", "application/json")
       .set_post_body(json_body)
       .timeout_connect(4)
       .on_error([&](std::string body, std::string error, unsigned status) {
       BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error sending G-code: %2%, HTTP %3%, body: %4%")
           % name % error % status % body;
           })
       .on_complete([&](std::string body, unsigned status) {
               BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: G-code sent successfully: %2%") % name % status_str;
               successful = true;
           })
#ifdef _WIN32
               .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
#endif // _WIN32
               .perform_sync();
   
   return successful;
}

std::pair<std::string, float> OctoPrint::get_status_progress(wxString& msg) const
{
    // GET /server/info

    // Since the request is performed synchronously here,
    // it is ok to refer to `msg` from within the closure
    const char* name = get_name();

    bool        res = true;
    std::string print_state = "standby";
    float       process = 0;
    auto        url = make_url("printer/objects/query?print_stats=state&display_status=progress");

    //BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get version at: %2%") % name % url;

    auto http = Http::get(std::move(url));
    set_auth(http);
    BOOST_LOG_TRIVIAL(info) << boost::format("Get status progress, the url is %1%") % url;
    // B64 //y6
    http.timeout_connect(4)
        .on_error([&](std::string body, std::string error, unsigned status) {
        // y1
        if (status == 404) {
            body = ("Network connection fails.");
            if (body.find("AWS") != std::string::npos)
                body += ("Unable to get required resources from AWS server, please check your network settings.");
            else
                body += ("Unable to get required resources from Aliyun server, please check your network settings.");
        }
        print_state = "offline";
        msg = format_error(body, error, status);
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting version: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
        })
        .on_complete([&](std::string body, unsigned) {
            BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got print_stats and process: %2%") % name % body;
            try {
                // All successful HTTP requests will return a json encoded object in the form of :
                // {result: <response data>}
                std::stringstream ss(body);
                pt::ptree         ptree;
                pt::read_json(ss, ptree);
                if (ptree.front().first != "result") {
                    msg = "Could not parse server response";
                    print_state = "offline";
                    process = 0;
                    return;
                }
                if (!ptree.front().second.get_optional<std::string>("status")) {
                    msg = "Could not parse server response";
                    print_state = "offline";
                    process = 0;
                    return;
                }
                print_state = ptree.get<std::string>("result.status.print_stats.state");
                process = std::stof(ptree.get<std::string>("result.status.display_status.progress"));
                BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Got state: %2%") % name % print_state;
                ;
            }
            catch (const std::exception&) {
                print_state = "offline";
                process = 0;
                msg = "Could not parse server response";
            }
        })
#ifdef _WIN32
        .ssl_revoke_best_effort(m_ssl_revoke_best_effort)
        .on_ip_resolve([&](std::string address) {
        // Workaround for Windows 10/11 mDNS resolve issue, where two mDNS resolves in succession fail.
        // Remember resolved address to be reused at successive REST API call.
        msg = GUI::from_u8(address);
            })
#endif // _WIN32
                .perform_sync();

    return std::make_pair(print_state, process);
}

SL1Host::SL1Host(DynamicPrintConfig *config) : 
    OctoPrint(config, true),
    m_authorization_type(dynamic_cast<const ConfigOptionEnum<AuthorizationType>*>(config->option("printhost_authorization_type"))->value),
    m_username(config->opt_string("printhost_user")),
    m_password(config->opt_string("printhost_password"))
{
}

// SL1Host
const char* SL1Host::get_name() const { return "SL1Host"; }

wxString SL1Host::get_test_ok_msg () const
{
    return _(L("Connection to Prusa SL1 / SL1S works correctly."));
}

wxString SL1Host::get_test_failed_msg (wxString &msg) const
{
    return GUI::from_u8((boost::format("%s: %s")
                    % _utf8(L("Could not connect to Prusa SLA"))
                    % std::string(msg.ToUTF8())).str());
}

bool SL1Host::validate_version_text(const boost::optional<std::string> &version_text) const
{
    return version_text ? boost::starts_with(*version_text, "Prusa SLA") : false;
}

void SL1Host::set_auth(Http &http) const
{
    switch (m_authorization_type) {
    case atKeyPassword:
        http.header("X-Api-Key", get_apikey());
        break;
    case atUserPassword:
        http.auth_digest(m_username, m_password);
        break;
    }

    if (! get_cafile().empty()) {
        http.ca_file(get_cafile());
    }
}

// PrusaLink
PrusaLink::PrusaLink(DynamicPrintConfig* config) :
    OctoPrint(config, true),
    m_authorization_type(dynamic_cast<const ConfigOptionEnum<AuthorizationType>*>(config->option("printhost_authorization_type"))->value),
    m_username(config->opt_string("printhost_user")),
    m_password(config->opt_string("printhost_password"))
{
}

const char* PrusaLink::get_name() const { return "PrusaLink"; }

wxString PrusaLink::get_test_ok_msg() const
{
    return _(L("Connection to PrusaLink works correctly."));
}

wxString PrusaLink::get_test_failed_msg(wxString& msg) const
{
    return GUI::from_u8((boost::format("%s: %s")
        % _utf8(L("Could not connect to PrusaLink"))
        % std::string(msg.ToUTF8())).str());
}

bool PrusaLink::validate_version_text(const boost::optional<std::string>& version_text) const
{
    return version_text ? (boost::starts_with(*version_text, "PrusaLink") || boost::starts_with(*version_text, "OctoPrint")) : false;
}

void PrusaLink::set_auth(Http& http) const
{
    switch (m_authorization_type) {
    case atKeyPassword:
        http.header("X-Api-Key", get_apikey());
        break;
    case atUserPassword:
        http.auth_digest(m_username, m_password);
        break;
    }

    if (!get_cafile().empty()) {
        http.ca_file(get_cafile());
    }
}


bool QIDILink::get_storage(wxArrayString& storage_path, wxArrayString& storage_name) const
{
    const char* name = get_name();

    bool res = true;
    auto url = make_url("api/v1/storage");
    wxString error_msg;

    struct StorageInfo {
        wxString path;
        wxString name;
        bool read_only = false;
        long long free_space = -1;
    };
    std::vector<StorageInfo> storage;

    // BOOST_LOG_TRIVIAL(info) << boost::format("%1%: Get storage at: %2%") % name % url;

    wxString wlang = GUI::wxGetApp().current_language_code();
    std::string lang = GUI::format(wlang.SubString(0, 1));

    auto http = Http::get(std::move(url));
    set_auth(http);
    http.header("Accept-Language", lang);
    http.on_error([&](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%: Error getting storage: %2%, HTTP %3%, body: `%4%`") % name % error % status % body;
        error_msg = L"\n\n" + boost::nowide::widen(error);
        res = false;
        // If status is 0, the communication with the printer has failed completely (most likely a timeout), if the status is <= 400, it is an error returned by the pritner.
        // If 0, we can show error to the user now, as we know the communication has failed. (res = true will do the trick.)
        // if not 0, we must not show error, as not all printers support api/v1/storage endpoint.
        // So we must be extra careful here, or we might be showing errors on perfectly fine communication.
        if (status == 0)
            res = true;
       
    })
    .on_complete([&](std::string body, unsigned) {
        BOOST_LOG_TRIVIAL(debug) << boost::format("%1%: Got storage: %2%") % name % body;
        try
        {
            std::stringstream ss(body);
            pt::ptree ptree;
            pt::read_json(ss, ptree);
            
            // what if there is more structure added in the future? Enumerate all elements? 
            if (ptree.front().first != "storage_list") {
                res = false;
                return;
            }
            // each storage has own subtree of storage_list
            for (const auto& section : ptree.front().second) {
                const auto name = section.second.get_optional<std::string>("name");
                const auto path = section.second.get_optional<std::string>("path");
                const auto space = section.second.get_optional<std::string>("free_space");
                const auto read_only = section.second.get_optional<bool>("read_only");
                const auto ro = section.second.get_optional<bool>("ro"); // In QIDILink 0.7.0RC2 "read_only" value is stored under "ro".
                const auto available = section.second.get_optional<bool>("available");
                if (path && (!available || *available)) {
                    StorageInfo si;
                    si.path = boost::nowide::widen(*path);
                    si.name = name ? boost::nowide::widen(*name) : wxString();
                    // If read_only is missing, assume it is NOT read only.
                    // si.read_only = read_only ? *read_only : false; // version without "ro"
                    si.read_only = (read_only ? *read_only : (ro ? *ro : false));
                    si.free_space = space ? std::stoll(*space) : 1;  // If free_space is missing, assume there is free space.
                    storage.emplace_back(std::move(si));
                }
            }
        }
        catch (const std::exception&)
        {
            res = false;
        }
    })
#ifdef WIN32
    .ssl_revoke_best_effort(m_ssl_revoke_best_effort)

#endif // WIN32
    .perform_sync();

    for (const auto& si : storage) {
        if (!si.read_only && si.free_space > 0) {
            storage_path.push_back(si.path);
            storage_name.push_back(si.name);
        }
    }

    if (res && storage_path.empty()) {
        if (!storage.empty()) { // otherwise error_msg is already filled 
            error_msg = L"\n\n" + _L("Storages found") + L": \n";
            for (const auto& si : storage) {
                error_msg += GUI::format_wxstr(si.read_only ?
                                                                // TRN %1% = storage path
                                                                _L("%1% : read only") : 
                                                                // TRN %1% = storage path
                                                                _L("%1% : no free space"), si.path) + L"\n";
            }
        }
        // TRN %1% = host
        std::string message = GUI::format(_L("Upload has failed. There is no suitable storage found at %1%."), m_host) + GUI::into_u8(error_msg);
        BOOST_LOG_TRIVIAL(error) << message;
        throw Slic3r::IOError(message);
    }

    return res;
}


}
