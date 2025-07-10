#include "CalibrationWizardPage.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "MsgDialog.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_CALI_ACTION, wxCommandEvent);
wxDEFINE_EVENT(EVT_CALI_TRAY_CHANGED, wxCommandEvent);

//w29
wxString get_cali_mode_caption_string(CalibMode mode)
{
    //w29
    if (mode == CalibMode::Calib_PA_Line|| mode == CalibMode::Calib_PA_Pattern|| mode == CalibMode::Calib_PA_Tower)
        return _L("Pressure Advance Calibration");
    if (mode == CalibMode::Calib_Flow_Rate)
        return _L("Flow Rate Calibration");
    if (mode == CalibMode::Calib_Vol_speed_Tower)
        return _L("Max Volumetric Speed Calibration");
    //w29
    //if (mode == CalibMode::Calib_Flow_Rate_Coarse || mode == CalibMode::Calib_Flow_Rate_Fine)
      //  return _L("Flow Rate Calibration");
    if (mode == CalibMode::Calib_Flow_Rate_Coarse)
        return _L("Flow Rate Coarse Calibration");
    if (mode == CalibMode::Calib_Flow_Rate_Fine)
        return _L("Flow Rate Fine Calibration");
    return "no cali_mode_caption";
}

//w29
CalibrationFilamentMode get_cali_filament_mode(MachineObject* obj, CalibMode mode)
{
    //w29
    return CalibrationFilamentMode::CALI_MODEL_SINGLE;
}

CalibMode get_obj_calibration_mode(const MachineObject* obj)
{
    CalibrationMethod method;
    int cali_stage;
    return get_obj_calibration_mode(obj, method, cali_stage);
}

CalibMode get_obj_calibration_mode(const MachineObject* obj, int& cali_stage)
{
    CalibrationMethod method;
    return get_obj_calibration_mode(obj, method, cali_stage);
}

CalibMode get_obj_calibration_mode(const MachineObject* obj, CalibrationMethod& method, int& cali_stage)
{
    method = CalibrationMethod::CALI_METHOD_MANUAL;

    if (!obj) return CalibMode::Calib_None;

    if (boost::contains(obj->m_gcode_file, "auto_filament_cali")) {
        method = CalibrationMethod::CALI_METHOD_AUTO;
        return CalibMode::Calib_PA_Line;
    }
    if (boost::contains(obj->m_gcode_file, "user_cali_manual_pa")) {
        method = CalibrationMethod::CALI_METHOD_MANUAL;
        return CalibMode::Calib_PA_Line;
    }
    if (boost::contains(obj->m_gcode_file, "extrusion_cali")) {
        method = CalibrationMethod::CALI_METHOD_MANUAL;
        return CalibMode::Calib_PA_Line;
    }

    if (boost::contains(obj->m_gcode_file, "abs_flowcalib_cali")) {
        method = CalibrationMethod::CALI_METHOD_AUTO;
        return CalibMode::Calib_Flow_Rate;
    }

    //w29
    CalibMode cali_mode = CalibUtils::get_calib_mode_by_name(obj->subtask_name, cali_stage);
    if (cali_mode != CalibMode::Calib_None) {
        method = CalibrationMethod::CALI_METHOD_MANUAL;
    }
    return cali_mode;
}


