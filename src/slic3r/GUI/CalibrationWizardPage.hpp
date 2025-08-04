#ifndef slic3r_GUI_CalibrationWizardPage_hpp_
#define slic3r_GUI_CalibrationWizardPage_hpp_

#include "wx/event.h"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/AMSControl.hpp"
#include "Widgets/ProgressBar.hpp"
#include "wxExtensions.hpp"
#include "PresetComboBoxes.hpp"

#include "../slic3r/Utils/CalibUtils.hpp"
#include "../../libslic3r/Calib.hpp"

namespace Slic3r { namespace GUI {


#define MIN_CALIBRATION_PAGE_WIDTH         FromDIP(1100)
#define PRESET_GAP                         FromDIP(25)
#define CALIBRATION_COMBOX_SIZE            wxSize(FromDIP(500), FromDIP(24))
#define CALIBRATION_FILAMENT_COMBOX_SIZE   wxSize(FromDIP(250), FromDIP(24))
#define CALIBRATION_OPTIMAL_INPUT_SIZE     wxSize(FromDIP(300), FromDIP(24))
#define CALIBRATION_FROM_TO_INPUT_SIZE     wxSize(FromDIP(160), FromDIP(24))
#define CALIBRATION_FGSIZER_HGAP           FromDIP(50)
#define CALIBRATION_TEXT_MAX_LENGTH        FromDIP(90) + CALIBRATION_FGSIZER_HGAP + 2 * CALIBRATION_FILAMENT_COMBOX_SIZE.x
#define CALIBRATION_PROGRESSBAR_LENGTH     FromDIP(690)

class CalibrationWizard;

//w29
wxString get_cali_mode_caption_string(CalibMode mode);

enum CalibrationFilamentMode {
    /* calibration single filament at once */
    CALI_MODEL_SINGLE = 0,
    /* calibration multi filament at once */
    CALI_MODEL_MULITI,
};

enum CalibrationMethod {
    CALI_METHOD_MANUAL = 0,
    CALI_METHOD_AUTO,
    CALI_METHOD_NONE,
};

//w29
CalibrationFilamentMode get_cali_filament_mode(MachineObject* obj, CalibMode mode);

CalibMode get_obj_calibration_mode(const MachineObject* obj);

CalibMode get_obj_calibration_mode(const MachineObject* obj, int& cali_stage);

CalibMode get_obj_calibration_mode(const MachineObject* obj, CalibrationMethod& method, int& cali_stage);

//w29
enum class CaliPageType {
    CALI_PAGE_START = 0,
    CALI_PAGE_PRESET,
    CALI_PAGE_CALI
};

class FilamentComboBox : public wxPanel
{
public:
    FilamentComboBox(wxWindow* parent, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~FilamentComboBox() {};

    void set_select_mode(CalibrationFilamentMode mode);
    //w29
    virtual bool Show(bool show = true);
    virtual bool Enable(bool enable);
    virtual void SetValue(bool value, bool send_event = true);
    void msw_rescale();

protected:
    //w29
    CheckBox* m_checkBox{ nullptr };
    wxRadioButton* m_radioBox{ nullptr };
    CalibrateFilamentComboBox* m_comboBox{ nullptr };
    CalibrationFilamentMode m_mode { CalibrationFilamentMode::CALI_MODEL_SINGLE };
};

typedef std::vector<FilamentComboBox*> FilamentComboBoxList;

class CaliPageCaption : public wxPanel
{
public:
    CaliPageCaption(wxWindow* parent,
        CalibMode cali_mode,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void show_prev_btn(bool show = true);
    //w29
    void msw_rescale();

protected:
    ScalableButton* m_prev_btn;
    //w29

private:
    void init_bitmaps();
    //w29

    ScalableBitmap m_prev_bmp_normal;
    ScalableBitmap m_prev_bmp_hover;
    ScalableBitmap m_help_bmp_normal;
    ScalableBitmap m_help_bmp_hover;
};

class CaliPageStepGuide : public wxPanel
{
public:
    CaliPageStepGuide(wxWindow* parent,
        wxArrayString steps,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void set_steps(int index);
    void set_steps_string(wxArrayString steps);
protected:
    wxArrayString m_steps;
    wxBoxSizer* m_step_sizer;
    std::vector<Label*> m_text_steps;
};

class CaliPagePicture : public wxPanel 
{
public:
    CaliPagePicture(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void set_bmp(const ScalableBitmap& bmp);
    //w29
    void msw_rescale();

protected:
    ScalableBitmap m_bmp;
    wxStaticBitmap* m_img;
};

//w29
enum class CaliPageActionType : int
{
    CALI_ACTION_START = 0,
    CALI_ACTION_CALI,
    CALI_ACTION_PREV,
    CALI_ACTION_NEXT,
    CALI_ACTION_CALI_NEXT,
    CALI_ACTION_GO_HOME,
    //w29
    CALI_ACTION_FLOW_COARSE,
    CALI_ACTION_FLOW_FINE,
    CALI_ACTION_PA_LINE,
    CALI_ACTION_PA_PATTERN,
    CALI_ACTION_PA_TOWER
};

class CaliPageButton : public Button
{
public:
    CaliPageButton(wxWindow* parent, CaliPageActionType type, wxString text = wxEmptyString);

