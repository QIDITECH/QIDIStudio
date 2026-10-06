#ifndef slic3r_TunnelManagerDialog_hpp_
#define slic3r_TunnelManagerDialog_hpp_

#include <wx/dialog.h>
#include <wx/string.h>

class wxButton;
class wxStaticText;
class wxTextCtrl;

namespace Slic3r {
namespace GUI {

class TunnelManagerDialog : public wxDialog
{
public:
    explicit TunnelManagerDialog(wxWindow* parent);

private:
    bool run_script(const wxString& script_name, const wxString& arguments,
                    const wxString& secret, wxString& output, wxString& error) const;
    void refresh_status();
    void configure_tunnel();
    void run_action(const wxString& action, bool show_success = false);
    void open_documentation();
    void copy_text(const wxString& value, const wxString& description);

    wxStaticText* m_overall_status { nullptr };
    wxStaticText* m_task_status { nullptr };
    wxStaticText* m_heartbeat_status { nullptr };
    wxStaticText* m_credential_status { nullptr };
    wxTextCtrl*   m_tunnel_id { nullptr };
    wxTextCtrl*   m_api_key { nullptr };
    wxButton*     m_start_button { nullptr };
    wxButton*     m_stop_button { nullptr };
    wxButton*     m_restart_button { nullptr };
    bool          m_tunnel_configured { false };
    bool          m_credentials_configured { false };
    wxString      m_local_mcp_url { "http://127.0.0.1:8765/mcp" };
};

} // namespace GUI
} // namespace Slic3r

#endif