CaliPageButton::CaliPageButton(wxWindow* parent, CaliPageActionType type, wxString text)
    : m_action_type(type),
    Button(parent, text)
{
    // y96
    StateColor btn_bg_blue(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(40, 90, 220), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(100, 150, 255), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(68, 121, 251), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    StateColor btn_bd_blue(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(68, 121, 251), StateColor::Enabled));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_blue(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    switch (m_action_type)
    {
        //w29
    case CaliPageActionType::CALI_ACTION_START:
        //w29
        this->SetLabel(_L("Calibrate"));
        break;
    case CaliPageActionType::CALI_ACTION_PREV:
        this->SetLabel(_L("Prev"));
        break;
    case CaliPageActionType::CALI_ACTION_NEXT:
        this->SetLabel(_L("Next"));
        break;
    case CaliPageActionType::CALI_ACTION_CALI_NEXT:
        this->SetLabel(_L("Next"));
        break;
    case CaliPageActionType::CALI_ACTION_CALI:
        this->SetLabel(_L("Calibrate"));
        break;
    //w29
    case CaliPageActionType::CALI_ACTION_FLOW_COARSE:
        this->SetLabel(_L("Coarse Calibration"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_FINE:
        this->SetLabel(_L("Fine Calibration"));
        break;
    //w29
    case CaliPageActionType::CALI_ACTION_PA_LINE:
        this->SetLabel(_L("PA Line"));
        break;
    case CaliPageActionType::CALI_ACTION_PA_PATTERN:
        this->SetLabel(_L("PA Pattern"));
        break;
    case CaliPageActionType::CALI_ACTION_PA_TOWER:
        this->SetLabel(_L("PA Tower"));
        break;
    default:
        this->SetLabel("Unknown");
        break;
    }

    switch (m_action_type)
    {
    //w29
    case CaliPageActionType::CALI_ACTION_PREV:
    case CaliPageActionType::CALI_ACTION_START:
    case CaliPageActionType::CALI_ACTION_NEXT:
    case CaliPageActionType::CALI_ACTION_CALI:
    case CaliPageActionType::CALI_ACTION_CALI_NEXT:
    case CaliPageActionType::CALI_ACTION_PA_LINE:
    case CaliPageActionType::CALI_ACTION_PA_PATTERN:
    case CaliPageActionType::CALI_ACTION_PA_TOWER:
    case CaliPageActionType::CALI_ACTION_FLOW_COARSE:
    case CaliPageActionType::CALI_ACTION_FLOW_FINE:
        SetBackgroundColor(btn_bg_blue);
        SetBorderColor(btn_bd_blue);
        SetTextColor(btn_text_blue);
        break;
    default:
        break;
    }

    //w29
    SetBackgroundColour(*wxWHITE);
    SetFont(Label::Body_16);
    SetMinSize(wxSize(-1, FromDIP(30)));
    SetCornerRadius(FromDIP(16));
}

void CaliPageButton::msw_rescale()
{
    SetMinSize(wxSize(-1, FromDIP(24)));
    SetCornerRadius(FromDIP(12));
    Rescale();
}


FilamentComboBox::FilamentComboBox(wxWindow* parent, const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, wxID_ANY, pos, size, wxTAB_TRAVERSAL)
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_comboBox = new CalibrateFilamentComboBox(this);
    m_comboBox->SetSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_comboBox->SetMinSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    main_sizer->Add(m_comboBox->clr_picker, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(8));
    main_sizer->Add(m_comboBox, 0, wxALIGN_CENTER);

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);
}

void FilamentComboBox::set_select_mode(CalibrationFilamentMode mode)
{
    m_mode = mode;
    if (m_checkBox)
        m_checkBox->Show(m_mode == CalibrationFilamentMode::CALI_MODEL_MULITI);
    if (m_radioBox)
        m_radioBox->Show(m_mode == CalibrationFilamentMode::CALI_MODEL_SINGLE);

    Layout();
}

//w29
bool FilamentComboBox::Show(bool show)
{
    return wxPanel::Show(show);
}

bool FilamentComboBox::Enable(bool enable) {
    if (!enable)
        SetValue(false);

    if (m_radioBox)
        m_radioBox->Enable(enable);
    if (m_checkBox)
        m_checkBox->Enable(enable);
    return wxPanel::Enable(enable);
}

void FilamentComboBox::SetValue(bool value, bool send_event) {
    if (m_radioBox) {
        if (value == m_radioBox->GetValue()) {
            if (m_checkBox) {
                if (value == m_checkBox->GetValue())
                    return;
            }
            else {
                return;
            }
        }
    }
    if (m_radioBox)
        m_radioBox->SetValue(value);
    if (m_checkBox)
        m_checkBox->SetValue(value);
}

void FilamentComboBox::msw_rescale()
{
    //m_checkBox->Rescale();
    m_comboBox->SetSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_comboBox->SetMinSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_comboBox->msw_rescale();
}



CaliPageCaption::CaliPageCaption(wxWindow* parent, CalibMode cali_mode,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    init_bitmaps();

    SetBackgroundColour(*wxWHITE);

    auto top_sizer = new wxBoxSizer(wxVERTICAL);
    auto caption_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_prev_btn = new ScalableButton(this, wxID_ANY, "cali_page_caption_prev",
        wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 30);
    m_prev_btn->SetBackgroundColour(*wxWHITE);
    caption_sizer->Add(m_prev_btn, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(10));

    wxString title = get_cali_mode_caption_string(cali_mode);
    Label* title_text = new Label(this, title);
    title_text->SetFont(Label::Head_20);
    title_text->Wrap(-1);
    caption_sizer->Add(title_text, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(10));
    caption_sizer->AddStretchSpacer();
    //w29

    top_sizer->Add(caption_sizer, 1, wxEXPAND);
    top_sizer->AddSpacer(FromDIP(35));
    this->SetSizer(top_sizer);
    top_sizer->Fit(this);

    // hover effect
    //m_prev_btn->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
    //    m_prev_btn->SetBitmap(m_prev_bmp_hover.bmp());
    //});

    //m_prev_btn->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
    //    m_prev_btn->SetBitmap(m_prev_bmp_normal.bmp());
    //});

    // send event
    m_prev_btn->Bind(wxEVT_BUTTON, [this](auto& e) {
        wxCommandEvent event(EVT_CALI_ACTION);
        event.SetEventObject(m_parent);
        event.SetInt((int)(CaliPageActionType::CALI_ACTION_GO_HOME));
        wxPostEvent(m_parent, event);
        });

#ifdef __linux__
    wxGetApp().CallAfter([this, title_text]() {
        title_text->SetMinSize(title_text->GetSize() + wxSize{ FromDIP(150), title_text->GetCharHeight() / 2 });
        Layout();
        Fit();
        });
#endif
}

void CaliPageCaption::init_bitmaps() {
    m_prev_bmp_normal = ScalableBitmap(this, "cali_page_caption_prev", 30);
    m_prev_bmp_hover = ScalableBitmap(this, "cali_page_caption_prev_hover", 30);
    m_help_bmp_normal = ScalableBitmap(this, "cali_page_caption_help", 30);
    m_help_bmp_hover = ScalableBitmap(this, "cali_page_caption_help_hover", 30);
}

//w29
void CaliPageCaption::show_prev_btn(bool show)
{
    m_prev_btn->Show(show);
}

//w29
void CaliPageCaption::msw_rescale()
{
    m_prev_btn->msw_rescale();
}

CaliPageStepGuide::CaliPageStepGuide(wxWindow* parent, wxArrayString steps,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style),
    m_steps(steps)
{
    SetBackgroundColour(*wxWHITE);

    auto top_sizer = new wxBoxSizer(wxVERTICAL);

    m_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_step_sizer->AddSpacer(FromDIP(90));
    for (int i = 0; i < m_steps.size(); i++) {
        Label* step_text = new Label(this, m_steps[i]);
        step_text->SetForegroundColour(wxColour(206, 206, 206));
        m_text_steps.push_back(step_text);
        m_step_sizer->Add(step_text, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(15));
        if (i != m_steps.size() - 1) {
            auto line = new wxPanel(this, wxID_ANY, wxDefaultPosition);
            line->SetBackgroundColour(*wxBLACK);
            m_step_sizer->Add(line, 1, wxALIGN_CENTER);
        }
    }
    m_step_sizer->AddSpacer(FromDIP(90));

    top_sizer->Add(m_step_sizer, 0, wxEXPAND);
    top_sizer->AddSpacer(FromDIP(30));
    this->SetSizer(top_sizer);
    top_sizer->Fit(this);

    wxGetApp().UpdateDarkUIWin(this);
}

void CaliPageStepGuide::set_steps(int index)
{
    for (Label* text_step : m_text_steps) {
        text_step->SetForegroundColour(wxColour(206, 206, 206));
    }
    m_text_steps[index]->SetForegroundColour(*wxBLACK);

    wxGetApp().UpdateDarkUIWin(this);
}

void CaliPageStepGuide::set_steps_string(wxArrayString steps)
{
    m_steps.Clear();
    m_text_steps.clear();
    m_step_sizer->Clear(true);
    m_steps = steps;
    m_step_sizer->AddSpacer(FromDIP(90));
    for (int i = 0; i < m_steps.size(); i++) {
        Label* step_text = new Label(this, m_steps[i]);
        step_text->SetForegroundColour(wxColour(206, 206, 206));
        m_text_steps.push_back(step_text);
        m_step_sizer->Add(step_text, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(15));
        if (i != m_steps.size() - 1) {
            auto line = new wxPanel(this, wxID_ANY, wxDefaultPosition);
            line->SetBackgroundColour(*wxBLACK);
            m_step_sizer->Add(line, 1, wxALIGN_CENTER);
        }
    }
    m_step_sizer->AddSpacer(FromDIP(90));

    wxGetApp().UpdateDarkUIWin(this);

    Layout();
}


CaliPagePicture::CaliPagePicture(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style) 
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(wxColour(0xCECECE));
    auto top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->AddStretchSpacer();
    m_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap);
    top_sizer->Add(m_img);
    top_sizer->AddStretchSpacer();
    this->SetSizer(top_sizer);
    top_sizer->Fit(this);
}

