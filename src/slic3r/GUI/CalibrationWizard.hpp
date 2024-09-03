#ifndef slic3r_GUI_CalibrationWizard_hpp_
#define slic3r_GUI_CalibrationWizard_hpp_

#include "../slic3r/Utils/CalibUtils.hpp"

#include "DeviceManager.hpp"
#include "CalibrationWizardPage.hpp"
#include "CalibrationWizardStartPage.hpp"
#include "CalibrationWizardPresetPage.hpp"

namespace Slic3r { namespace GUI {


class CalibrationWizardPageStep
{
public:
    CalibrationWizardPageStep(CalibrationWizardPage* data) {
        page = data;
    }
    
    CalibrationWizardPageStep* prev { nullptr };
    CalibrationWizardPageStep* next { nullptr };
    CalibrationWizardPage*     page { nullptr };
    void chain(CalibrationWizardPageStep* step) {
        if (!step) return;
        this->next = step;
        step->prev = this;
    }
};

class CalibrationWizard : public wxPanel {
public:
    CalibrationWizard(wxWindow* parent, CalibMode mode,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    ~CalibrationWizard();

    //w29
    CalibrationWizardPageStep* get_curr_step() { return m_curr_step; }

    void show_step(CalibrationWizardPageStep* step);

    virtual void update(MachineObject* obj);
    //w29
    
    CalibMode get_calibration_mode() { return m_mode; }

    //w29
    void msw_rescale();

protected:
    void on_cali_go_home();

protected:
    /* wx widgets*/
    wxScrolledWindow* m_scrolledWindow;
    wxBoxSizer* m_all_pages_sizer;

    CalibMode           m_mode;
    //w29
    CalibrationMethod   m_cali_method { CalibrationMethod::CALI_METHOD_MANUAL };
    MachineObject*      curr_obj { nullptr };
    //w29

    CalibrationWizardPageStep* m_curr_step { nullptr };

    CalibrationWizardPageStep* start_step { nullptr };
    CalibrationWizardPageStep* preset_step { nullptr };
    //w29
    CalibrationWizardPageStep* pa_line{ nullptr };
    CalibrationWizardPageStep* pa_pattern{ nullptr };
    CalibrationWizardPageStep* pa_tower{ nullptr };
    CalibrationWizardPageStep* coarse_calib_step{ nullptr };
    //w29
    CalibrationWizardPageStep* fine_calib_step{ nullptr };

    /* save steps of calibration pages */
    std::vector<CalibrationWizardPageStep*> m_page_steps;

    SecondaryCheckDialog *go_home_dialog = nullptr;
};

class PressureAdvanceWizard : public CalibrationWizard {
public:
    PressureAdvanceWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~PressureAdvanceWizard() {};
    //w29

protected:
    void create_pages();

    void on_cali_start();
    //w29

    void on_cali_action(wxCommandEvent& evt);

    void update(MachineObject* obj) override;
    //w29

    bool                       m_show_result_dialog = false;
    std::vector<PACalibResult> m_calib_results_history;
    int                        cali_version = -1;
};

class FlowRateWizard : public CalibrationWizard {
public:
    FlowRateWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~FlowRateWizard() {};
    //w29

protected:
    void create_pages();

    void on_cali_action(wxCommandEvent& evt);

    //w29
    void cali_start_pass(int road);

    void update(MachineObject* obj) override;
};

class MaxVolumetricSpeedWizard : public CalibrationWizard {
public:
    MaxVolumetricSpeedWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~MaxVolumetricSpeedWizard() {};

protected:
    void create_pages();

    void on_cali_action(wxCommandEvent& evt);

    void on_cali_start();
};

// save printer_type in command event
wxDECLARE_EVENT(EVT_DEVICE_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(EVT_CALIBRATION_JOB_FINISHED, wxCommandEvent);

}} // namespace Slic3r::GUI

#endif
