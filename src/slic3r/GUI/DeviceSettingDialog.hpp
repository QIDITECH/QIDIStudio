#ifndef _DEVICESETTINGDIALOG_HPP_
#define _DEVICESETTINGDIALOG_HPP_

#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <vector>
#include <string>
#include <wx/simplebook.h>
#include <wx/radiobut.h>
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"

namespace Slic3r{
namespace GUI {

class DevSettingButton : public wxPanel{
public:
    DevSettingButton(wxWindow* parent);

private:

    void update_displayed_bitmap();

    wxStaticBitmap* m_bmp{ nullptr };
    wxBitmap m_img_default;
    wxBitmap m_img_hover;
    bool     m_hover{false};
};

class DeviceSettingItem : public wxPanel{
public:
    using NavigateFn = std::function<void()>;

    DeviceSettingItem(wxWindow* parent, const wxString& label, NavigateFn on_click);
    ~DeviceSettingItem() override;

    void SetValueText(const wxString& value);

private:
    void on_enter(wxMouseEvent& evt);
    void on_leave(wxMouseEvent& evt);
    void on_left_down(wxMouseEvent& evt);

    NavigateFn m_on_click;
    Label* m_label {nullptr};
    Label* m_value {nullptr};
    wxStaticBitmap* m_arrow{nullptr};

    bool m_hover{ false };
};

class DeviceSettingDialog : public wxDialog{
public :
    enum AiMonitorSensitivityLevel {
        LOW         = 0,
        MEDIUM      = 1,
        HIGH        = 2,
        LEVELS_NUM  = 3
    };

    DeviceSettingDialog(wxWindow* parent);
    ~DeviceSettingDialog();

    void init_device_info();
    void creat_ui();
    void ShowPage(int idx);
    std::string sensitivity_level_to_msg_string(enum AiMonitorSensitivityLevel level);
    void update_spaghetti_detection_from_device();
    void apply_print_options_to_device();
    void refresh_print_options_layout();
    
protected:
    enum PageIndex : int
    {
        PAGE_ROOT = 0,
        PAGE_PRINT_OPTIONS,
        PAGE_DEVICE_INFO,
        PAGE_DEVICE_NAME,
        PAGE_COUNT
    };

private:
    wxSimplebook* m_book{ nullptr };

private:
    wxPanel* build_root_page();
    wxPanel* build_header(wxWindow* parent, const wxString& title,
                            bool show_back, bool show_save, bool show_close);
    wxPanel* build_print_options_page();
    wxPanel* build_device_info_page();
    wxPanel* build_device_name_page();
    
    void GoRoot();

    void on_close(wxMouseEvent& evt);
    void on_back(wxMouseEvent& evt);
    void on_save(wxCommandEvent& evt);

    wxColour m_text_fg;

    DeviceSettingItem* m_item_print_options;
    DeviceSettingItem* m_item_device_info;
    DeviceSettingItem* m_item_device_name;
    Label* text_ai_detections;
    Label* text_ai_detections_caption;

    // print options - AI detection
    wxPanel*       ai_refine_panel{ nullptr };
    CheckBox*      m_cb_spaghetti_detection{ nullptr };
    Label*         text_spaghetti_detection{ nullptr };
    Label*         text_spaghetti_detection_caption{ nullptr };
    ComboBox*      spaghetti_detection_level_list{ nullptr };
    wxSizerItem*   m_spaghetti_sensitivity_item{ nullptr };
    wxScrolledWindow* m_print_options_scroll{ nullptr };

    CheckBox*      m_cb_fod_check{ nullptr };
    Label*         text_fod_check{ nullptr };
    Label*         text_fod_check_caption{ nullptr };

    // print options - Other
    CheckBox*      m_rb_extruder_cooler{ nullptr };
    Label*         text_extruder_cooler{ nullptr };
    Label*         text_extruder_cooler_caption{ nullptr };

    wxString m_device_name;
    wxString m_device_type;
    wxString m_device_ip;
    wxString m_device_mac;
    wxString m_device_serial;
    wxString m_device_firmware_version;

    // device name page
    TextInput*  m_device_name_input{ nullptr };
};

}}

#endif  //_DEVICESETTINGDIALOG