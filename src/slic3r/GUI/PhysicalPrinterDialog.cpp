#include "PhysicalPrinterDialog.hpp"
#include "PresetComboBoxes.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/wupdlock.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
//#include "PrintHostDialogs.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "RemovableDriveManager.hpp"
#include "BitmapCache.hpp"
#include "BonjourDialog.hpp"
#include "MsgDialog.hpp"
#include "PrinterWebView.hpp"

namespace fs_path = std::filesystem;
namespace Slic3r {
namespace GUI {

#define BORDER_W FromDIP(10)

//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------

// y3 //y25
PhysicalPrinterDialog::PhysicalPrinterDialog(wxWindow* parent, wxString printer_name, std::map<std::string, DynamicPrintConfig> m_machine, std::set<std::string> exit_host) :
    DPIDialog(parent, wxID_ANY, _L("Physical Printer"), wxDefaultPosition, wxSize(60 * wxGetApp().em_unit(), -1), wxDEFAULT_DIALOG_STYLE),
    m_printer("", wxGetApp().preset_bundle->physical_printers.default_config())
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);
    // y3 modify Dialog
    empty_flag = printer_name.empty();

    Tab *tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
    m_presets = tab->get_presets();
    const Preset &sel_preset  = m_presets->get_edited_preset();
    std::string suffix = _CTX_utf8(L_CONTEXT("Copy", "PresetName"), "PresetName");
    PresetBundle &printer_bundle = *wxGetApp().preset_bundle;
    std::string printer_name_u8 = into_u8(printer_name);
    PhysicalPrinter* printer = printer_bundle.physical_printers.find_printer(printer_name_u8);
    wxString   input_name = "";
    std::string   inherits = "";
    std::string printer_host = "";
    if(!printer_name.empty())
    {
        inherits = printer->config.opt_string("preset_name");
        input_name = from_u8(printer_name_u8);
        old_name = printer_name_u8;
    }
    if(inherits.empty())
    {
        inherits = sel_preset.config.opt_string("printer_model");
    }
    exit_machine = m_machine;
    m_exit_host = exit_host;

    wxStaticText *label_top = new wxStaticText(this, wxID_ANY, _L("Machine Name") + ":");
    label_top->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3C")));
    wxBoxSizer *input_sizer_name = new wxBoxSizer(wxVERTICAL);

    m_input_ctrl = new wxTextCtrl(this, wxID_ANY, input_name);
    m_input_ctrl->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3C")));
    m_valid_label = new wxStaticText(this, wxID_ANY, "");
    m_valid_label->SetForegroundColour(wxColor(255, 111, 0));

    input_sizer_name->Add(label_top, 0, wxEXPAND | wxTOP, BORDER_W);
    input_sizer_name->Add(m_input_ctrl, 0, wxEXPAND , BORDER_W);
    input_sizer_name->Add(m_valid_label, 0, wxEXPAND | wxBOTTOM, BORDER_W);

    wxStaticText* pret_combo_text = new wxStaticText(this, wxID_ANY, _L("Choice your physical printer preset") + ":");
    pret_combo_text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3C")));
    pret_combobox = new wxComboBox(this, wxID_ANY, inherits, wxDefaultPosition, wxDefaultSize);
    pret_combobox->SetMinSize(wxSize(FromDIP(360), FromDIP(32)));
    PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
    const std::deque<Preset>& presets = preset_bundle.printers.get_presets();
    std::string printer_model_t = "";
    //y50
    std::set<std::string> qidi_printers;
    const auto enabled_vendors = wxGetApp().app_config->vendors();
    for (const auto vendor : enabled_vendors) {
    std::map<std::string, std::set<std::string>> model_map = vendor.second;
        for (auto model_name : model_map) {
            qidi_printers.emplace(model_name.first);
        }
    }

    for (auto preset_name : qidi_printers)
    {
        if(printer_model_t == preset_name)
            continue;
        else
        {
            pret_combobox->Append(preset_name);
            printer_model_t = preset_name;
        }
    }
    wxBoxSizer* pret_sizer = new wxBoxSizer(wxVERTICAL);
    pret_sizer->Add(pret_combo_text, 0, wxEXPAND | wxTOP, BORDER_W);
    pret_sizer->Add(pret_combobox, 0, wxEXPAND | wxBOTTOM, BORDER_W);

    auto input_sizer = new wxBoxSizer(wxVERTICAL);
    input_sizer->Add(input_sizer_name, 0, wxEXPAND, BORDER_W);
    input_sizer->Add(pret_sizer, 0, wxEXPAND, BORDER_W);
    
