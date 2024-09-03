#include "CalibrationWizardStartPage.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r { namespace GUI {

#define CALIBRATION_START_PAGE_TEXT_MAX_LENGTH FromDIP(1000)
CalibrationStartPage::CalibrationStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    :CalibrationWizardPage(parent, id, pos, size, style)
{
    m_top_sizer = new wxBoxSizer(wxVERTICAL);
}

void CalibrationStartPage::create_when(wxWindow* parent, wxString title, wxString content)
{
    m_when_title = new Label(this, title);
    m_when_title->SetFont(Label::Head_14);
    m_when_title->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    m_when_title->SetMinSize({CALIBRATION_START_PAGE_TEXT_MAX_LENGTH, -1});

    m_when_content = new Label(this, content);;
    m_when_content->SetFont(Label::Body_14);
    m_when_content->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    m_when_content->SetMinSize({CALIBRATION_START_PAGE_TEXT_MAX_LENGTH, -1});
}

void CalibrationStartPage::create_about(wxWindow* parent, wxString title, wxString content)
{
    m_about_title = new Label(this, title);
    m_about_title->SetFont(Label::Head_14);
    m_about_title->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    m_about_title->SetMinSize({CALIBRATION_START_PAGE_TEXT_MAX_LENGTH, -1});

    m_about_content = new Label(this, content);
    m_about_content->SetFont(Label::Body_14);
    m_about_content->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    m_about_content->SetMinSize({CALIBRATION_START_PAGE_TEXT_MAX_LENGTH, -1});
}

void CalibrationStartPage::create_bitmap(wxWindow* parent, const wxBitmap& before_img, const wxBitmap& after_img)
{
    if (!m_before_bmp)
        m_before_bmp = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_before_bmp->SetBitmap(before_img);
    if (!m_after_bmp)
        m_after_bmp = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_after_bmp->SetBitmap(after_img);
    if (!m_images_sizer) {
        m_images_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_images_sizer->Add(m_before_bmp, 0, wxALL, 0);
        m_images_sizer->AddSpacer(FromDIP(20));
        m_images_sizer->Add(m_after_bmp, 0, wxALL, 0);
    }
}

void CalibrationStartPage::create_bitmap(wxWindow* parent, std::string before_img, std::string after_img)
{
    wxBitmap before_bmp = create_scaled_bitmap(before_img, this, 350);
    wxBitmap after_bmp = create_scaled_bitmap(after_img, this, 350);

    create_bitmap(parent, before_bmp, after_bmp);
}

void CalibrationStartPage::create_bitmap(wxWindow* parent, std::string img) {
    wxBitmap before_bmp = create_scaled_bitmap(img, this, 350);
    if (!m_bmp_intro)
        m_bmp_intro = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_bmp_intro->SetBitmap(before_bmp);
    if (!m_images_sizer) {
        m_images_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_images_sizer->Add(m_bmp_intro, 0, wxALL, 0);
    }
}

//w29
void CalibrationStartPage::add_bitmap(wxWindow* parent, wxBoxSizer* m_top_sizer, std::string img , bool can_modify ,int modify_size) {
    wxBitmap before_bmp; //= create_scaled_bitmap(img, this, 350);
    if(can_modify)
        before_bmp = create_scaled_bitmap(img, this, modify_size);
    else
        before_bmp = create_scaled_bitmap(img, this, 350);
    wxStaticBitmap* m_bmp_intro_temp{ nullptr };
    wxBoxSizer* m_images_sizer_temp{ nullptr };
    if (!m_bmp_intro_temp)
        m_bmp_intro_temp = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_bmp_intro_temp->SetBitmap(before_bmp);
    m_images_sizer_temp = new wxBoxSizer(wxHORIZONTAL);
    m_images_sizer_temp->Add(m_bmp_intro_temp, 0, wxALL, 0);
    if(can_modify)
        m_top_sizer->Add(m_images_sizer_temp, 0, wxALIGN_CENTER, 0);
    else
        m_top_sizer->Add(m_images_sizer_temp, 0, wxALL, 0);
    if (!can_modify)
    m_top_sizer->AddSpacer(PRESET_GAP);
    m_bmp_intro_temp = { nullptr };
    m_images_sizer_temp = { nullptr };
}
void CalibrationStartPage::create_paragraph(wxWindow* parent, Label* title, std::string title_txt, Label* content, std::string content_txt) {
    title = new Label(this, _L(title_txt));
    title->SetFont(Label::Head_14);
    title->Wrap(FromDIP(1100));
    title->SetMinSize({ FromDIP(1100), -1 });

    content = new Label(this, _L(content_txt));;
    content->SetFont(Label::Body_14);
    content->Wrap(FromDIP(1100));
    content->SetMinSize({ FromDIP(1100), -1 });
    m_top_sizer->Add(title);
    m_top_sizer->Add(content);
    m_top_sizer->AddSpacer(PRESET_GAP);
}
void CalibrationStartPage::create_txt(wxWindow* parent, Label* label, std::string label_txt) {
    label = new Label(parent, _L(label_txt));
    label->SetFont(Label::Body_14);
    label->Wrap(FromDIP(1100));
    label->SetMinSize({ FromDIP(1100), -1 });
    m_top_sizer->Add(label);
    m_top_sizer->AddSpacer(PRESET_GAP);
}


