#ifndef slic3r_GUI_CalibrationWizardStartPage_hpp_
#define slic3r_GUI_CalibrationWizardStartPage_hpp_

#include "CalibrationWizardPage.hpp"

namespace Slic3r { namespace GUI {



class CalibrationStartPage : public CalibrationWizardPage
{
public:
    CalibrationStartPage(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

protected:
    CalibMode m_cali_mode;

    wxBoxSizer*   m_top_sizer{ nullptr };
    wxBoxSizer*   m_images_sizer{ nullptr };
    Label*        m_when_title{ nullptr };
    Label*        m_when_content{ nullptr };
    Label*        m_about_title{ nullptr };
    Label*        m_about_content{ nullptr };
    wxStaticBitmap* m_before_bmp{ nullptr };
    wxStaticBitmap* m_after_bmp{ nullptr };
    wxStaticBitmap* m_bmp_intro{ nullptr };
    //w29

    void create_when(wxWindow* parent, wxString title, wxString content);
    void create_about(wxWindow* parent, wxString title, wxString content);
    void create_bitmap(wxWindow* parent, const wxBitmap& before_img, const wxBitmap& after_img);
    void create_bitmap(wxWindow* parent, std::string before_img, std::string after_img);
    void create_bitmap(wxWindow* parent, std::string img);
    //w29
    void add_bitmap(wxWindow* parent, wxBoxSizer* m_top_sizer, std::string img,bool can_modify = false , int modify_size =350);
    void create_paragraph(wxWindow* parent, Label* title, std::string title_txt, Label* content, std::string content_txt);
    void create_txt(wxWindow* parent, Label* label, std::string label_txt);
};

class CalibrationPAStartPage : public CalibrationStartPage
{
public:
    CalibrationPAStartPage(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);
    //w29
    void msw_rescale() override;
};

class CalibrationFlowRateStartPage : public CalibrationStartPage
{
public:
    CalibrationFlowRateStartPage(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);
    //w29
    void msw_rescale() override;
};

class CalibrationMaxVolumetricSpeedStartPage : public CalibrationStartPage
{
public:
    CalibrationMaxVolumetricSpeedStartPage(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);
    void msw_rescale() override;
};

class CaliPresetTipsstartPanel : public CalibrationStartPage
{
public:
    CaliPresetTipsstartPanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

};

}} // namespace Slic3r::GUI

#endif