#include <wx/dcgraph.h>
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "CalibrationPanel.hpp"
#include "I18N.hpp"
#include "Tab.hpp"

namespace Slic3r { namespace GUI {

#define REFRESH_INTERVAL       1000
    
#define INITIAL_NUMBER_OF_MACHINES 0
#define LIST_REFRESH_INTERVAL 200
#define MACHINE_LIST_REFRESH_INTERVAL 2000
wxDEFINE_EVENT(EVT_FINISHED_UPDATE_MLIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_USER_MLIST, wxCommandEvent);

wxString get_calibration_type_name(CalibMode cali_mode)
{
    switch (cali_mode) {
    case CalibMode::Calib_PA_Line:
        //w29
        return _L("Pressure Advance");
    case CalibMode::Calib_Flow_Rate:
        return _L("Flow Rate");
    case CalibMode::Calib_Vol_speed_Tower:
        return _L("Max Volumetric Speed");
    case CalibMode::Calib_Temp_Tower:
        return _L("Temperature");
    case CalibMode::Calib_Retraction_tower:
        return _L("Retraction");
    default:
        return "";
    }
}
//w29


CalibrationPanel::CalibrationPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    //w29
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);

    init_tabpanel();

    wxBoxSizer* sizer_main = new wxBoxSizer(wxVERTICAL);
    sizer_main->Add(m_tabpanel, 1, wxEXPAND, 0);

    SetSizerAndFit(sizer_main);
    Layout();

    init_timer();
    //w29
}