    m_printer = PhysicalPrinter("Default Name", m_printer.config, sel_preset);
    m_config = &m_printer.config;
    //y40
    if (printer)
    {
        m_config->set_key_value("print_host", new ConfigOptionString(printer->config.opt_string("print_host")));
        m_config->set_key_value("printhost_apikey", new ConfigOptionString(printer->config.opt_string("printhost_apikey")));
    }
    else
    {
        m_config->set_key_value("print_host", new ConfigOptionString(""));
        m_config->set_key_value("printhost_apikey", new ConfigOptionString(""));
    }
    m_optgroup = new ConfigOptionsGroup(this, _L("Print Host upload"), m_config);
    //y47
    m_optgroup->set_label_width(15);

    auto button_sizer = new wxBoxSizer(wxHORIZONTAL);

    // y96
    StateColor btn_bg_blue(std::pair<wxColour, int>(wxColour(95, 82, 253), StateColor::Pressed), std::pair<wxColour, int>(wxColour(129, 150, 255), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_blue);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(*wxWHITE);
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, &PhysicalPrinterDialog::OnOK, this);

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](auto &e) { this->EndModal(wxID_NO);});

    button_sizer->AddStretchSpacer();
    button_sizer->Add(m_button_ok, 0, wxALL, FromDIP(5));
    button_sizer->Add(m_button_cancel, 0, wxALL, FromDIP(5));

    wxBoxSizer *topSizer = new wxBoxSizer(wxVERTICAL);


    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->EndModal(wxID_NO);});
    m_input_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { update(); });
    build_printhost_settings(m_optgroup);

    topSizer->Add(input_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(m_optgroup->sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(button_sizer, 0, wxEXPAND | wxALL, BORDER_W);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
    this->CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

PhysicalPrinterDialog::~PhysicalPrinterDialog()
{
}

void PhysicalPrinterDialog::build_printhost_settings(ConfigOptionsGroup* m_optgroup)
{
    m_optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
        if (opt_key == "print_host")
            this->update_printhost_buttons();
    };

    auto create_sizer_with_btn = [](wxWindow* parent, ScalableButton** btn, const std::string& icon_name, const wxString& label) {
        *btn = new ScalableButton(parent, wxID_ANY, icon_name, label, wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        (*btn)->SetFont(wxGetApp().normal_font());

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(*btn);
        return sizer;
    };

    auto printhost_browse = [=](wxWindow* parent) 
    {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_browse_btn, "printer_host_browser", _L("Browse") + " " + dots);
        m_printhost_browse_btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent& e) {
            BonjourDialog dialog(this, Preset::printer_technology(*m_config));
            if (dialog.show_and_lookup()) {
                //y32
                std::string get_host = into_u8(dialog.get_selected());
                boost::trim(get_host);
                m_optgroup->set_value("print_host", from_u8(get_host), true);
                m_optgroup->get_field("print_host")->field_changed();
            }
        });

        return sizer;
    };

    auto print_host_test = [=](wxWindow* parent) {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_test_btn, "printer_host_test", _L("Test"));

        m_printhost_test_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
            if (!host) {
                const wxString text = _L("Could not get a valid Printer Host reference");
                show_error(this, text);
                return;
            }
            wxString msg;
            bool result;
            {
                // Show a wait cursor during the connection test, as it is blocking UI.
                wxBusyCursor wait;
                result = host->test(msg);
            }
            if (result)
                show_info(this, host->get_test_ok_msg(), _L("Success!"));
            else
                show_error(this, host->get_test_failed_msg(msg));
            });

        return sizer;
    };

    // Set a wider width for a better alignment
    Option option = m_optgroup->get_option("print_host");
    //y49
    option.opt.width = Field::def_width_wider() * 2;
    Line host_line = m_optgroup->create_single_option_line(option);
    
    host_line.append_widget(printhost_browse);
    host_line.append_widget(print_host_test);
    m_optgroup->append_line(host_line);

    //y49
    option = m_optgroup->get_option("printhost_apikey");
    option.opt.width = Field::def_width_wider() * 2;
    option.opt.full_width = 1;
    m_optgroup->append_single_option_line(option);

    m_optgroup->activate();

    Field* printhost_field = m_optgroup->get_field("print_host");
    if (printhost_field)
    {
        wxTextCtrl* temp = dynamic_cast<wxTextCtrl*>(printhost_field->getWindow());
        if (temp)
            temp->Bind(wxEVT_TEXT, ([printhost_field, temp](wxEvent& e)
            {
#ifndef __WXGTK__
                e.Skip();
                temp->GetToolTip()->Enable(true);
#endif // __WXGTK__
                // Remove all leading and trailing spaces from the input
                std::string trimed_str, str = trimed_str = temp->GetValue().ToStdString();
                boost::trim(trimed_str);
                if (trimed_str != str)
                    temp->SetValue(trimed_str);

                TextCtrl* field = dynamic_cast<TextCtrl*>(printhost_field);
                if (field)
                    field->propagate_value();
            }), temp->GetId());
    }
    update();
}

