#ifndef slic3r_PhysicalPrinterDialog_hpp_
#define slic3r_PhysicalPrinterDialog_hpp_

#include <vector>

#include <wx/gdicmn.h>

#include "libslic3r/Preset.hpp"
#include "GUI_Utils.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/Button.hpp"

class wxString;
class wxTextCtrl;
class wxStaticText;
class ScalableButton;
class wxBoxSizer;

namespace Slic3r {

namespace GUI {

//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------

class ConfigOptionsGroup;
class PhysicalPrinterDialog : public DPIDialog
{
    // y3
    std::string         old_name;
    std::map<std::string, DynamicPrintConfig> exit_machine;
    PhysicalPrinter     m_printer;
    bool                empty_flag;

    DynamicPrintConfig* m_config            { nullptr };
    ConfigOptionsGroup* m_optgroup          { nullptr };

    wxString            m_default_name;
    DynamicPrintConfig* host_config            { nullptr };

    ScalableButton*     m_printhost_browse_btn              {nullptr};
    ScalableButton*     m_printhost_test_btn                {nullptr};
    ScalableButton*     m_printhost_cafile_browse_btn       {nullptr};
    ScalableButton*     m_printhost_client_cert_browse_btn  {nullptr};
    ScalableButton*     m_printhost_port_browse_btn         {nullptr};

    RoundedRectangle*   m_input_area                        {nullptr};
    wxStaticText*       m_valid_label                       {nullptr};
    wxTextCtrl*         m_input_ctrl                        {nullptr};
    Button*             m_button_ok                         {nullptr};
    Button*             m_button_cancel                     {nullptr};

    // y2 add param
    wxComboBox*         pret_combobox                       {nullptr};
    wxTextCtrl*         printer_host_ctrl                   { nullptr };
    std::set<std::string>        m_exit_host;

    void build_printhost_settings(ConfigOptionsGroup* optgroup);
    void OnOK(wxMouseEvent& event);

public:
    // y3 //y25
    PhysicalPrinterDialog(wxWindow* parent, wxString printer_name, std::map<std::string, DynamicPrintConfig> m_machine, std::set<std::string> exit_host);
    ~PhysicalPrinterDialog();

    enum ValidationType
    {
        Valid,
        NoValid,
        Warning
    };
    PresetCollection* m_presets {nullptr};
    ValidationType  m_valid_type;
    std::string     m_printer_name;
    std::string     m_printer_host;

    void        update(bool printer_change = false);
    void        update_host_type(bool printer_change);
    void        update_preset_input();
    void        update_printhost_buttons();
    void        update_printers();
    
    // y3
    std::string        get_name();
    std::string        get_host();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {};
};


} // namespace GUI
} // namespace Slic3r

#endif
