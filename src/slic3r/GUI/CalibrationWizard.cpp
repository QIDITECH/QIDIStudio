#include "CalibrationWizard.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "CalibrationWizardPage.hpp"
#include "../../libslic3r/Calib.hpp"
#include "Tabbook.hpp"
#include "CaliHistoryDialog.hpp"
#include "CalibUtils.hpp"
#include "QDTUtil.hpp"

#include "DeviceCore/DevInfo.h"
#include "DeviceCore/DevManager.h"

//w29
#include "MainFrame.hpp"

namespace Slic3r { namespace GUI {

#define CALIBRATION_DEBUG

wxDEFINE_EVENT(EVT_DEVICE_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_CALIBRATION_JOB_FINISHED, wxCommandEvent);

static const wxString NA_STR = _L("N/A");
static const int MAX_PA_HISTORY_RESULTS_NUMS = 16;

//w29
CalibrationWizard::CalibrationWizard(wxWindow* parent, CalibMode mode, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style) 
    , m_mode(mode)
{
    SetBackgroundColour(wxColour("#EEEEEE"));

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    //w29
    m_scrolledWindow->SetScrollRate(20, 20);
    m_scrolledWindow->SetBackgroundColour(*wxWHITE);

    wxBoxSizer* padding_sizer = new wxBoxSizer(wxHORIZONTAL);
    padding_sizer->Add(0, 0, 1);    
    
    m_all_pages_sizer = new wxBoxSizer(wxVERTICAL);
    padding_sizer->Add(m_all_pages_sizer, 0);

    padding_sizer->Add(0, 0, 1);

    m_scrolledWindow->SetSizer(padding_sizer);

    main_sizer->Add(m_scrolledWindow, 1, wxEXPAND | wxALL, FromDIP(10));

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);
    //w29

#if !QDT_RELEASE_TO_PUBLIC
    this->Bind(wxEVT_CHAR_HOOK, [this](auto& evt) {
        const int keyCode = evt.GetKeyCode();
        switch (keyCode)
        {
        case WXK_PAGEUP:
        {
            show_step(m_curr_step->prev);
            break;
        }
        case WXK_PAGEDOWN:
        {
            show_step(m_curr_step->next);
            break;
        }
        default:
            evt.Skip();
            break;
        }
        });
#endif
}

CalibrationWizard::~CalibrationWizard()
{
    ;
}

//w29
void CalibrationWizard::show_step(CalibrationWizardPageStep* step)
{
    if (!step)
        return;

    if (m_curr_step) {
        m_curr_step->page->Hide();
    }

    m_curr_step = step;

    if (m_curr_step) {
        m_curr_step->page->Show();
    }

    Layout();
}

void CalibrationWizard::update(MachineObject* obj)
{
    curr_obj = obj;

    /* only update curr step
    if (m_curr_step) {
        m_curr_step->page->update(obj);
    }
    */
    //w29

    // update all page steps
    for (int i = 0; i < m_page_steps.size(); i++) {
        if (m_page_steps[i]->page)
            m_page_steps[i]->page->update(obj);
    }
}

//w29

void CalibrationWizard::msw_rescale() 
{
    for (int i = 0; i < m_page_steps.size(); i++) {
        if (m_page_steps[i]->page)
            m_page_steps[i]->page->msw_rescale();
    }
}
//w29

void CalibrationWizard::on_cali_go_home()
{
    // can go home? confirm to continue
    CalibrationMethod method;
    int cali_stage = 0;
    CalibMode obj_cali_mode = get_obj_calibration_mode(curr_obj, method, cali_stage);

    bool double_confirm = false;
    CaliPageType page_type = get_curr_step()->page->get_page_type();
    //w29
    if (!m_page_steps.empty()) {
        show_step(m_page_steps.front());
    }
}

PressureAdvanceWizard::PressureAdvanceWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, m_mode, id, pos, size, style)//w29
   // : CalibrationWizard(parent, CalibMode::Calib_PA_Line, id, pos, size, style)
{
    create_pages();
}
//w29

