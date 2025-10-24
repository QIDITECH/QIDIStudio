#ifndef slic3r_GUI_CalibrationPanel_hpp_
#define slic3r_GUI_CalibrationPanel_hpp_

#include "CalibrationWizard.hpp"
#include "Tabbook.hpp"
//#include "Widgets/SideTools.hpp"

namespace Slic3r { namespace GUI {

#define SELECT_MACHINE_GREY900 wxColour(38, 46, 48)
#define SELECT_MACHINE_GREY600 wxColour(144,144,144)
#define SELECT_MACHINE_GREY400 wxColour(206, 206, 206)
#define SELECT_MACHINE_BRAND wxColour(68, 121, 251)
#define SELECT_MACHINE_REMIND wxColour(255,111,0)
#define SELECT_MACHINE_LIGHT_GREEN wxColour(0,66,255)   // y96

//w29
#define CALI_MODE_COUNT  3


wxString get_calibration_type_name(CalibMode cali_mode);
//w29

class CalibrationPanel : public wxPanel
{
public:
    CalibrationPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~CalibrationPanel();
    Tabbook* get_tabpanel() { return m_tabpanel; };
    //w29
    bool Show(bool show);
    //w29
    void msw_rescale();
    //w29
    TabPresetComboBox* m_printer_choice{ nullptr };
    TabPresetComboBox* m_filament_choice{ nullptr };
    TabPresetComboBox* m_print_choice{ nullptr };
    void create_preset_box(wxWindow* parent, wxBoxSizer* sizer_side_tools);

protected:
    void init_tabpanel();
    void init_timer();
    //w29

    //w29
    bool                    m_initialized { false };
    MachineObject*          obj{ nullptr };
    Tabbook*                m_tabpanel{ nullptr };
    CalibrationWizard*      m_cali_panels[CALI_MODE_COUNT];
    wxTimer*                m_refresh_timer = nullptr;

};
}} // namespace Slic3r::GUI

#endif