CalibrationPAStartPage::CalibrationPAStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationStartPage(parent, id, pos, size, style)
{
    m_cali_mode = CalibMode::Calib_PA_Line;
    m_page_type = CaliPageType::CALI_PAGE_START;

    create_page(this);

    this->SetSizer(m_top_sizer);
    Layout();
    m_top_sizer->Fit(this);
}

CaliPresetTipsstartPanel::CaliPresetTipsstartPanel(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos,
    const wxSize& size,
    long style)
    : CalibrationStartPage(parent, id, pos, size, style)
{
    this->SetBackgroundColour(wxColour(238, 238, 238));
    this->SetMinSize(wxSize(MIN_CALIBRATION_PAGE_WIDTH, -1));

    m_top_sizer = new wxBoxSizer(wxVERTICAL);

    create_panel(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CaliPresetTipsstartPanel::create_panel(wxWindow* parent)
{
    m_top_sizer->AddSpacer(FromDIP(10));

    auto preset_panel_tips = new Label(parent, _L("After completing the pressure pre calibration process, please create a new project before printing."));
    preset_panel_tips->SetFont(Label::Body_14);
    preset_panel_tips->Wrap(CALIBRATION_TEXT_MAX_LENGTH * 1.5f);
    m_top_sizer->Add(preset_panel_tips, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    add_bitmap(parent, m_top_sizer, "newProject",true,95);

    //m_top_sizer->AddSpacer(FromDIP(10));

    auto info_sizer = new wxFlexGridSizer(0, 3, 0, FromDIP(10));
    info_sizer->SetFlexibleDirection(wxBOTH);
    info_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_top_sizer->Add(info_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    m_top_sizer->AddSpacer(FromDIP(10));
}

void CalibrationPAStartPage::create_page(wxWindow* parent)
{
    m_page_caption = new CaliPageCaption(parent, CalibMode::Calib_PA_Line);
    m_page_caption->show_prev_btn(false);
    m_top_sizer->Add(m_page_caption, 0, wxALIGN_CENTER, 0);

    Label* title_1{ nullptr };
    std::string title_text_1 = "What is Pressure Advance Calibration ?";
    Label* content_1{ nullptr };
    std::string content_text_1 = "From fluid mechanics, when a newtonian fluid flow through a hole, it needs pressure, and the pressure is proportional to the flow rate.\
            \nAs the filament is not rigid body, when the extruder starts to extrude, the filament will be compressed to generate the pressure. The compression process will delay the response of the real flow, as the extruder only provides the amount of the filament that needs to extrude, no extra.";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    add_bitmap(parent, m_top_sizer, "PressureAdvanceCompare",true, 346);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* title_2{ nullptr };
    std::string title_text_2 = "When to Calibrate Pressure in Advance";
    Label* content_2{ nullptr };
    std::string content_text_2 = "1.Use different brands of filaments, or the filaments are damp;\
        \n2.The nozzle is worn or replaced with a different size nozzle;\
        \n3.Use different printing parameters such as temperature and line width;\
        \n4.PA calibration does not work with PETG.";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    Label* introduce{ nullptr };
    std::string introduce_text = "We have provided 3 calibration modes. Click the button below to enter the corresponding calibration page.\
        \nBefore calibration, you need to select the printer you are using, the consumables that need to be calibrated, and the process. You can directly select them in the upper left corner of the current page.";
    create_txt(parent, introduce, introduce_text);
    
    //w29
    m_action_panel = new CaliPageActionPanel(parent, CalibMode::Calib_PA_Line, CaliPageType::CALI_PAGE_START);

    m_top_sizer->Add(m_action_panel, 0, wxEXPAND, 0);
    CaliPresetTipsstartPanel* m_tips_panel = new CaliPresetTipsstartPanel(parent);
    m_top_sizer->Add(m_tips_panel, 0);
    m_top_sizer->AddSpacer(PRESET_GAP);

#ifdef __linux__
    wxGetApp().CallAfter([this, title_1, title_2, content_1, content_2, introduce]() {
        title_1->SetMinSize(title_1->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        title_2->SetMinSize(title_2->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        content_1->SetMinSize(content_1->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        content_2->SetMinSize(content_2->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        introduce->SetMinSize(introduce_text->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        Layout();
        Fit();
        });
#endif
}
//w29

void CalibrationPAStartPage::msw_rescale()
{
    CalibrationWizardPage::msw_rescale();
    //w29
    if (wxGetApp().app_config->get_language_code() == "zh-cn") {
        create_bitmap(this, "cali_page_before_pa_CN", "cali_page_after_pa_CN");
    } else {
        create_bitmap(this, "cali_page_before_pa", "cali_page_after_pa");
    }
}

CalibrationFlowRateStartPage::CalibrationFlowRateStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationStartPage(parent, id, pos, size, style)
{
    m_cali_mode = CalibMode::Calib_Flow_Rate;

    create_page(this);

    this->SetSizer(m_top_sizer);
    Layout();
    m_top_sizer->Fit(this);
}

void CalibrationFlowRateStartPage::create_page(wxWindow* parent)
{
    //w29
    m_page_caption = new CaliPageCaption(parent, CalibMode::Calib_Flow_Rate);
    m_page_caption->show_prev_btn(false);
    m_top_sizer->Add(m_page_caption, 0, wxALIGN_CENTER, 0);

    Label* introduce{ nullptr };
    std::string introduce_text = "When using official filaments, the default values of the software are obtained through our testing, and usually perform well in the vast majority of printing situations.";
    create_txt(parent, introduce, introduce_text);

    Label* title_1{nullptr};
    std::string title_text_1 = "When do you need Flowrate Calibration";
    Label* content_1{ nullptr };
    std::string content_text_1 = "If you notice the following signs and other uncertain reasons during printing, you may consider performing flowrate calibration:\
            \n1. Over-Extrusion: If you see excess material on your printed object, forming blobs or zits, or the layers seem too thick, it could be a sign of over-extrusion;\
            \n2. Under-Extrusion: This is the opposite of over - extrusion.Signs include missing layers, weak infill, or gaps in the print.This could mean that your printer isn't extruding enough filament;\
            \n3. Poor Surface Quality : If the surface of your prints seems rough or uneven, this could be a result of an incorrect flow rate;\
            \n4. Weak Structural Integrity : If your prints break easily or don't seem as sturdy as they should be, this might be due to under-extrusion or poor layer adhesion, which can be improved by flow rate calibration;\
            \n5. When using third-party filaments";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    add_bitmap(parent, m_top_sizer, "FlowrateCompare",true,290);

    Label* title_2{ nullptr };
    std::string title_text_2 = "Calibration process";
    Label* content_2{ nullptr };
    std::string content_text_2 = "The calibration process includes two types: coarse calibration and fine calibration.\
            \nUsually, we first use coarse calibration to obtain a range, and then perform fine calibration to obtain precise values. You can also directly use the values of coarse calibration.\
            \nBefore calibration, you need to select the printer you are using, the consumables that need to be calibrated, and the process. You can directly select them in the upper left corner of the current page.";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    m_action_panel = new CaliPageActionPanel(parent, CalibMode::Calib_Flow_Rate, CaliPageType::CALI_PAGE_START);

    m_top_sizer->Add(m_action_panel, 0, wxEXPAND, 0);

#ifdef __linux__
    wxGetApp().CallAfter([this, title_1, title_2, content_1, content_2, introduce]() {
        title_1->SetMinSize(title_1->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        title_2->SetMinSize(title_2->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        content_1->SetMinSize(content_1->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        content_2->SetMinSize(content_2->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        introduce->SetMinSize(introduce_text->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        Layout();
        Fit();
        });
#endif
}
//w29

void CalibrationFlowRateStartPage::msw_rescale()
{
    CalibrationWizardPage::msw_rescale();
    if (wxGetApp().app_config->get_language_code() == "zh-cn") {
        create_bitmap(this, "cali_page_flow_introduction_CN");
    } else {
        create_bitmap(this, "cali_page_flow_introduction");
    }
}

CalibrationMaxVolumetricSpeedStartPage::CalibrationMaxVolumetricSpeedStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationStartPage(parent, id, pos, size, style)
{
    m_cali_mode = CalibMode::Calib_Vol_speed_Tower;

    create_page(this);

    this->SetSizer(m_top_sizer);
    Layout();
    m_top_sizer->Fit(this);
}

void CalibrationMaxVolumetricSpeedStartPage::create_page(wxWindow* parent)
{
    //w29
    m_page_caption = new CaliPageCaption(parent, m_cali_mode);
    m_page_caption->show_prev_btn(false);
    m_top_sizer->Add(m_page_caption, 0, wxALIGN_CENTER, 0);

    Label* title_1{ nullptr };
    std::string title_text_1 = "What is Max Volumetric Speed Calibration ?";
    Label* content_1{ nullptr };
    std::string content_text_1 = "Different filaments have different maximum volume speed.\
        \nNozzle material, caliber, printing temperature, etc., will affect the maximum volume speed.\
        \nWhen the maximum volume velocity is set too high and does not match the filament properties, there may be missing threads during the printing process, resulting in a deterioration of the surface texture of the model.\
        \nThis is a test designed to calibrate the maximum volumetric speed of the specific filament. The generic or 3rd party filament types may not have the correct volumetric flow rate set in the filament. This test will help you to find the maximum volumetric speed of the filament.";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    Label* title_2{ nullptr };
    std::string title_text_2 = "When to Calibrate Max Volumetric Speed ?";
    Label* content_2{ nullptr };
    std::string content_text_2 = "We have configured corresponding values for our official consumables in the software. When you have the following situations, you need to calibrate the Max Volumetric Speed:\
        \n1.Use different brands of filaments;\
        \n2.Replaced nozzles with different materials and diameters;\
        \n3.You have changed the printing temperature;\
        \n4.During the printing process, it was found that there were missing threads, insufficient extrusion, or broken filling.\
        \nBefore calibration, you need to select the printer you are using, the consumables that need to be calibrated, and the process. You can directly select them in the upper left corner of the current page.";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    add_bitmap(parent, m_top_sizer, "maxvolumetricspeedmodel", true, 300);
    m_top_sizer->AddSpacer(PRESET_GAP);
    
    m_action_panel = new CaliPageActionPanel(parent, m_cali_mode, CaliPageType::CALI_PAGE_START);

    m_top_sizer->Add(m_action_panel, 0, wxEXPAND, 0);
    CaliPresetTipsstartPanel* m_tips_panel = new CaliPresetTipsstartPanel(parent);
    m_top_sizer->Add(m_tips_panel, 0);
    m_top_sizer->AddSpacer(PRESET_GAP);
#ifdef __linux__
    wxGetApp().CallAfter([this, title_1, title_2, content_1, content_2]() {
        title_1->SetMinSize(title_1->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        title_2->SetMinSize(title_2->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        content_1->SetMinSize(content_1->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        content_2->SetMinSize(content_2->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        Layout();
        Fit();
        });
#endif
}

void CalibrationMaxVolumetricSpeedStartPage::msw_rescale()
{
    CalibrationWizardPage::msw_rescale();
    if (wxGetApp().app_config->get_language_code() == "zh-cn") {
        create_bitmap(this, "cali_page_before_pa_CN", "cali_page_after_pa_CN");
    } else {
        create_bitmap(this, "cali_page_before_pa", "cali_page_after_pa");
    }
}

}}