void CaliPagePicture::set_bmp(const ScalableBitmap& bmp)
{
    m_bmp = bmp;
    m_img->SetBitmap(m_bmp.bmp());
}

void CaliPagePicture::msw_rescale()
{
    m_bmp.msw_rescale();
    m_img->SetBitmap(m_bmp.bmp());
}
//w29

CaliPageActionPanel::CaliPageActionPanel(wxWindow* parent,
    CalibMode cali_mode,
    CaliPageType page_type,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    m_parent = parent;

    wxWindow* btn_parent = this;
    //w29
    if (cali_mode == CalibMode::Calib_PA_Line) {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_PA_LINE));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_PA_PATTERN));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_PA_TOWER));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI_NEXT));
        }
    }
    else if (cali_mode == CalibMode::Calib_Flow_Rate) {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_COARSE));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_FINE));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI_NEXT));
        }
    }
    else {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_START));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI_NEXT));
        }
        else {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_PREV));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_NEXT));
        }
        
    }

    auto top_sizer = new wxBoxSizer(wxHORIZONTAL);

    top_sizer->Add(0, 0, 1, wxEXPAND, 0);
    for (int i = 0; i < m_action_btns.size(); i++) {
        top_sizer->Add(m_action_btns[i], 0, wxALL, FromDIP(5));

        m_action_btns[i]->Bind(wxEVT_BUTTON,
            [this, i](wxCommandEvent& evt) {
                wxCommandEvent event(EVT_CALI_ACTION);
                event.SetEventObject(m_parent);
                event.SetInt((int)m_action_btns[i]->get_action_type());
                wxPostEvent(m_parent, event);
            });
    }
    top_sizer->Add(0, 0, 1, wxEXPAND, 0);

    this->SetSizer(top_sizer);
    top_sizer->Fit(this);
}

