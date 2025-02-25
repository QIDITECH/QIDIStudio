#ifndef slic3r_OctoPrint_hpp_
#define slic3r_OctoPrint_hpp_

#include <string>
#include <wx/string.h>
#include <boost/optional.hpp>

#include "PrintHost.hpp"
#include "libslic3r/PrintConfig.hpp"
//w42
#include "slic3r/GUI/SelectMachine.hpp"


namespace Slic3r {

class DynamicPrintConfig;
class Http;

class OctoPrint : public PrintHost
{
public:
    OctoPrint(DynamicPrintConfig *config, bool add_port);
    OctoPrint(std::string host,std::string local_ip);
    ~OctoPrint() override = default;

    const char* get_name() const override;

    bool test(wxString &curl_msg) const override;
    wxString get_test_ok_msg () const override;
    wxString get_test_failed_msg (wxString &msg) const override;
    bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const override;
    bool has_auto_discovery() const override { return true; }
    bool can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }
    std::string get_host() const override { return m_show_ip; }
    const std::string& get_apikey() const { return m_apikey; }
    const std::string& get_cafile() const { return m_cafile; }
    //y40
    virtual std::string get_status(wxString& curl_msg) const override;
    virtual float       get_progress(wxString& curl_msg) const override;
    virtual std::pair<std::string, float>       get_status_progress(wxString& curl_msg) const override;

    static void                SetStop(bool isStop) { m_isStop = isStop; };
    static bool                GetStop() { return m_isStop; };
    static bool                m_isStop;
    //y53
    static double              progress_percentage;

    //w42
    bool       get_box_state(wxString& curl_msg);
    bool       get_printer_state(wxString& curl_msg);
    std::pair<std::string, float>       send_command_to_printer(wxString& curl_msg);
    GUI::Box_info       get_box_info(wxString& msg);
    void       get_color_filament_str(wxString& msg, GUI::Box_info& machine_filament_info);

protected:
    std::string m_show_ip;
    std::string m_host;
    std::string m_apikey;
    std::string m_cafile;
    bool        m_ssl_revoke_best_effort;

    virtual void set_auth(Http &http) const;
    std::string make_url(const std::string &path) const;

protected:
    virtual bool validate_version_text(const boost::optional<std::string> &version_text) const;

};

class SL1Host: public OctoPrint
{
public:
    SL1Host(DynamicPrintConfig *config);
    ~SL1Host() override = default;

    const char* get_name() const override;

    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString &msg) const override;
    PrintHostPostUploadActions get_post_upload_actions() const override { return {}; }

protected:
    bool validate_version_text(const boost::optional<std::string> &version_text) const override;

private:
    void set_auth(Http &http) const override;

    // Host authorization type.
    AuthorizationType m_authorization_type;
    // username and password for HTTP Digest Authentization (RFC RFC2617)
    std::string m_username;
    std::string m_password;
};

class PrusaLink : public OctoPrint
{
public:
    PrusaLink(DynamicPrintConfig* config);
    ~PrusaLink() override = default;

    const char* get_name() const override;

    wxString get_test_ok_msg() const override;
    wxString get_test_failed_msg(wxString& msg) const override;
    PrintHostPostUploadActions get_post_upload_actions() const override { return PrintHostPostUploadAction::StartPrint; }

protected:
    bool validate_version_text(const boost::optional<std::string>& version_text) const override;

private:
    void set_auth(Http& http) const override;

    // Host authorization type.
    AuthorizationType m_authorization_type;
    // username and password for HTTP Digest Authentization (RFC RFC2617)
    std::string m_username;
    std::string m_password;
};


class QIDILink : public OctoPrint
{
public:
    bool get_storage(wxArrayString& storage_path, wxArrayString& storage_name) const override;
};

class QIDIConnect : public QIDILink
{
public:
    bool get_storage(wxArrayString& storage_path, wxArrayString& storage_name) const override;
};

}

#endif