void PhysicalPrinterDialog::update_printhost_buttons()
{
    std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
    if (host) {
        m_printhost_test_btn->Enable(!m_config->opt_string("print_host").empty() && host->can_test());
        m_printhost_browse_btn->Enable(host->has_auto_discovery());
    }
}

void PhysicalPrinterDialog::update_preset_input() {
    m_printer_name = into_u8(m_input_ctrl->GetValue());

    m_valid_type = Valid;
    wxString info_line;

    const char *unusable_symbols = "<>[]:/\\|?*\"";

    const std::string unusable_suffix = PresetCollection::get_suffix_modified(); //"(modified)";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (m_printer_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line    = _L("Name is invalid;") + "\n" + _L("illegal characters:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }


    if (m_valid_type == Valid && m_printer_name.find(unusable_suffix) != std::string::npos) {
        info_line    = _L("Name is invalid;") + "\n" + _L("illegal suffix:") + "\n\t" + from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid &&
        (m_printer_name == "Default Setting" || m_printer_name == "Default Filament" || m_printer_name == "Default Printer")) {
        info_line    = _L("Name is unavailable.");
        m_valid_type = NoValid;
    }

    // y3
    auto it = exit_machine.find(m_printer_name);
    if(m_valid_type == Valid && it != exit_machine.end() && m_printer_name != old_name)
    {
        info_line = from_u8((boost::format(_u8L("Printer name \"%1%\" already exists.")) % m_printer_name).str());
        m_valid_type = NoValid;  
    }

    // y3
    if(empty_flag)
    {
        empty_flag = false;
        m_valid_type = NoValid;
    }

    else if(m_valid_type == Valid && m_printer_name.empty()) {
        info_line    = _L("The name is not allowed to be empty.");
        m_valid_type = NoValid;
    }
    else if (m_valid_type == Valid && m_printer_name.find_first_of(' ') == 0) {
        info_line = _L("The name is not allowed to start with space character.");
        m_valid_type = NoValid;
    }
    else if(m_valid_type == Valid && m_printer_name.find_last_of(' ') == m_printer_name.length() - 1) {
        info_line = _L("The name is not allowed to end with space character.");
        m_valid_type = NoValid;
    }


    if (m_valid_type == Valid && m_printer_name.find_first_of(' ') == 0) {
        info_line    = _L("The name is not allowed to start with space character.");
        m_valid_type = NoValid;
    }


    if (m_valid_type == Valid && m_presets->get_preset_name_by_alias(m_printer_name) != m_printer_name) {
        info_line    = _L("The name cannot be the same as a preset alias name.");
        m_valid_type = NoValid;
    }

    m_valid_label->SetLabel(info_line);
    // m_input_area->Refresh();
    m_input_ctrl->Refresh();
    m_valid_label->Show(!info_line.IsEmpty());

    if (m_valid_type == NoValid) {
        if (m_button_ok)
        {
            m_button_ok->Disable();
            m_button_ok->SetBackgroundColor(wxColour(200, 200, 200));
        }
    }
    else {
        if (m_button_ok)
        {
            m_button_ok->Enable();
            StateColor btn_bg_blue(std::pair<wxColour, int>(wxColour(95, 82, 253), StateColor::Pressed), std::pair<wxColour, int>(wxColour(129, 150, 255), StateColor::Hovered),
                std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
            m_button_ok->SetBackgroundColor(btn_bg_blue);
        }
            
    }
}

void PhysicalPrinterDialog::update(bool printer_change)
{
    m_optgroup->reload_config();
    update_preset_input();

    // update_printhost_buttons();

    this->SetSize(this->GetBestSize());
    this->Layout();
}

void PhysicalPrinterDialog::update_host_type(bool printer_change)
{
    if (m_config == nullptr)
        return;
    bool all_presets_are_from_mk3_family = false;
    Field* ht = m_optgroup->get_field("host_type");

    wxArrayString types;
    // Append localized enum_labels
    assert(ht->m_opt.enum_labels.size() == ht->m_opt.enum_values.size());
    for (size_t i = 0; i < ht->m_opt.enum_labels.size(); i++) {
        if (ht->m_opt.enum_values[i] == "prusalink" && !all_presets_are_from_mk3_family)
            continue;
        types.Add(_(ht->m_opt.enum_labels[i]));
    }

    Choice* choice = dynamic_cast<Choice*>(ht);
    choice->set_values(types);
    auto set_to_choice_and_config = [this, choice](PrintHostType type) {
        choice->set_value(static_cast<int>(type));
        m_config->set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(type));
    };
    if ((printer_change && all_presets_are_from_mk3_family) || all_presets_are_from_mk3_family)
        set_to_choice_and_config(htPrusaLink);  
    else if ((printer_change && !all_presets_are_from_mk3_family) || (!all_presets_are_from_mk3_family && m_config->option<ConfigOptionEnum<PrintHostType>>("host_type")->value == htPrusaLink))
        set_to_choice_and_config(htOctoPrint);
    else
        choice->set_value(m_config->option("host_type")->getInt());
}

