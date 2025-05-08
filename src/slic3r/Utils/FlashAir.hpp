#ifndef slic3r_FlashAir_hpp_
#define slic3r_FlashAir_hpp_

#include <string>
#include <wx/string.h>

#include "PrintHost.hpp"


namespace Slic3r {

class DynamicPrintConfig;
class Http;

class FlashAir : public PrintHost
{
public:
	FlashAir(DynamicPrintConfig *config);
	~FlashAir() override = default;

	const char* get_name() const override;

	bool test(wxString &curl_msg) const override;
	virtual std::string get_status(wxString& curl_msg) const override { return "1"; };
	virtual float       get_progress(wxString& curl_msg) const override { return 1; };
	virtual std::pair<std::string, float>       get_status_progress(wxString& curl_msg) const override { return std::make_pair("1", 1); };
	virtual bool send_command_to_printer(wxString& curl_msg, wxString commond) const override {return false;};
	virtual bool send_timelapse_status(wxString& msg, std::string ip, bool status) const {return false;};
	wxString get_test_ok_msg() const override;
	wxString get_test_failed_msg(wxString &msg) const override;
	bool upload(PrintHostUpload upload_data, ProgressFn prorgess_fn, ErrorFn error_fn) const override;
	bool has_auto_discovery() const override { return false; }
	bool can_test() const override { return true; }
    PrintHostPostUploadActions get_post_upload_actions() const override { return {}; }
	std::string get_host() const override { return host; }
    
private:
	std::string host;

	std::string timestamp_str() const;
	std::string make_url(const std::string &path) const;
	std::string make_url(const std::string &path, const std::string &arg, const std::string &val) const;
};

}

#endif