void PressureAdvanceWizard::create_pages()
{
    start_step  = new CalibrationWizardPageStep(new CalibrationPAStartPage(m_scrolledWindow));
    preset_step = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, CalibMode::Calib_PA_Line, false));
    //w29
    pa_line = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, CalibMode::Calib_PA_Line, false));
    pa_pattern = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, CalibMode::Calib_PA_Pattern, false));
    pa_tower = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, CalibMode::Calib_PA_Tower, false));
    //w29
    
    m_all_pages_sizer->Add(start_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(preset_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    //w29
    m_all_pages_sizer->Add(pa_line->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(pa_pattern->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(pa_tower->page, 1, wxEXPAND | wxALL, FromDIP(25));
    //w29

    m_page_steps.push_back(start_step);
    m_page_steps.push_back(preset_step);
    //w29
    m_page_steps.push_back(pa_line);
    m_page_steps.push_back(pa_pattern);
    m_page_steps.push_back(pa_tower);

    //w29
    pa_line->page->set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);
    pa_pattern->page->set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);
    pa_tower->page->set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);

    for (int i = 0; i < m_page_steps.size() -1; i++) {
        m_page_steps[i]->chain(m_page_steps[i+1]);
    }

    for (int i = 0; i < m_page_steps.size(); i++) {
        m_page_steps[i]->page->Hide();
        m_page_steps[i]->page->Bind(EVT_CALI_ACTION, &PressureAdvanceWizard::on_cali_action, this);
    }

    if (!m_page_steps.empty())
        show_step(m_page_steps.front());
}

void PressureAdvanceWizard::on_cali_action(wxCommandEvent& evt)
{
    CaliPageActionType action = static_cast<CaliPageActionType>(evt.GetInt());
    //w29
    if (action == CaliPageActionType::CALI_ACTION_NEXT) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI_NEXT) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_PREV) {
        show_step(m_curr_step->prev);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI) {
        on_cali_start();
    }
    //w29
    else if (action == CaliPageActionType::CALI_ACTION_PA_LINE) {
        show_step(pa_line);
    }
    else if (action == CaliPageActionType::CALI_ACTION_PA_PATTERN) {
        show_step(pa_pattern);
    }
    else if (action == CaliPageActionType::CALI_ACTION_PA_TOWER) {
        show_step(pa_tower);
    }
    else if (action == CaliPageActionType::CALI_ACTION_GO_HOME) {
        on_cali_go_home();
    }

}

void PressureAdvanceWizard::update(MachineObject* obj)
{
    CalibrationWizard::update(obj);

    if (!obj)
        return;

    if (!m_show_result_dialog) {
        if (obj->cali_version != -1 && obj->cali_version != cali_version) {
            cali_version = obj->cali_version;
            CalibUtils::emit_get_PA_calib_info(obj->nozzle_diameter, "");
        }
    }
}
//w29