    CaliPageActionType get_action_type() { return m_action_type; }

    void msw_rescale();

private:
    CaliPageActionType m_action_type;
};

//w29
class CaliPageActionPanel : public wxPanel
{
public:
    CaliPageActionPanel(wxWindow* parent,
        CalibMode cali_mode,
        CaliPageType page_type,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void bind_button(CaliPageActionType action_type, bool is_block);
    void show_button(CaliPageActionType action_type, bool show = true);
    void enable_button(CaliPageActionType action_type, bool enable = true);
    void msw_rescale();

protected:
    std::vector<CaliPageButton*> m_action_btns;
};

class CalibrationWizardPage : public wxPanel 
{
public:
    CalibrationWizardPage(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~CalibrationWizardPage() {};

    CaliPageType get_page_type() { return m_page_type; }

    CalibrationWizardPage* get_prev_page() { return m_prev_page; }
    CalibrationWizardPage* get_next_page() { return m_next_page; }
    void set_prev_page(CalibrationWizardPage* prev) { m_prev_page = prev; }
    void set_next_page(CalibrationWizardPage* next) { m_next_page = next; }
    CalibrationWizardPage* chain(CalibrationWizardPage* next)
    {
        set_next_page(next);
        next->set_prev_page(this);
        return next;
    }

    virtual void update(MachineObject* obj) { curr_obj = obj; }
    //w29

    virtual void set_cali_filament_mode(CalibrationFilamentMode mode) {
        m_cali_filament_mode = mode;
    }

    virtual void set_cali_method(CalibrationMethod method) {
        m_cali_method = method;
    }

    virtual void msw_rescale();
    //w29

protected:
    CalibMode             m_cali_mode;
    CaliPageType          m_page_type;
    CalibrationFilamentMode  m_cali_filament_mode;
    CalibrationMethod     m_cali_method{ CalibrationMethod::CALI_METHOD_MANUAL };
    MachineObject*        curr_obj { nullptr };

    wxWindow*             m_parent { nullptr };
    CaliPageCaption*      m_page_caption { nullptr };
    CaliPageActionPanel*  m_action_panel { nullptr };
    Label*         m_statictext_printer_msg{ nullptr };

private:
    CalibrationWizardPage* m_prev_page {nullptr};
    CalibrationWizardPage* m_next_page {nullptr};
};

wxDECLARE_EVENT(EVT_CALI_ACTION, wxCommandEvent);
wxDECLARE_EVENT(EVT_CALI_TRAY_CHANGED, wxCommandEvent);


}} // namespace Slic3r::GUI

#endif