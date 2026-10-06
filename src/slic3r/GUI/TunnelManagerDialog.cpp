#include "TunnelManagerDialog.hpp"

#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <wx/button.h>
#include <wx/busyinfo.h>
#include <wx/arrstr.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace Slic3r {
namespace GUI {

namespace {

std::string into_u8(const wxString& value)
{
    const auto buffer = value.ToUTF8();
    const char* data = buffer.data();
    return data != nullptr ? std::string(data, buffer.length()) : std::string();
}

wxString from_u8(const std::string& value)
{
    return wxString::FromUTF8(value.data(), value.size());
}

wxString join_lines(const wxArrayString& lines)
{
    wxString result;
    for (const wxString& line : lines) {
        if (!result.empty())
            result += '\n';
        result += line;
    }
    return result;
}

bool is_valid_tunnel_id(const wxString& value)
{
    const std::string id = into_u8(value);
    const std::string prefix = "tunnel_";
    if (id.size() <= prefix.size() || id.compare(0, prefix.size(), prefix) != 0)
        return false;
    return std::all_of(id.begin() + prefix.size(), id.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-';
    });
}

wxButton* add_button(wxWindow* parent, wxSizer* sizer, const wxString& label)
{
    wxButton* button = new wxButton(parent, wxID_ANY, label);
    sizer->Add(button, 0, wxRIGHT | wxBOTTOM, 6);
    return button;
}

} // namespace