void PressureAdvanceWizard::on_cali_start()
{
    //w29
    {
    CalibrationPresetPage* preset_page = nullptr;// = (static_cast<CalibrationPresetPage*>(preset_step->page));
    if (m_curr_step == pa_line)
        preset_page = (static_cast<CalibrationPresetPage*>(pa_line->page));
    if (m_curr_step == pa_pattern)
        preset_page = (static_cast<CalibrationPresetPage*>(pa_pattern->page));
    if (m_curr_step == pa_tower)
        preset_page = (static_cast<CalibrationPresetPage*>(pa_tower->page));
    CalibInfo calib_info;
    wxArrayString values = preset_page->get_custom_range_values();
    if (values.size() != 3) {
        MessageDialog msg_dlg(nullptr, _L("The input value size must be 3."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    else {
        values[0].ToDouble(&calib_info.params.start);
        values[1].ToDouble(&calib_info.params.end);
        values[2].ToDouble(&calib_info.params.step);
    }
    calib_info.params.print_numbers = true;

    if (calib_info.params.start < 0 || calib_info.params.step < 0.001 || calib_info.params.end < calib_info.params.start + calib_info.params.step) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nStart PA: >= 0.0\nEnd PA: > Start PA\nPA step: >= 0.001"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    Calib_Params m_paramas;
    m_paramas.start = calib_info.params.start;
    m_paramas.end = calib_info.params.end;
    m_paramas.step = calib_info.params.step;
    m_paramas.mode = calib_info.params.mode;
    
    if (m_curr_step == pa_line)
        m_paramas.mode = CalibMode::Calib_PA_Line;
    if (m_curr_step == pa_pattern)
        m_paramas.mode = CalibMode::Calib_PA_Pattern;
    if (m_curr_step == pa_tower)
        m_paramas.mode = CalibMode::Calib_PA_Tower;


    wxGetApp().plater_->calib_pa(m_paramas);
    }
}
//w29

FlowRateWizard::FlowRateWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    //w29
    : CalibrationWizard(parent, m_mode, id, pos, size, style)//: CalibrationWizard(parent, CalibMode::Calib_Flow_Rate, id, pos, size, style)
{
    create_pages();
}

void FlowRateWizard::create_pages()
{
    start_step = new CalibrationWizardPageStep(new CalibrationFlowRateStartPage(m_scrolledWindow));
    preset_step = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, m_mode, false));
    coarse_calib_step = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, CalibMode::Calib_Flow_Rate_Coarse, false));
    fine_calib_step = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, CalibMode::Calib_Flow_Rate_Fine, false));
    
    //w29
    m_all_pages_sizer->Add(start_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(preset_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(coarse_calib_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(fine_calib_step->page, 1, wxEXPAND | wxALL, FromDIP(25));

    //w29
    m_page_steps.push_back(start_step);
    m_page_steps.push_back(preset_step);
    m_page_steps.push_back(coarse_calib_step);
    m_page_steps.push_back(fine_calib_step);

    //w29
    coarse_calib_step->page->set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);
    fine_calib_step->page->set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);

    for (int i = 0; i < m_page_steps.size() - 1; i++) {
        m_page_steps[i]->chain(m_page_steps[i + 1]);
    }

    for (int i = 0; i < m_page_steps.size(); i++) {
        m_page_steps[i]->page->Hide();
        m_page_steps[i]->page->Bind(EVT_CALI_ACTION, &FlowRateWizard::on_cali_action, this);
    }

    if (!m_page_steps.empty())
        show_step(m_page_steps.front());
}

void FlowRateWizard::on_cali_action(wxCommandEvent& evt)
{

    //w29
    CaliPageActionType action = static_cast<CaliPageActionType>(evt.GetInt());
    if (action == CaliPageActionType::CALI_ACTION_NEXT) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI_NEXT) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_PREV) {
        show_step(m_curr_step->prev);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI) {
        if (m_curr_step == coarse_calib_step)
            cali_start_pass(1);
        else if(m_curr_step == fine_calib_step)
            cali_start_pass(2);
    }
    else if (action == CaliPageActionType::CALI_ACTION_GO_HOME) {
        on_cali_go_home();
    }
    //w29
    else if (action == CaliPageActionType::CALI_ACTION_FLOW_COARSE) {
        show_step(coarse_calib_step);
    }
    else if (action == CaliPageActionType::CALI_ACTION_FLOW_FINE) {
        //this->set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);
        show_step(fine_calib_step);
    }
}