void CaliPageActionPanel::bind_button(CaliPageActionType action_type, bool is_block)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {

            if (is_block) {
                m_action_btns[i]->Bind(wxEVT_BUTTON,
                    [this](wxCommandEvent& evt) {
                        MessageDialog msg(nullptr, _L("The current firmware version of the printer does not support calibration.\nPlease upgrade the printer firmware."), _L("Calibration not supported"), wxOK | wxICON_WARNING);
                        msg.ShowModal();
                    });
            }
            else {
                m_action_btns[i]->Bind(wxEVT_BUTTON,
                    [this, i](wxCommandEvent& evt) {
                        wxCommandEvent event(EVT_CALI_ACTION);
                        event.SetEventObject(m_parent);
                        event.SetInt((int)m_action_btns[i]->get_action_type());
                        wxPostEvent(m_parent, event);
                    });
            }
        }
    }

}

void CaliPageActionPanel::show_button(CaliPageActionType action_type, bool show)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {
            m_action_btns[i]->Show(show);
        }
    }
    Layout();
}

void CaliPageActionPanel::enable_button(CaliPageActionType action_type, bool enable)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {
            m_action_btns[i]->Enable(enable);
        }
    }
}

void CaliPageActionPanel::msw_rescale()
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        m_action_btns[i]->msw_rescale();
    }
}
//w29

CalibrationWizardPage::CalibrationWizardPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
    , m_parent(parent)
{
    SetBackgroundColour(*wxWHITE);
    SetMinSize({ MIN_CALIBRATION_PAGE_WIDTH, -1 });
}

void CalibrationWizardPage::msw_rescale()
{
    m_page_caption->msw_rescale();
    m_action_panel->msw_rescale();
}
//w29
}
}