TunnelManagerDialog::TunnelManagerDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("MCP Tunnel Manager"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    wxStaticText* intro = new wxStaticText(this, wxID_ANY,
        _L("Connect this QIDI Studio MCP server to ChatGPT. The Runtime API key is encrypted with Windows DPAPI and is never stored in QIDI Studio."));
    intro->Wrap(650);
    root->Add(intro, 0, wxEXPAND | wxALL, 12);

    wxStaticBoxSizer* status_box = new wxStaticBoxSizer(wxVERTICAL, this, _L("Status"));
    wxFlexGridSizer* status_grid = new wxFlexGridSizer(2, 8, 12);
    status_grid->Add(new wxStaticText(this, wxID_ANY, _L("Overall:")), 0, wxALIGN_CENTER_VERTICAL);
    m_overall_status = new wxStaticText(this, wxID_ANY, _L("Checking..."));
    status_grid->Add(m_overall_status, 1, wxEXPAND);
    status_grid->Add(new wxStaticText(this, wxID_ANY, _L("Scheduled task:")), 0, wxALIGN_CENTER_VERTICAL);
    m_task_status = new wxStaticText(this, wxID_ANY, "-");
    status_grid->Add(m_task_status, 1, wxEXPAND);
    status_grid->Add(new wxStaticText(this, wxID_ANY, _L("Heartbeat:")), 0, wxALIGN_CENTER_VERTICAL);
    m_heartbeat_status = new wxStaticText(this, wxID_ANY, "-");
    status_grid->Add(m_heartbeat_status, 1, wxEXPAND);
    status_grid->Add(new wxStaticText(this, wxID_ANY, _L("Credentials:")), 0, wxALIGN_CENTER_VERTICAL);
    m_credential_status = new wxStaticText(this, wxID_ANY, "-");
    status_grid->Add(m_credential_status, 1, wxEXPAND);
    status_grid->AddGrowableCol(1, 1);
    status_box->Add(status_grid, 1, wxEXPAND | wxALL, 8);
    root->Add(status_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    wxStaticBoxSizer* setup_box = new wxStaticBoxSizer(wxVERTICAL, this, _L("Configuration"));
    wxFlexGridSizer* setup_grid = new wxFlexGridSizer(2, 8, 12);
    setup_grid->Add(new wxStaticText(this, wxID_ANY, _L("Tunnel ID:")), 0, wxALIGN_CENTER_VERTICAL);
    m_tunnel_id = new wxTextCtrl(this, wxID_ANY);
    m_tunnel_id->SetHint("tunnel_...");
    setup_grid->Add(m_tunnel_id, 1, wxEXPAND);
    setup_grid->Add(new wxStaticText(this, wxID_ANY, _L("Runtime API key:")), 0, wxALIGN_CENTER_VERTICAL);
    m_api_key = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_PASSWORD);
    m_api_key->SetHint(_L("Enter only when configuring or replacing the key"));
    setup_grid->Add(m_api_key, 1, wxEXPAND);
    setup_grid->AddGrowableCol(1, 1);
    setup_box->Add(setup_grid, 1, wxEXPAND | wxALL, 8);

    wxBoxSizer* setup_buttons = new wxBoxSizer(wxHORIZONTAL);
    wxButton* save = add_button(this, setup_buttons, _L("Save / Repair and Connect"));
    wxButton* tunnels_page = add_button(this, setup_buttons, _L("Open Tunnels Page"));
    wxButton* keys_page = add_button(this, setup_buttons, _L("Open Runtime Keys Page"));
    setup_box->Add(setup_buttons, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    root->Add(setup_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    wxBoxSizer* lifecycle = new wxBoxSizer(wxHORIZONTAL);
    m_start_button = add_button(this, lifecycle, _L("Start"));
    m_stop_button = add_button(this, lifecycle, _L("Stop"));
    m_restart_button = add_button(this, lifecycle, _L("Restart"));
    wxButton* refresh = add_button(this, lifecycle, _L("Refresh"));
    wxButton* doctor = add_button(this, lifecycle, _L("Run Diagnostics"));
    root->Add(lifecycle, 0, wxLEFT | wxRIGHT, 12);

    wxBoxSizer* utilities = new wxBoxSizer(wxHORIZONTAL);
    wxButton* dashboard = add_button(this, utilities, _L("Open Dashboard"));
    wxButton* logs = add_button(this, utilities, _L("Open Logs"));
    wxButton* copy_id = add_button(this, utilities, _L("Copy Tunnel ID"));
    wxButton* copy_local = add_button(this, utilities, _L("Copy Local MCP URL"));
    wxButton* docs = add_button(this, utilities, _L("Setup Help"));
    root->Add(utilities, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

    wxStdDialogButtonSizer* close_sizer = new wxStdDialogButtonSizer();
    close_sizer->AddButton(new wxButton(this, wxID_CLOSE, _L("Close")));
    close_sizer->Realize();
    root->Add(close_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    SetSizerAndFit(root);
    SetMinSize(wxSize(720, GetSize().GetHeight()));
    CentreOnParent();

    save->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { configure_tunnel(); });
    tunnels_page->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        wxLaunchDefaultBrowser("https://platform.openai.com/settings/organization/tunnels");
    });
    keys_page->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        wxLaunchDefaultBrowser("https://platform.openai.com/settings/organization/api-keys");
    });
    m_start_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_action("start"); });
    m_stop_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_action("stop"); });
    m_restart_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_action("restart"); });
    refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_status(); });
    doctor->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_action("doctor", true); });
    dashboard->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_action("open-ui"); });
    logs->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { run_action("logs"); });
    copy_id->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        copy_text(m_tunnel_id->GetValue(), _L("Tunnel ID"));
    });
    copy_local->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        copy_text(m_local_mcp_url, _L("Local MCP URL"));
    });
    docs->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open_documentation(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);

    refresh_status();
}

bool TunnelManagerDialog::run_script(const wxString& script_name, const wxString& arguments,
                                     const wxString& secret, wxString& output, wxString& error) const
{
#ifndef _WIN32
    error = _L("The integrated tunnel manager currently supports Windows only.");
    return false;
#else
    const wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
    const wxFileName script(executable.GetPath() + wxFILE_SEP_PATH + "mcp-tunnel", script_name);
    if (!script.FileExists()) {
        error = wxString::Format(_L("Tunnel component was not found: %s"), script.GetFullPath());
        return false;
    }

    const wxString command = wxString::Format(
        "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" %s",
        script.GetFullPath(), arguments);
    wxExecuteEnv environment;
    wxGetEnvMap(&environment.env);
    const wxString secret_name = "QIDI_MCP_RUNTIME_API_KEY";
    if (!secret.empty())
        environment.env[secret_name] = secret;

    wxArrayString stdout_lines;
    wxArrayString stderr_lines;
    const long exit_code = wxExecute(command, stdout_lines, stderr_lines,
                                     wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE, &environment);
    environment.env.erase(secret_name);
    output = join_lines(stdout_lines);
    error = join_lines(stderr_lines);
    if (exit_code == -1 && error.empty())
        error = _L("PowerShell could not be started.");
    return exit_code == 0;
#endif
}

