#include <regex>
#include "CalibrationWizardPresetPage.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "MsgDialog.hpp"
#include "libslic3r/Print.hpp"
#include "Tab.hpp"

namespace Slic3r { namespace GUI {
static int PA_LINE = 0;
static int PA_PATTERN = 1;
//w29
static int PA_TOWER = 2;

CaliPresetCaliStagePanel::CaliPresetCaliStagePanel(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos,
    const wxSize& size,
    long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);

    m_top_sizer = new wxBoxSizer(wxVERTICAL);

    create_panel(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CaliPresetCaliStagePanel::create_panel(wxWindow* parent)
{
    //w29
    auto title = new Label(parent, _L("Calibration Type"));
    title->SetFont(Label::Head_14);
    title->Hide();
    m_top_sizer->Add(title);
    m_top_sizer->AddSpacer(FromDIP(15));
    m_top_sizer->AddSpacer(FromDIP(10));

    input_panel = new wxPanel(parent);
    input_panel->Hide();
    auto input_sizer = new wxBoxSizer(wxHORIZONTAL);
    input_panel->SetSizer(input_sizer);
    input_sizer->AddSpacer(FromDIP(18));
    m_top_sizer->Add(input_panel);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        SetFocusIgnoringChildren();
        });
}
//w29

CaliPresetWarningPanel::CaliPresetWarningPanel(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos,
    const wxSize& size,
    long style)
    : wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);

    m_top_sizer = new wxBoxSizer(wxHORIZONTAL);

    create_panel(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CaliPresetWarningPanel::create_panel(wxWindow* parent)
{
    m_warning_text = new Label(parent, wxEmptyString);
    m_warning_text->SetFont(Label::Body_13);
    m_warning_text->SetForegroundColour(wxColour(230, 92, 92));
    m_warning_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_top_sizer->Add(m_warning_text, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(5));
}

void CaliPresetWarningPanel::set_warning(wxString text)
{
    m_warning_text->SetLabel(text);
}

CaliPresetCustomRangePanel::CaliPresetCustomRangePanel(
    wxWindow* parent,
    int input_value_nums,
    //w29
    bool scale,
    wxWindowID id,
    const wxPoint& pos,
    const wxSize& size,
    long style)
    : wxPanel(parent, id, pos, size, style)
    , m_input_value_nums(input_value_nums)
{
    SetBackgroundColour(*wxWHITE);

    m_title_texts.resize(input_value_nums);
    m_value_inputs.resize(input_value_nums);

    m_top_sizer = new wxBoxSizer(wxHORIZONTAL);
    //w29
    create_panel(this, scale);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CaliPresetCustomRangePanel::set_unit(wxString unit)
{
    for (size_t i = 0; i < m_input_value_nums; ++i) {
        m_value_inputs[i]->SetLabel(unit);
    }
}

void CaliPresetCustomRangePanel::set_titles(wxArrayString titles)
{
    if (titles.size() != m_input_value_nums)
        return;

    for (size_t i = 0; i < m_input_value_nums; ++i) {
        m_title_texts[i]->SetLabel(titles[i]);
    }
}

void CaliPresetCustomRangePanel::set_values(wxArrayString values) {
    if (values.size() != m_input_value_nums)
        return;

    for (size_t i = 0; i < m_input_value_nums; ++i) {
        m_value_inputs[i]->GetTextCtrl()->SetValue(values[i]);
    }
}

wxArrayString CaliPresetCustomRangePanel::get_values()
{
    wxArrayString result;
    for (size_t i = 0; i < m_input_value_nums; ++i) {
        result.push_back(m_value_inputs[i]->GetTextCtrl()->GetValue());
    }
    return result;
}

//w29
void CaliPresetCustomRangePanel::create_panel(wxWindow* parent , bool scale)
{
    wxBoxSizer* horiz_sizer;
    horiz_sizer = new wxBoxSizer(wxHORIZONTAL);
    for (size_t i = 0; i < m_input_value_nums; ++i) {
        if (i > 0) {
            horiz_sizer->Add(FromDIP(10), 0, 0, wxEXPAND, 0);
        }

        wxBoxSizer *item_sizer;
        item_sizer = new wxBoxSizer(wxVERTICAL);
        m_title_texts[i] = new Label(parent, _L("Title"));
        m_title_texts[i]->Wrap(-1);
        //w29
        m_title_texts[i]->SetFont(::Label::Head_14);
        item_sizer->Add(m_title_texts[i], 0, wxALL, 0);
        //w29
        if (m_input_value_nums == 1 && scale)
            m_value_inputs[i] = new TextInput(parent, wxEmptyString, wxString::FromUTF8("°C"), "", wxDefaultPosition, wxSize(20 * wxGetApp().em_unit(), 30 * wxGetApp().em_unit() / 10), 0);
        else
            m_value_inputs[i] = new TextInput(parent, wxEmptyString, wxString::FromUTF8("°C"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, 0);
        m_value_inputs[i]->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
        m_value_inputs[i]->GetTextCtrl()->Bind(wxEVT_TEXT, [this, i](wxCommandEvent& event) {
            std::string number = m_value_inputs[i]->GetTextCtrl()->GetValue().ToStdString();
            std::string decimal_point;
            std::string expression = "^[-+]?[0-9]+([,.][0-9]+)?$";
            std::regex decimalRegex(expression);
            int decimal_number;
            if (std::regex_match(number, decimalRegex)) {
                std::smatch match;
                if (std::regex_search(number, match, decimalRegex)) {
                    std::string decimalPart = match[1].str();
                    if (decimalPart != "")
                        decimal_number = decimalPart.length() - 1;
                    else
                        decimal_number = 0;
                }
                int max_decimal_length;
                if (i <= 1)
                    max_decimal_length = 3;
                else if (i >= 2)
                    max_decimal_length = 3;
                if (decimal_number > max_decimal_length) {
                    int allowed_length = number.length() - decimal_number + max_decimal_length;
                    number = number.substr(0, allowed_length);
                    m_value_inputs[i]->GetTextCtrl()->SetValue(number);
                    m_value_inputs[i]->GetTextCtrl()->SetInsertionPointEnd();
                }
            }
            // input is not a number, invalid.
            else
                BOOST_LOG_TRIVIAL(trace) << "The K input string is not a valid number when calibrating. ";

            });
        item_sizer->Add(m_value_inputs[i], 0, wxALL, 0);
        horiz_sizer->Add(item_sizer, 0, wxEXPAND, 0);
    }

    m_top_sizer->Add(horiz_sizer, 0, wxEXPAND, 0);
}


CaliPresetTipsPanel::CaliPresetTipsPanel(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos,
    const wxSize& size,
    long style)
    : wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(wxColour(238, 238, 238));
    this->SetMinSize(wxSize(MIN_CALIBRATION_PAGE_WIDTH, -1));
    
    m_top_sizer = new wxBoxSizer(wxVERTICAL);

    create_panel(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CaliPresetTipsPanel::create_panel(wxWindow* parent)
{
    m_top_sizer->AddSpacer(FromDIP(10));

    auto preset_panel_tips = new Label(parent, _L("A test model will be printed. Please clear the build plate and place it back to the hot bed before calibration."));
    preset_panel_tips->SetFont(Label::Body_14);
    preset_panel_tips->Wrap(CALIBRATION_TEXT_MAX_LENGTH * 1.5f);
    m_top_sizer->Add(preset_panel_tips, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    m_top_sizer->AddSpacer(FromDIP(10));

    auto info_sizer = new wxFlexGridSizer(0, 3, 0, FromDIP(10));
    info_sizer->SetFlexibleDirection(wxBOTH);
    info_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    auto nozzle_temp_sizer = new wxBoxSizer(wxVERTICAL);
    auto nozzle_temp_text = new Label(parent, _L("Nozzle temperature"));
    nozzle_temp_text->SetFont(Label::Body_12);
    m_nozzle_temp = new TextInput(parent, wxEmptyString, wxString::FromUTF8("°C"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_READONLY);
    m_nozzle_temp->SetBorderWidth(0);
    nozzle_temp_sizer->Add(nozzle_temp_text, 0, wxALIGN_LEFT);
    nozzle_temp_sizer->Add(m_nozzle_temp, 0, wxEXPAND);
    nozzle_temp_text->Hide();
    m_nozzle_temp->Hide();

    auto bed_temp_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto printing_param_text = new Label(parent, _L("Printing Parameters"));
    printing_param_text->SetFont(Label::Head_12);
    printing_param_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    bed_temp_sizer->Add(printing_param_text, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));
    auto bed_temp_text = new Label(parent, _L("Bed temperature"));
    bed_temp_text->SetFont(Label::Body_12);

    m_bed_temp = new Label(parent, wxString::FromUTF8("- °C"));
    m_bed_temp->SetFont(Label::Body_12);
    bed_temp_sizer->Add(bed_temp_text, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(10));
    bed_temp_sizer->Add(m_bed_temp, 0, wxALIGN_CENTER);

    auto max_flow_sizer = new wxBoxSizer(wxVERTICAL);
    auto max_flow_text = new Label(parent, _L("Max volumetric speed"));
    max_flow_text->SetFont(Label::Body_12);
    m_max_volumetric_speed = new TextInput(parent, wxEmptyString, wxString::FromUTF8("mm³"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_READONLY);
    m_max_volumetric_speed->SetBorderWidth(0);
    max_flow_sizer->Add(max_flow_text, 0, wxALIGN_LEFT);
    max_flow_sizer->Add(m_max_volumetric_speed, 0, wxEXPAND);
    max_flow_text->Hide();
    m_max_volumetric_speed->Hide();

    m_nozzle_temp->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
    m_max_volumetric_speed->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});

    info_sizer->Add(nozzle_temp_sizer);
    info_sizer->Add(bed_temp_sizer);
    info_sizer->Add(max_flow_sizer);
    m_top_sizer->Add(info_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    m_top_sizer->AddSpacer(FromDIP(10));
}

void CaliPresetTipsPanel::set_params(int nozzle_temp, int bed_temp, float max_volumetric)
{
    wxString text_nozzle_temp = wxString::Format("%d", nozzle_temp);
    m_nozzle_temp->GetTextCtrl()->SetValue(text_nozzle_temp);

    std::string bed_temp_text = bed_temp==0 ? "-": std::to_string(bed_temp);
    m_bed_temp->SetLabel(wxString::FromUTF8(bed_temp_text + "°C"));

    wxString flow_val_text = wxString::Format("%0.2f", max_volumetric);
    m_max_volumetric_speed->GetTextCtrl()->SetValue(flow_val_text);
}

void CaliPresetTipsPanel::get_params(int& nozzle_temp, int& bed_temp, float& max_volumetric)
{
    try {
        nozzle_temp = stoi(m_nozzle_temp->GetTextCtrl()->GetValue().ToStdString());
    }
    catch (...) {
        nozzle_temp = 0;
    }
    try {
        bed_temp = stoi(m_bed_temp->GetLabel().ToStdString());
    }
    catch (...) {
        bed_temp = 0;
    }
    try {
        max_volumetric = stof(m_max_volumetric_speed->GetTextCtrl()->GetValue().ToStdString());
    }
    catch (...) {
        max_volumetric = 0.0f;
    }
}

CalibrationPresetPage::CalibrationPresetPage(
    wxWindow* parent,
    CalibMode cali_mode,
    bool custom_range,
    wxWindowID id,
    const wxPoint& pos,
    const wxSize& size,
    long style)
    : CalibrationWizardPage(parent, id, pos, size, style)
    , m_show_custom_range(custom_range)
{
    SetBackgroundColour(*wxWHITE);

    m_cali_mode = cali_mode;
    m_page_type = CaliPageType::CALI_PAGE_PRESET;
    m_cali_filament_mode = CalibrationFilamentMode::CALI_MODEL_SINGLE;
    m_top_sizer = new wxBoxSizer(wxVERTICAL);

    create_page(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CalibrationPresetPage::msw_rescale()
{
    CalibrationWizardPage::msw_rescale();
    //w29
    //m_ams_sync_button->msw_rescale();
    
    for (auto& comboBox : m_filament_comboBox_list) {
        comboBox->msw_rescale();
    }
}
//w29

#define NOZZLE_LIST_COUNT       4
#define NOZZLE_LIST_DEFAULT     1
float nozzle_diameter_list[NOZZLE_LIST_COUNT] = {0.2, 0.4, 0.6, 0.8 };

void CalibrationPresetPage::create_paragraph(wxWindow* parent, Label* title, std::string title_txt, Label* content, std::string content_txt) {
    
    title = new Label(this, _L(title_txt));
    title->SetFont(Label::Head_14);
    title->Wrap(FromDIP(1100));
    title->SetMinSize({ FromDIP(1100), -1 });

    content = new Label(this, _L(content_txt));
    content->SetFont(Label::Body_14);
    content->Wrap(FromDIP(1100));
    content->SetMinSize({ FromDIP(1100), -1 });
    m_top_sizer->Add(title);
    m_top_sizer->Add(content);
#ifdef __linux__
    wxGetApp().CallAfter([this, content, title]() {
        content->SetMinSize(content->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        title->SetMinSize(title->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        Layout();
        Fit();
});
#endif
    m_top_sizer->AddSpacer(PRESET_GAP);
}
void CalibrationPresetPage::create_txt(wxWindow* parent, Label* label, std::string label_txt) {

    label = new Label(parent, _L(label_txt));
    label->SetFont(Label::Body_14);
    label->Wrap(FromDIP(1100));
    label->SetMinSize({ FromDIP(1100), -1 });
    m_top_sizer->Add(label);
#ifdef __linux__
    wxGetApp().CallAfter([this, label]() {
        label->SetMinSize(label->GetSize() + wxSize{ 0, wxWindow::GetCharHeight() });
        Layout();
        Fit();
});
#endif
    m_top_sizer->AddSpacer(PRESET_GAP);
}

//w29
void CalibrationPresetPage::add_bitmap(wxWindow* parent, wxBoxSizer* m_top_sizer, std::string img,const bool custom_cut , const int px_cnt) {
    wxBitmap before_bmp;// = create_scaled_bitmap(img, this, 350);
    if (custom_cut) 
        before_bmp = create_scaled_bitmap(img, this, px_cnt);
    else
        before_bmp = create_scaled_bitmap(img, this, 350);

    wxStaticBitmap* m_bmp_intro_temp{ nullptr };
    wxBoxSizer* m_images_sizer_temp{ nullptr };
    if (!m_bmp_intro_temp)
        m_bmp_intro_temp = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_bmp_intro_temp->SetBitmap(before_bmp);
    m_images_sizer_temp = new wxBoxSizer(wxHORIZONTAL);
    m_images_sizer_temp->Add(m_bmp_intro_temp, 0, wxALL, 0);

    m_top_sizer->Add(m_images_sizer_temp, 0, wxALIGN_CENTER, 0);
    m_bmp_intro_temp = { nullptr };
    m_images_sizer_temp = { nullptr };
}

//w29
void CalibrationPresetPage::create_gif_images(wxWindow* parent, wxBoxSizer* m_top_sizer, const std::string image) {
    wxAnimation animation;
    if (animation.LoadFile(Slic3r::resources_dir() + "/images/" + image + ".gif")) {
        wxAnimationCtrl* gifCtrl = new wxAnimationCtrl(parent, wxID_ANY, animation);
        gifCtrl->Play();

        m_top_sizer->Add(gifCtrl, 0, wxALIGN_CENTER | wxALL, 10);
    }
}

//w29
void CalibrationPresetPage::create_page_flow_coarse(wxWindow* parent, wxBoxSizer* m_top_sizer) {

    Label* title_1{ nullptr };
    std::string title_text_1 = "Step 1";
    Label* content_1{ nullptr };
    std::string content_text_1 = "You only need to click the \"Calibrate\" button below and wait for a short time.After successful slicing, you have three ways to print:\
\n1. Directly send the sliced file and print it;\
\n2. Send the sliced file to the printer via the network and manually select the sliced file for printing;\
\n3. Send the sliced file to a storage medium and print it through the storage medium;\
\nAfter successful printing, you will receive the model as shown in the picture. Choose the number with the smoothest surface; \
\nThe value of the number \"0\" in the figure has the smoothest surface, so the value obtained from coarse calibration is \"1 + 0.00 = 1\", which can be used as the intermediate value for fine calibration.";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    add_bitmap(parent, m_top_sizer, "flowcoarseresult", true, 400);
    m_top_sizer->AddSpacer(PRESET_GAP);

    m_custom_range_panel = new CaliPresetCustomRangePanel(parent, 1);
    m_top_sizer->Add(m_custom_range_panel, 0, wxALIGN_CENTER);

    Label* title_2{ nullptr };
    std::string title_text_2 = "Step 2";
    Label* content_2{ nullptr };
    std::string content_text_2 = "You can also directly apply this value to your printing configuration, return to the \"Prepare\" interface, enter the filaments parameters to make modifications, and then click the save button to save your configuration.";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    create_gif_images(parent, m_top_sizer, "flowratiocoarseset"); 
}

//w29
void CalibrationPresetPage::create_page_flow_fine(wxWindow* parent, wxBoxSizer* m_top_sizer) {

    Label* title_1{ nullptr };
    std::string title_text_1 = "Step 1";
    Label* content_1{ nullptr };
    std::string content_text_1 = "After passing the \"coarse calibration\", the intermediate value \"1\" was obtained. Enter this value into the text box below and follow the steps in the \"coarse calibration\" to print.";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    m_custom_range_panel = new CaliPresetCustomRangePanel(parent, 1,true);
    m_top_sizer->Add(m_custom_range_panel, 0, wxALIGN_CENTER);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* title_2{ nullptr };
    std::string title_text_2 = "Step 2";
    Label* content_2{ nullptr };
    std::string content_text_2 = "After printing, select a number with the smoothest and smoothest surface, as shown in the figure below as \" - 1\". The optimal flow rate for obtaining current filaments is \"1.00 - 0.01 = 0.99\".";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    add_bitmap(parent, m_top_sizer, "flowfineresult", true, 430);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* title_3{ nullptr };
    std::string title_text_3 = "Step 3";
    Label* content_3{ nullptr };
    std::string content_text_3 = "Fill in the value obtained in step 2 into the \"Flow ratio\" in the filaments settings, and you have completed the flow calibration here";
    create_paragraph(parent, title_3, title_text_3, content_3, content_text_3);

    create_gif_images(parent, m_top_sizer, "flowratioset");
}

//w29
void CalibrationPresetPage::create_page_pa_line(wxWindow* parent, wxBoxSizer* m_top_sizer) {

    Label* title_1{ nullptr };
    std::string title_text_1 = "Step 1";
    Label* content_1{ nullptr };
    std::string content_text_1 = "Enter the minimum pressure advance value, maximum pressure advance value, and step size at the bottom of the current page, click the \"Calibrate\" button at the bottom of the page, and wait for a little time. The software will automatically set the calibration configuration.";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    m_custom_range_panel = new CaliPresetCustomRangePanel(parent);
    m_top_sizer->Add(m_custom_range_panel, 0, wxALIGN_CENTER);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* introduce_1{ nullptr };
    std::string introduce_text_1 = "After successful slicing, you have three methods to perform the operation:\
\n1. Directly send the sliced file and print it;\
\n2. Send the sliced file to the printer via the network and manually select the sliced file for printing;\
\n3. Send the sliced file to a storage medium and print it through the storage medium.\
\nReferring to this process, you will print the calibration model as shown in the following figure.";
    create_txt(parent, introduce_1, introduce_text_1);

    add_bitmap(parent, m_top_sizer, "PressureAdvanceLine", true, 400);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* title_2{ nullptr };
    std::string title_text_2 = "Step 2";
    Label* content_2{ nullptr };
    std::string content_text_2 = "After printing is completed, select the smoothest line, enter its corresponding value into the software and save it.";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    create_gif_images(parent, m_top_sizer, "pavalue03");   
}

//w29
void CalibrationPresetPage::create_page_pa_pattern(wxWindow* parent, wxBoxSizer* m_top_sizer) {
    
    Label* title_1{ nullptr };
    std::string title_text_1 = "Step 1";
    Label* content_1{ nullptr };
    std::string content_text_1 = "Enter the minimum pressure advance value, maximum pressure advance value, and step size at the bottom of the current page, click the \"Calibrate\" button at the bottom of the page, and wait for a little time. The software will automatically set the calibration configuration.";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    m_custom_range_panel = new CaliPresetCustomRangePanel(parent);
    m_top_sizer->Add(m_custom_range_panel, 0, wxALIGN_CENTER);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* introduce_1{ nullptr };
    std::string introduce_text_1 = "After successful slicing, you have three methods to perform the operation:\
\n1. Directly send the sliced file and print it;\
\n2. Send the sliced file to the printer via the network and manually select the sliced file for printing;\
\n3. Send the sliced file to a storage medium and print it through the storage medium.\
\nReferring to this process, you will print the calibration model as shown in the following figure.";
    create_txt(parent, introduce_1, introduce_text_1);

    add_bitmap(parent, m_top_sizer, "PressureAdvancePattern", true, 350);
    m_top_sizer->AddSpacer(PRESET_GAP);
    
    Label* title_2{ nullptr };
    std::string title_text_2 = "Step 2";
    Label* content_2{ nullptr };
    std::string content_text_2 = "There are three feature regions in this model that need to be observed:\
\n1. In regions 1 and 3 of the figure, when the pressure advance value is too small, material stacking will occur and the endpoints will exceed the bounding box. When the pressure advance value is too high, there may be a shortage of wire and the endpoint has not reached the bounding box.\
\n2. In region 2 of the figure, when the pressure advance value is too small, material stacking may occur, which can cause excessive overflow at corners during actual printing. When the pressure value is too high, there may be missing threads. In actual printing, it can cause corners to become rounded and lead to missing threads.\
\nFinally, save the value with the best surface effect.";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    create_gif_images(parent, m_top_sizer, "pavalue03");
}

//w29
void CalibrationPresetPage::create_page_pa_tower(wxWindow* parent, wxBoxSizer* m_top_sizer) {

    Label* title_1{ nullptr };
    std::string title_text_1 = "Step 1";
    Label* content_1{ nullptr };
    std::string content_text_1 = "Enter the minimum pressure advance value, maximum pressure advance value, and step size at the bottom of the current page, click the \"Calibrate\" button at the bottom of the page, and wait for a little time. The software will automatically set the calibration configuration.";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    m_custom_range_panel = new CaliPresetCustomRangePanel(parent);
    m_top_sizer->Add(m_custom_range_panel, 0, wxALIGN_CENTER);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* introduce_1{ nullptr };
    std::string introduce_text_1 = "After successful slicing, you have three methods to perform the operation:\
\n1. Directly send the sliced file and print it;\
\n2. Send the sliced file to the printer via the network and manually select the sliced file for printing;\
\n3. Send the sliced file to a storage medium and print it through the storage medium.\
\nReferring to this process, you will print the calibration model as shown in the following figure.";
    create_txt(parent, introduce_1, introduce_text_1);

    add_bitmap(parent, m_top_sizer, "patowermodel", true, 350);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* title_2{ nullptr };
    std::string title_text_2 = "Step 2";
    Label* content_2{ nullptr };
    std::string content_text_2 = "Observe each corner of the model and calibrate it. The pressure advance increases by a step value gradient with every 5mm increase in height. If the pressure advance value is too small, there will be excessive extrusion at the corner. If the pressure advance value is too large, there will be a right angle becoming rounded or even missing threads at the corner. Determine the optimal position for the effect and use a scale to determine the height.";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    Label* title_3{ nullptr };
    std::string title_text_3 = "Step 3";
    Label* content_3{ nullptr };
    std::string content_text_3 = "Calculate the optimal pressure advance value using the given formula:\
\nPressure Advance = k_Start + floor(height ÷ 5) × k_Step\
\nNOTICE: floor() represents rounding downwards\
\nAccording to the measured values, the pressure advance value in the figure is : 0.00 + floor(22.7 ÷ 5) × 0.005 = 0.02\
\nFinally, save the value with the best surface effect.";
    create_paragraph(parent, title_3, title_text_3, content_3, content_text_3);

    create_gif_images(parent, m_top_sizer, "pavalue02");
}

void CalibrationPresetPage::create_page_max_volumetric_speed(wxWindow* parent, wxBoxSizer* m_top_sizer) {

    Label* title_1{ nullptr };
    std::string title_text_1 = "Step 1";
    Label* content_1{ nullptr };
    std::string content_text_1 = "Enter the minimum volumetric speed value, maximum volumetric speed value, and step size at the bottom of the current page, click the \"Calibrate\" button at the bottom of the page, and wait for a little time. The software will automatically set the calibration configuration.";
    create_paragraph(parent, title_1, title_text_1, content_1, content_text_1);

    m_custom_range_panel = new CaliPresetCustomRangePanel(parent);
    m_top_sizer->Add(m_custom_range_panel, 0, wxALIGN_CENTER);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* introduce_1{ nullptr };
    std::string introduce_text_1 = "After successful slicing, you have three methods to perform the operation:\
\n1. Directly send the sliced file and print it;\
\n2. Send the sliced file to the printer via the network and manually select the sliced file for printing;\
\n3. Send the sliced file to a storage medium and print it through the storage medium.\
\nReferring to this process, you will print the calibration model as shown in the following figure.";
    create_txt(parent, introduce_1, introduce_text_1);

    add_bitmap(parent, m_top_sizer, "Volumetricspeedmodel", true, 350);
    m_top_sizer->AddSpacer(PRESET_GAP);

    Label* title_2{ nullptr };
    std::string title_text_2 = "Step 2";
    Label* content_2{ nullptr };
    std::string content_text_2 = "It can be observed that at a certain height, the model begins to show missing fibers. There are two methods to measure the maximum volumetric velocity:\
\n1. Observing the number of notches nums on the right side, you can use StartV + (step * 2) = Max Volumetric Speed.\
\n2. In the \"Preview\" interface, view the Gcode of the model, find the \"Flow\" value corresponding to the missing part, and save it.";
    create_paragraph(parent, title_2, title_text_2, content_2, content_text_2);

    create_gif_images(parent, m_top_sizer, "Volumetricspeedset");
}


void CalibrationPresetPage::create_page(wxWindow* parent)
{
    m_page_caption = new CaliPageCaption(parent, m_cali_mode);
    m_page_caption->show_prev_btn(true);
    m_top_sizer->Add(m_page_caption, 0, wxEXPAND, 0);

    if (m_cali_mode == CalibMode::Calib_Flow_Rate) {
        wxArrayString steps;
        steps.Add(_L("Preset"));
        steps.Add(_L("Calibration1"));
        steps.Add(_L("Calibration2"));
        steps.Add(_L("Record Factor"));
        m_step_panel = new CaliPageStepGuide(parent, steps);
        m_step_panel->set_steps(0);
    }
    else {
        wxArrayString steps;
        steps.Add(_L("Preset"));
        steps.Add(_L("Calibration"));
        //w29
        //steps.Add(_L("Record Factor"));
        m_step_panel = new CaliPageStepGuide(parent, steps);
        m_step_panel->set_steps(0);
    }
    if (m_step_panel)
        m_step_panel->Show(false);
    
    //w29
    if (m_cali_mode == CalibMode::Calib_Flow_Rate_Coarse)
        create_page_flow_coarse(parent, m_top_sizer);
    if (m_cali_mode == CalibMode::Calib_Flow_Rate_Fine)
        create_page_flow_fine(parent, m_top_sizer);
    if (m_cali_mode == CalibMode::Calib_PA_Line)
        create_page_pa_line(parent, m_top_sizer);
    if (m_cali_mode == CalibMode::Calib_PA_Pattern)
        create_page_pa_pattern(parent, m_top_sizer);
    if (m_cali_mode == CalibMode::Calib_PA_Tower)
        create_page_pa_tower(parent, m_top_sizer);
    if (m_cali_mode == CalibMode::Calib_Vol_speed_Tower)
        create_page_max_volumetric_speed(parent, m_top_sizer);

    m_top_sizer->AddSpacer(PRESET_GAP);
    m_action_panel = new CaliPageActionPanel(parent, m_cali_mode, CaliPageType::CALI_PAGE_PRESET);
    m_top_sizer->Add(m_action_panel, 0, wxEXPAND, 0);
    m_top_sizer->Add(m_step_panel, 0, wxEXPAND, 0);

    m_cali_stage_panel = new CaliPresetCaliStagePanel(parent);
    m_cali_stage_panel->set_parent(this);
    m_top_sizer->Add(m_cali_stage_panel, 0);

    //w29
    m_warning_panel = new CaliPresetWarningPanel(parent);
    m_tips_panel = new CaliPresetTipsPanel(parent);
    m_tips_panel->Hide();

    m_statictext_printer_msg = new Label(this, wxEmptyString, wxALIGN_CENTER_HORIZONTAL);
    m_statictext_printer_msg->SetFont(::Label::Body_13);
    m_statictext_printer_msg->Hide();
}

wxString CalibrationPresetPage::format_text(wxString& m_msg)
{
    if (wxGetApp().app_config->get("language") != "zh_CN") { return m_msg; }

    wxString out_txt = m_msg;
    wxString count_txt = "";
    int      new_line_pos = 0;

    for (int i = 0; i < m_msg.length(); i++) {
        auto text_size = m_statictext_printer_msg->GetTextExtent(count_txt);
        if (text_size.x < (FromDIP(600))) {
            count_txt += m_msg[i];
        }
        else {
            out_txt.insert(i - 1, '\n');
            count_txt = "";
        }
    }
    return out_txt;
}

void CalibrationPresetPage::stripWhiteSpace(std::string& str)
{
    if (str == "") { return; }

    string::iterator cur_it;
    cur_it = str.begin();

    while (cur_it != str.end()) {
        if ((*cur_it) == '\n' || (*cur_it) == ' ') {
            cur_it = str.erase(cur_it);
        }
        else {
            cur_it++;
        }
    }
}

//w29
void CalibrationPresetPage::update(MachineObject* obj)
{
    curr_obj = obj;
}

void CalibrationPresetPage::set_cali_filament_mode(CalibrationFilamentMode mode)
{
    CalibrationWizardPage::set_cali_filament_mode(mode);

    for (int i = 0; i < m_filament_comboBox_list.size(); i++) {
        m_filament_comboBox_list[i]->set_select_mode(mode);
    }
}

void CalibrationPresetPage::set_cali_method(CalibrationMethod method)
{
    
    if (method == CalibrationMethod::CALI_METHOD_MANUAL) {
         if (m_cali_mode == CalibMode::Calib_PA_Line || m_cali_mode == CalibMode::Calib_PA_Pattern || m_cali_mode == CalibMode::Calib_PA_Tower) {
            if (m_cali_stage_panel)
                m_cali_stage_panel->Show(false);

            if (m_custom_range_panel) {
                wxArrayString titles;
                titles.push_back(_L("From k Value"));
                titles.push_back(_L("To k Value"));
                titles.push_back(_L("Value step"));
                m_custom_range_panel->set_titles(titles);

                wxArrayString values;
                {
                    values.push_back(wxString::Format(wxT("%.0f"), 0));
                    values.push_back(wxString::Format(wxT("%.2f"), 0.04));
                    values.push_back(wxString::Format(wxT("%.3f"), 0.002));
                }
                m_custom_range_panel->set_values(values);

                m_custom_range_panel->set_unit("");
                m_custom_range_panel->Show();
            }
        }
         //w29
        else if ( m_cali_mode == CalibMode::Calib_Flow_Rate_Fine || m_cali_mode == CalibMode::Calib_Flow_Rate) {
             if (m_cali_stage_panel)
                 m_cali_stage_panel->Hide();

            if (m_custom_range_panel) {
                wxArrayString titles;
                titles.push_back(_L("Flow Ratio"));
                m_custom_range_panel->set_titles(titles);

                wxArrayString values;
                
                //w29
                values.push_back(wxString::Format(wxT("%.0f"), 1.0));
                
                m_custom_range_panel->set_values(values);

                m_custom_range_panel->set_unit("");
                m_custom_range_panel->Show();
            }
        }
        else if (m_cali_mode == CalibMode::Calib_Flow_Rate_Coarse) {
             if (m_cali_stage_panel)
                 m_cali_stage_panel->Show(false);

             if (m_custom_range_panel) {
                 wxArrayString values;
                 values.push_back(wxString::Format(wxT("%.0f"), 1.0));
                 m_custom_range_panel->set_values(values);
                 m_custom_range_panel->Show(false);
             }
         }

    }
    else {
        wxArrayString steps;
        steps.Add(_L("Preset"));
        steps.Add(_L("Calibration"));
        steps.Add(_L("Record Factor"));
        m_step_panel->set_steps_string(steps);
        m_step_panel->set_steps(0);
        if (m_cali_stage_panel)
            m_cali_stage_panel->Show(false);
        if (m_custom_range_panel)
            m_custom_range_panel->Show(false);
    }
}
//w29


wxArrayString CalibrationPresetPage::get_custom_range_values()
{
    if (m_custom_range_panel) {
        return m_custom_range_panel->get_values();
    }
    return wxArrayString();
}
//w29


MaxVolumetricSpeedPresetPage::MaxVolumetricSpeedPresetPage(
    wxWindow *parent, CalibMode cali_mode, bool custom_range, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    : CalibrationPresetPage(parent, cali_mode, custom_range, id, pos, size, style)
{
    if (custom_range && m_custom_range_panel) {
        wxArrayString titles;
        titles.push_back(_L("From Volumetric Speed"));
        titles.push_back(_L("To Volumetric Speed"));
        titles.push_back(_L("Step"));

        //w29
        wxArrayString values;
        values.push_back(wxString::Format(wxT("%.1f"), 5.0));
        values.push_back(wxString::Format(wxT("%.1f"), 15.0));
        values.push_back(wxString::Format(wxT("%.1f"), 0.1));
        
        m_custom_range_panel->set_titles(titles);
        m_custom_range_panel->set_values(values);
        //w29
        m_custom_range_panel->set_unit("");
    }
}
}}
