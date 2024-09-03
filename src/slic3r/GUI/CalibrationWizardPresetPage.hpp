#ifndef slic3r_GUI_CalibrationWizardPresetPage_hpp_
#define slic3r_GUI_CalibrationWizardPresetPage_hpp_

#include "CalibrationWizardPage.hpp"

namespace Slic3r { namespace GUI {

//w29
class CalibrationPresetPage;

class CaliPresetCaliStagePanel : public wxPanel
{
public:
    CaliPresetCaliStagePanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);
    void create_panel(wxWindow* parent);
    //w29
    void set_parent(CalibrationPresetPage* parent) { m_stage_panel_parent = parent; }
    //w29
protected:
    //w29
    wxBoxSizer*   m_top_sizer;
    wxPanel*       input_panel;
    CalibrationPresetPage* m_stage_panel_parent;
};

//w29
class CaliPresetWarningPanel : public wxPanel
{
public:
    CaliPresetWarningPanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

    void set_warning(wxString text);
protected:
    wxBoxSizer*   m_top_sizer;
    Label* m_warning_text;
};

class CaliPresetTipsPanel : public wxPanel
{
public:
    CaliPresetTipsPanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

    void set_params(int nozzle_temp, int bed_temp, float max_volumetric);
    void get_params(int& nozzle_temp, int& bed_temp, float& max_volumetric);
protected:
    wxBoxSizer*     m_top_sizer;
    TextInput*      m_nozzle_temp;
    Label*   m_bed_temp;
    TextInput*      m_max_volumetric_speed;
};

class CaliPresetCustomRangePanel : public wxPanel
{
public:
    CaliPresetCustomRangePanel(wxWindow* parent,
        int input_value_nums = 3,
        //w29
        bool scale = false,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    //w29
    void create_panel(wxWindow* parent , bool scale = false);

    void set_unit(wxString unit);
    void set_titles(wxArrayString titles);
    void set_values(wxArrayString values);
    wxArrayString get_values();

protected:
    wxBoxSizer*     m_top_sizer;
    int                       m_input_value_nums;
    std::vector<Label*> m_title_texts;
    std::vector<TextInput*>    m_value_inputs;
};

//w29
class CalibrationPresetPage : public CalibrationWizardPage
{
public:
    CalibrationPresetPage(wxWindow* parent,
        CalibMode cali_mode,
        bool custom_range = false,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    //w29
    void create_page(wxWindow* parent);
    wxString format_text(wxString& m_msg);
    void stripWhiteSpace(std::string& str);
    void update(MachineObject* obj) override;

    void set_cali_filament_mode(CalibrationFilamentMode mode) override;

    void set_cali_method(CalibrationMethod method) override;

    wxArrayString get_custom_range_values();

    void msw_rescale() override;

    //w29
    void create_paph(wxWindow* parent, wxString title, wxString content)
    {
        m_txt_title = new Label(this, title);
        m_txt_title->SetFont(Label::Head_14);
        m_txt_title->Wrap(FromDIP(1000));
        m_txt_title->SetMinSize({ FromDIP(1000), -1 });

        m_txt_content = new Label(this, content);;
        m_txt_content->SetFont(Label::Body_14);
        m_txt_content->Wrap(FromDIP(1000));
        m_txt_content->SetMinSize({ FromDIP(1000), -1 });
    }
    void create_page_flow_coarse(wxWindow* parent, wxBoxSizer* m_top_sizer);
    void create_page_flow_fine(wxWindow* parent, wxBoxSizer* m_top_sizer);
    void create_page_pa_line(wxWindow* parent, wxBoxSizer* m_top_sizer);
    void create_page_pa_pattern(wxWindow* parent, wxBoxSizer* m_top_sizer);
    void create_page_pa_tower(wxWindow* parent, wxBoxSizer* m_top_sizer);
    void create_page_max_volumetric_speed(wxWindow* parent, wxBoxSizer* m_top_sizer);
    void add_bitmap(wxWindow* parent, wxBoxSizer* m_top_sizer, std::string img, const bool custom_cut = false, const int px_cnt = 350);
    void create_gif_images(wxWindow* parent, wxBoxSizer* m_top_sizer,const std::string image);

protected:
    //w29

    CaliPageStepGuide* m_step_panel{ nullptr };
    CaliPresetCaliStagePanel* m_cali_stage_panel { nullptr };
    CaliPresetWarningPanel*   m_warning_panel { nullptr };
    CaliPresetCustomRangePanel* m_custom_range_panel { nullptr };
    CaliPresetTipsPanel*      m_tips_panel { nullptr };

    wxBoxSizer* m_top_sizer;
    
    //w29
    FilamentComboBoxList m_filament_comboBox_list;

    bool m_show_custom_range { false };
    MachineObject* curr_obj { nullptr };

    //w29
    Label* m_txt_title{ nullptr };
    Label* m_txt_content{ nullptr };
};

class MaxVolumetricSpeedPresetPage : public CalibrationPresetPage
{
public:
    MaxVolumetricSpeedPresetPage(wxWindow *     parent,
                                 CalibMode      cali_mode,
                                 bool           custom_range = false,
                                 wxWindowID     id           = wxID_ANY,
                                 const wxPoint &pos          = wxDefaultPosition,
                                 const wxSize & size         = wxDefaultSize,
                                 long           style        = wxTAB_TRAVERSAL);
};

}} // namespace Slic3r::GUI

#endif