void FlowRateWizard::cali_start_pass(int road) {
    CalibrationPresetPage* preset_page = nullptr;// = (static_cast<CalibrationPresetPage*>(preset_step->page));
    if (m_curr_step == coarse_calib_step)
        preset_page = (static_cast<CalibrationPresetPage*>(coarse_calib_step->page));
    if (m_curr_step == fine_calib_step)
        preset_page = (static_cast<CalibrationPresetPage*>(fine_calib_step->page));
    wxArrayString values = preset_page->get_custom_range_values();
    double input_value;
    if (values.size() != 1) {
        MessageDialog msg_dlg(nullptr, _L("The input value size must be 1."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    else {
        values[0].ToDouble(&input_value);
    }
    if (input_value <= 0 || input_value >= 2) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\n0.0< Flow Ratio < 2"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    wxGetApp().plater_->calib_flowrate(road, input_value);
}
//w29

void FlowRateWizard::update(MachineObject* obj)
{
    CalibrationWizard::update(obj);
}

//w29
MaxVolumetricSpeedWizard::MaxVolumetricSpeedWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_Vol_speed_Tower, id, pos, size, style)
{
    create_pages();
}

void MaxVolumetricSpeedWizard::create_pages() 
{
    start_step  = new CalibrationWizardPageStep(new CalibrationMaxVolumetricSpeedStartPage(m_scrolledWindow));
    preset_step = new CalibrationWizardPageStep(new MaxVolumetricSpeedPresetPage(m_scrolledWindow, m_mode, true));
    //w29

    m_all_pages_sizer->Add(start_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(preset_step->page, 1, wxEXPAND | wxALL, FromDIP(25));

    //w29
    m_page_steps.push_back(start_step);
    m_page_steps.push_back(preset_step);
    preset_step->page->set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);
    for (int i = 0; i < m_page_steps.size() - 1; i++) {
        m_page_steps[i]->chain(m_page_steps[i + 1]);
    }

    for (int i = 0; i < m_page_steps.size(); i++) {
        m_page_steps[i]->page->Hide();
        m_page_steps[i]->page->Bind(EVT_CALI_ACTION, &MaxVolumetricSpeedWizard::on_cali_action, this);
    }

    for (auto page_step : m_page_steps) {
        page_step->page->Hide();
    }

    if (!m_page_steps.empty())
        show_step(m_page_steps.front());
    return;
}

void MaxVolumetricSpeedWizard::on_cali_action(wxCommandEvent& evt)
{
    CaliPageActionType action = static_cast<CaliPageActionType>(evt.GetInt());

    if (action == CaliPageActionType::CALI_ACTION_START) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_PREV) {
        show_step(m_curr_step->prev);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI) {
        on_cali_start();
    }
    else if (action == CaliPageActionType::CALI_ACTION_NEXT) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_GO_HOME) {
        on_cali_go_home();
    }
}

void MaxVolumetricSpeedWizard::on_cali_start()
{
    //w29
    CalibrationPresetPage* preset_page = (static_cast<CalibrationPresetPage*>(preset_step->page));
    wxArrayString values = preset_page->get_custom_range_values();
    Calib_Params  params;
    if (values.size() != 3) {
        MessageDialog msg_dlg(nullptr, _L("The input value size must be 3."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    else {
        values[0].ToDouble(&params.start);
        values[1].ToDouble(&params.end);
        values[2].ToDouble(&params.step);
    }
    params.mode = m_mode;
    CalibInfo calib_info;
    calib_info.params = params;
    Calib_Params m_paramas;
    m_paramas.start = calib_info.params.start;
    m_paramas.end = calib_info.params.end;
    m_paramas.step = calib_info.params.step;

    if (m_paramas.start <= 0 || m_paramas.step < 0.01 || m_paramas.end < (m_paramas.start + m_paramas.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nstart > 0 \nstep >= 0.01\nend >= start + step"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_paramas.mode = CalibMode::Calib_Vol_speed_Tower;
    wxGetApp().plater_->calib_max_vol_speed(m_paramas);
    wxGetApp().mainframe->select_tab(size_t(MainFrame::tpPreview));

}
//w29

}}