//w29
void CalibrationPanel::create_preset_box(wxWindow* parent, wxBoxSizer* sizer_side_tools) {
    PresetBundle* m_preset_bundle = wxGetApp().preset_bundle;
    auto panel = this;
    m_printer_choice = new TabPresetComboBox(panel, Slic3r::Preset::TYPE_PRINTER);
    m_printer_choice->set_selection_changed_function([this](int selection) {
        if (!m_printer_choice->selection_is_changed_according_to_physical_printers())
        {
            if (!m_printer_choice->is_selected_physical_printer())
                wxGetApp().preset_bundle->physical_printers.unselect_printer();
            std::string preset_name = m_printer_choice->GetString(selection).ToUTF8().data();
            Slic3r::GUI::Tab* printer_tab = Slic3r::GUI::wxGetApp().get_tab(Preset::Type::TYPE_PRINTER);
            printer_tab->select_preset(Preset::remove_suffix_modified(preset_name));
            m_filament_choice->update();
            m_print_choice->update();
        }
        });


    m_filament_choice = new TabPresetComboBox(panel, Slic3r::Preset::TYPE_FILAMENT);
    m_filament_choice->set_selection_changed_function([this](int selection) {
        if (!m_filament_choice->selection_is_changed_according_to_physical_printers())
        {
            if (!m_filament_choice->is_selected_physical_printer())
                wxGetApp().preset_bundle->physical_printers.unselect_printer();
            std::string preset_name = m_filament_choice->GetString(selection).ToUTF8().data();
            if (preset_name.find("PETG") != std::string::npos && m_tabpanel->GetSelection() == 1) {
                wxMessageBox("PETG filaments are not suitable for pressure advance calibration", "Warning", wxOK | wxICON_ERROR);
            }
            Slic3r::GUI::Tab* filament_tab = Slic3r::GUI::wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT);

            filament_tab->select_preset(preset_name);
            const std::string& name = wxGetApp().preset_bundle->filaments.get_selected_preset_name();
            {
                Preset* preset = wxGetApp().preset_bundle->filaments.find_preset(name, false);
                if (preset)
                {
                    if (preset->is_compatible) wxGetApp().preset_bundle->set_filament_preset(0, name);
                }
                wxGetApp().sidebar().combos_filament()[0]->update();
            }
        }
        });

    m_print_choice = new TabPresetComboBox(panel, Slic3r::Preset::TYPE_PRINT);
    m_print_choice->set_selection_changed_function([this](int selection) {
        if (!m_print_choice->selection_is_changed_according_to_physical_printers())
        {
            if (!m_print_choice->is_selected_physical_printer())
                wxGetApp().preset_bundle->physical_printers.unselect_printer();
            std::string preset_name = m_print_choice->GetString(selection).ToUTF8().data();
            Slic3r::GUI::Tab* print_tab = Slic3r::GUI::wxGetApp().get_tab(Preset::Type::TYPE_PRINT);

            print_tab->select_preset(Preset::remove_suffix_modified(preset_name));
        }
        });

    wxPanel* m_printer_panel = new wxPanel(parent);
    ScalableButton* m_printer_icon = new ScalableButton(m_printer_panel, wxID_ANY, "printer");
    Label* m_printer_title = new Label(m_printer_panel, _L("Printer"), LB_PROPAGATE_MOUSE_EVENT);
    m_printer_icon->SetForegroundColour(m_printer_icon->GetForegroundColour());
    wxBoxSizer* h_printer_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_printer_panel->SetSizer(h_printer_sizer);
    h_printer_sizer->Add(FromDIP(10), 0, 0, 0, 0);
    h_printer_sizer->Add(m_printer_icon, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    h_printer_sizer->Add(m_printer_title, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    h_printer_sizer->AddStretchSpacer(1);



    wxPanel* m_filament_panel = new wxPanel(parent);
    ScalableButton* m_filament_icon = new ScalableButton(m_filament_panel, wxID_ANY, "filament");
    Label* m_filament_title = new Label(m_filament_panel, _L("Filament"), LB_PROPAGATE_MOUSE_EVENT);
    m_filament_icon->SetForegroundColour(m_filament_icon->GetForegroundColour());
    wxBoxSizer* h_filament_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_filament_panel->SetSizer(h_filament_sizer);
    h_filament_sizer->Add(FromDIP(10), 0, 0, 0, 0);
    h_filament_sizer->Add(m_filament_icon, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    h_filament_sizer->Add(m_filament_title, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    h_filament_sizer->AddStretchSpacer(1);


    wxPanel* m_process_panel = new wxPanel(parent);
    ScalableButton* m_process_icon = new ScalableButton(m_process_panel, wxID_ANY, "process");
    Label* m_process_title = new Label(m_process_panel, _L("Process"), LB_PROPAGATE_MOUSE_EVENT);
    m_process_icon->SetForegroundColour(m_process_icon->GetForegroundColour());
    wxBoxSizer* h_process_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_process_panel->SetSizer(h_process_sizer);
    h_process_sizer->Add(FromDIP(10), 0, 0, 0, 0);
    h_process_sizer->Add(m_process_icon, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
    h_process_sizer->Add(m_process_title, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    h_process_sizer->AddStretchSpacer(1);

    wxColour bg(241, 241, 241);
    m_printer_icon->SetBackgroundColour(bg);
    m_filament_icon->SetBackgroundColour(bg);
    m_process_icon->SetBackgroundColour(bg);
    m_printer_title->SetBackgroundColour(bg);
    m_filament_title->SetBackgroundColour(bg);
    m_process_title->SetBackgroundColour(bg);
    m_printer_panel->SetBackgroundColour(bg);
    m_filament_panel->SetBackgroundColour(bg);
    m_process_panel->SetBackgroundColour(bg);
    
    sizer_side_tools->Add(m_printer_panel, 0, wxEXPAND);
    sizer_side_tools->AddSpacer(10);
    sizer_side_tools->Add(m_printer_choice, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
    sizer_side_tools->Add(m_filament_panel, 0, wxEXPAND | wxTOP | wxBottom, 10);
    sizer_side_tools->AddSpacer(10);
    sizer_side_tools->Add(m_filament_choice, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);
    sizer_side_tools->Add(m_process_panel, 0, wxEXPAND | wxTOP | wxBottom, 10);
    sizer_side_tools->AddSpacer(10);
    sizer_side_tools->Add(m_print_choice, 0, wxEXPAND  | wxLEFT | wxRIGHT, 5);

    wxStaticLine* line = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    line->SetForegroundColour(bg);
    line->SetBackgroundColour(bg);

    sizer_side_tools->Add(line, 0, wxEXPAND | wxTOP, 10);
    
    sizer_side_tools->Layout();
    this->Layout();
    this->Refresh();

    m_printer_choice->update();
    m_filament_choice->update();
    m_print_choice->update();
    Layout();
    Refresh();
}

void CalibrationPanel::init_tabpanel() {
    //w29

    wxBoxSizer* sizer_side_tools = new wxBoxSizer(wxVERTICAL);
    sizer_side_tools->SetMinSize(wxSize(250, -1));
    //w29
    create_preset_box(this, sizer_side_tools);

    m_tabpanel = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_printer_choice->update();
    m_filament_choice->update();
    m_print_choice->update();
    //w29
    m_tabpanel->SetBackgroundColour(*wxWHITE);
    m_cali_panels[0] = new FlowRateWizard(m_tabpanel);
    m_cali_panels[1] = new PressureAdvanceWizard(m_tabpanel);
    //w29
    m_cali_panels[2] = new MaxVolumetricSpeedWizard(m_tabpanel);
    for (int i = 0; i < (int)CALI_MODE_COUNT; i++) {
        bool selected = false;
        if (i == 0)
            selected = true;
        //w29
        /*m_tabpanel->AddPage(m_cali_panels[i],
            get_calibration_type_name(m_cali_panels[i]->get_calibration_mode()),
            "",
            selected);*/
        if (i == 1) {
            m_tabpanel->AddPage(m_cali_panels[i],
                get_calibration_type_name(CalibMode::Calib_PA_Line),
                "",
                selected);
        }
        else if( i == 0){
            selected = true;
            m_tabpanel->AddPage(m_cali_panels[i],
                get_calibration_type_name(CalibMode::Calib_Flow_Rate),
                "",
                selected);
        }
        else {
            m_tabpanel->AddPage(m_cali_panels[i],
                get_calibration_type_name(m_cali_panels[i]->get_calibration_mode()),
                "",
                selected);
        }
    }

    for (int i = 0; i < (int)CALI_MODE_COUNT; i++)
        m_tabpanel->SetPageImage(i, "");

    auto padding_size = m_tabpanel->GetBtnsListCtrl()->GetPaddingSize(0);
    m_tabpanel->GetBtnsListCtrl()->SetPaddingSize({ FromDIP(15), padding_size.y });

    m_initialized = true;
}

void CalibrationPanel::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());
}
//w29

bool CalibrationPanel::Show(bool show) {
    if (show) {
        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());

        DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (dev) {
            //set a default machine when obj is null
            obj = dev->get_selected_machine();
            if (obj == nullptr) {
                dev->load_last_machine();
                obj = dev->get_selected_machine();
                if (obj)
                    GUI::wxGetApp().sidebar().load_ams_list(obj->dev_id, obj);
            }
            else {
                obj->reset_update_time();
            }
        }

    }
    else {
        m_refresh_timer->Stop();
    }
    return wxPanel::Show(show);
}
//w29

void CalibrationPanel::msw_rescale()
{
    for (int i = 0; i < (int)CALI_MODE_COUNT; i++) {
        m_cali_panels[i]->msw_rescale();
    }
}

CalibrationPanel::~CalibrationPanel() {
    if (m_refresh_timer)
        m_refresh_timer->Stop();
    delete m_refresh_timer;
}

}}