void PhysicalPrinterDialog::update_printers()
{
    wxBusyCursor wait;

    std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));

    wxArrayString printers;
    Field *rs = m_optgroup->get_field("printhost_port");
    try {
        if (! host->get_printers(printers))
            printers.clear();
    } catch (const HostNetworkError &err) {
        printers.clear();
        show_error(this, _L("Connection to printers connected via the print host failed.") + "\n\n" + from_u8(err.what()));
    }
    Choice *choice = dynamic_cast<Choice*>(rs);
    choice->set_values(printers);
    printers.empty() ? rs->disable() : rs->enable();
}

void PhysicalPrinterDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    m_printhost_browse_btn->msw_rescale();
    m_printhost_test_btn->msw_rescale();
    if (m_printhost_cafile_browse_btn)
        m_printhost_cafile_browse_btn->msw_rescale();

    m_optgroup->msw_rescale();

    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void PhysicalPrinterDialog::OnOK(wxMouseEvent& event)
{
    //y25
    std::string now_host = boost::any_cast<std::string>(m_optgroup->get_field("print_host")->get_value());
    if (now_host.empty())
    {
        MessageDialog msg_wingow(nullptr, _L("The host or IP or URL cannot be empty."), "", wxICON_WARNING | wxOK);
        msg_wingow.ShowModal();
        return;
    }
    //31
    std::string ip = now_host;
    if (now_host.find(":") != std::string::npos)
    {
        size_t pos = now_host.find(":");
        ip = now_host.substr(0, pos);
    }
    for (auto exit_host : m_exit_host)
    {
        if (exit_host.find(ip) != std::string::npos)
        {
            MessageDialog msg_wingow(nullptr, _L("A device with the same host (IP or URL) already exists, please re-enter."), "", wxICON_WARNING | wxOK);
            msg_wingow.ShowModal();
            return;
        }
    }

    // y3
    m_printer_name = into_u8(m_input_ctrl->GetValue());
    m_printer_host = boost::any_cast<std::string>(m_optgroup->get_field("print_host")->get_value());
    std::string m_apikey = boost::any_cast<std::string>(m_optgroup->get_field("printhost_apikey")->get_value());
    m_config->set_key_value("name", new ConfigOptionString(into_u8(m_input_ctrl->GetValue())));
    m_config->set_key_value("preset_name", new ConfigOptionString(into_u8(pret_combobox->GetValue())));
    m_config->set_key_value("preset_names", new ConfigOptionString(into_u8(pret_combobox->GetValue())));
    m_config->set_key_value("print_host", new ConfigOptionString(m_printer_host));
    m_config->set_key_value("printhost_apikey", new ConfigOptionString(m_apikey));
    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    m_printer.set_name(m_printer_name);
    if(!old_name.empty())
        printers.save_printer(m_printer, old_name);
    else
        printers.save_printer(m_printer);

    this->EndModal(wxID_YES);
    // y13
    // wxGetApp().SetPresentChange(true);
}

std::string PhysicalPrinterDialog::get_name()
{
    return m_printer_name;
}

std::string PhysicalPrinterDialog::get_host()
{
    return m_printer_host;
}

}}    // namespace Slic3r::GUI