void TunnelManagerDialog::refresh_status()
{
    wxString output;
    wxString error;
    if (!run_script("manage-qidi-mcp-tunnel.ps1", "-Action status", wxEmptyString, output, error)) {
        m_tunnel_configured = false;
        m_credentials_configured = false;
        m_overall_status->SetLabel(_L("Unavailable"));
        m_task_status->SetLabel(error.empty() ? _L("Not installed") : error);
        m_heartbeat_status->SetLabel("-");
        m_credential_status->SetLabel("-");
        m_start_button->Enable(false);
        m_stop_button->Enable(false);
        m_restart_button->Enable(false);
        Layout();
        return;
    }

    try {
        const size_t json_start = output.find('{');
        const size_t json_end = output.rfind('}');
        if (json_start == wxString::npos || json_end == wxString::npos || json_end < json_start)
            throw std::runtime_error("status response did not contain JSON");
        std::stringstream stream(into_u8(output.substr(json_start, json_end - json_start + 1)));
        boost::property_tree::ptree status;
        boost::property_tree::read_json(stream, status);

        const bool configured = status.get<bool>("configured", false);
        const bool healthy = status.get<bool>("healthy", false);
        const bool heartbeat_fresh = status.get<bool>("heartbeat_fresh", false);
        const bool credentials = status.get<bool>("credentials_configured", false);
        const std::string task_state = status.get<std::string>("task_state", "Not installed");
        const std::string tunnel_id = status.get<std::string>("tunnel_id", "");
        std::string heartbeat_age = status.get<std::string>("heartbeat_age_seconds", "");
        if (heartbeat_age == "null")
            heartbeat_age.clear();
        m_local_mcp_url = from_u8(status.get<std::string>("local_mcp_url", "http://127.0.0.1:8765/mcp"));
        m_tunnel_configured = configured;
        m_credentials_configured = credentials;

        m_overall_status->SetLabel(healthy ? _L("Connected") : (configured ? _L("Configured, not connected") : _L("Not configured")));
        m_task_status->SetLabel(from_u8(task_state));
        if (!configured)
            m_heartbeat_status->SetLabel("-");
        else if (heartbeat_fresh)
            m_heartbeat_status->SetLabel(heartbeat_age.empty() ? _L("Fresh") : from_u8("Fresh (" + heartbeat_age + " seconds ago)"));
        else
            m_heartbeat_status->SetLabel(heartbeat_age.empty() ? _L("Missing or stale") : from_u8("Stale (" + heartbeat_age + " seconds ago)"));
        m_credential_status->SetLabel(credentials ? _L("DPAPI key configured") : _L("Not configured"));
        if (!tunnel_id.empty() && !m_tunnel_id->HasFocus())
            m_tunnel_id->ChangeValue(from_u8(tunnel_id));
        m_start_button->Enable(configured && task_state != "Running");
        m_stop_button->Enable(configured && task_state == "Running");
        m_restart_button->Enable(configured);
    }
    catch (const std::exception& exception) {
        m_tunnel_configured = false;
        m_credentials_configured = false;
        m_overall_status->SetLabel(_L("Status error"));
        m_task_status->SetLabel(from_u8(exception.what()));
    }
    Layout();
}

void TunnelManagerDialog::configure_tunnel()
{
    wxString tunnel_id = m_tunnel_id->GetValue();
    tunnel_id.Trim(true).Trim(false);
    if (!is_valid_tunnel_id(tunnel_id)) {
        wxMessageBox(_L("Tunnel ID must begin with tunnel_ and contain only letters, numbers, underscores, or hyphens."),
                     _L("Invalid Tunnel ID"), wxOK | wxICON_WARNING, this);
        return;
    }
    wxString api_key = m_api_key->GetValue();
    if (!api_key.empty() && api_key.length() < 8) {
        wxMessageBox(_L("Enter the Runtime API key to configure or replace the saved key."),
                     _L("Runtime API Key Required"), wxOK | wxICON_WARNING, this);
        return;
    }
    const bool replace_api_key = !api_key.empty();
    if (!replace_api_key && (!m_tunnel_configured || !m_credentials_configured)) {
        wxMessageBox(_L("Enter the Runtime API key because no reusable DPAPI credential is configured."),
                     _L("Runtime API Key Required"), wxOK | wxICON_WARNING, this);
        return;
    }

    wxString output;
    wxString error;
    const wxString credential_argument = replace_api_key ? "-FromEnvironment" : "-ReuseExistingCredential";
    const wxString arguments = wxString::Format("-TunnelId \"%s\" %s -SkipBrowser", tunnel_id, credential_argument);
    bool success = false;
    {
        wxBusyInfo busy(replace_api_key ? _L("Configuring the MCP tunnel...") : _L("Repairing the MCP tunnel..."), this);
        success = run_script("setup-qidi-mcp-tunnel.ps1", arguments,
                             replace_api_key ? api_key : wxEmptyString, output, error);
    }
    api_key.clear();
    m_api_key->Clear();
    if (!success) {
        wxMessageBox(error.empty() ? output : error, _L("Tunnel Setup Failed"), wxOK | wxICON_ERROR, this);
    } else {
        wxMessageBox(replace_api_key
                         ? _L("The tunnel was configured and started. The Runtime API key is now protected with Windows DPAPI.")
                         : _L("The tunnel was repaired and started using the existing DPAPI-protected Runtime API key."),
                     _L("MCP Tunnel Connected"), wxOK | wxICON_INFORMATION, this);
    }
    refresh_status();
}

void TunnelManagerDialog::run_action(const wxString& action, bool show_success)
{
    wxString output;
    wxString error;
    bool success = false;
    {
        wxBusyInfo busy(wxString::Format(_L("Running tunnel action: %s"), action), this);
        success = run_script("manage-qidi-mcp-tunnel.ps1", "-Action " + action, wxEmptyString, output, error);
    }

    wxString message = error.empty() ? output : error;
    try {
        const size_t json_start = output.find('{');
        const size_t json_end = output.rfind('}');
        if (json_start != wxString::npos && json_end != wxString::npos && json_end >= json_start) {
            std::stringstream stream(into_u8(output.substr(json_start, json_end - json_start + 1)));
            boost::property_tree::ptree result;
            boost::property_tree::read_json(stream, result);
            message = from_u8(result.get<std::string>("message", into_u8(message)));
            const std::string doctor_output = result.get<std::string>("doctor_output", "");
            if (!doctor_output.empty())
                message += "\n\n" + from_u8(doctor_output);
        }
    }
    catch (...) {
        // Preserve the original process output if a future script changes schema.
    }

    if (!success) {
        wxMessageBox(message, _L("MCP Tunnel"), wxOK | wxICON_ERROR, this);
    } else if (show_success) {
        wxMessageBox(message, _L("MCP Tunnel Diagnostics"), wxOK | wxICON_INFORMATION, this);
    }
    refresh_status();
}

void TunnelManagerDialog::open_documentation()
{
    const wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
    const wxFileName readme(executable.GetPath() + wxFILE_SEP_PATH + "mcp-tunnel", "README.md");
    if (!readme.FileExists() || !wxLaunchDefaultApplication(readme.GetFullPath()))
        wxMessageBox(_L("The tunnel README could not be opened."), _L("Setup Help"), wxOK | wxICON_WARNING, this);
}

void TunnelManagerDialog::copy_text(const wxString& value, const wxString& description)
{
    if (value.empty()) {
        wxMessageBox(description + _L(" is not available."), _L("MCP Tunnel"), wxOK | wxICON_WARNING, this);
        return;
    }
    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(value));
        wxTheClipboard->Close();
    } else {
        wxMessageBox(_L("The clipboard could not be opened."), _L("MCP Tunnel"), wxOK | wxICON_WARNING, this);
    }
}

} // namespace GUI
} // namespace Slic3r
