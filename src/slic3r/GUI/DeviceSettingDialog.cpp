#include "DeviceSettingDialog.hpp"

#include "QDSDeviceManager.hpp"
#include "PrinterTaskDispatcher.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/StateColor.hpp"
#include "wxExtensions.hpp"
#include "nlohmann/json.hpp"
#include "slic3r/Utils/Http.hpp"
#include <thread>
#include "GUI.hpp"

#if QDT_RELEASE_TO_PUBLIC
#include "../QIDI/QIDINetwork.hpp"
#endif

using namespace nlohmann;

namespace Slic3r{
namespace GUI{

const int DialogWidth   = 612;
const int DialogHeight  = 673;
const int ItemHeight    = 72;

// 颜色
const wxColour DialogBGColour     (0xFF, 0xFF, 0xFF);
const wxColour DialogMainBGColour (0xEE, 0xEE, 0xEE);
const wxColour DialogBgRowHover   (0xF7, 0xF7, 0xF7);
const wxColour DialogTextPrimary  (0x33, 0x33, 0x33);
const wxColour DialogLine         (0xC4, 0xC4, 0xC4);
const wxColour DialogLine_2       (0xDF, 0xDF, 0xDF);

const wxColour STATIC_TEXT_CAPTION_COL = wxColour(100, 100, 100);

// 头部栏规格
constexpr int HeaderHeight       = 55;   // 标题区高度
constexpr int HeaderTitleLeft    = 28;   // 标题离左边界
constexpr int HeaderTitleTop     = 14;   // 标题离顶部
constexpr int CloseBtnSize       = 28;   // X 按钮大小
constexpr int HeaderLineBottomGap = 5;  // 分割线离头部底边

// 内容白框规格
constexpr int ContentMarginLR    = 33;   // 白框左右边距

// 列表项内边距
constexpr int ItemLabelLeft      = 20;   // 选项名称离框左边
constexpr int ItemArrowRight     = 20;   // 箭头离框右边
constexpr int ItemArrowSize      = 20;   // 箭头图标大小

enum {
    ID_HEADER_BACK  = wxID_HIGHEST + 110,
    ID_HEADER_SAVE  = wxID_HIGHEST + 111,
    ID_HEADER_CLOSE = wxID_HIGHEST + 112,
};

wxFont row_label_font()     { return Label::sysFont(18, false); }
wxFont row_value_font()     { return Label::sysFont(16, false); }

enum class AiSensitivityLevel {
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    LEVELS_NUM = 3
};

wxString ai_sensitivity_to_label(AiSensitivityLevel level) {
    switch (level) {
    case AiSensitivityLevel::LOW:    return _L("Low");
    case AiSensitivityLevel::MEDIUM: return _L("Medium");
    case AiSensitivityLevel::HIGH:   return _L("High");
    default:                         return wxEmptyString;
    }
}

DevSettingButton::DevSettingButton(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(38, 24)){
    SetBackgroundColour(DialogBGColour);
    SetCursor(wxCURSOR_HAND);

    m_img_default = create_scaled_bitmap("camera_setting", this, 20);
    m_img_hover     = create_scaled_bitmap("camera_setting_hover", this, 20);

    m_bmp = new wxStaticBitmap(this, wxID_ANY, m_img_default);

    m_bmp->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxMouseEvent evt(e);
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    });

    m_bmp->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        wxMouseEvent evt(e);
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
    });

    m_bmp->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e){
        if (!m_hover) {
            m_hover = true;
            update_displayed_bitmap();
        }
        e.Skip();
    });

    m_bmp->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
        if (m_hover) {
            m_hover = false;
            update_displayed_bitmap();
        }
        e.Skip();
    });

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddStretchSpacer();
    sizer->Add(m_bmp, 0, wxALIGN_CENTER_HORIZONTAL);
    sizer->AddStretchSpacer();
    SetSizer(sizer);
}

void DevSettingButton::update_displayed_bitmap(){
    if(!m_bmp)
        return;
    const wxBitmap desired = m_hover ? m_img_hover : m_img_default;
    m_bmp->SetBitmap(desired);
    m_bmp->Refresh();
}

DeviceSettingItem::DeviceSettingItem(wxWindow* parent, const wxString& label, NavigateFn on_click)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
    , m_on_click(std::move(on_click)){
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif
    SetBackgroundColour(DialogBGColour);
    SetBackgroundStyle(wxBG_STYLE_COLOUR);

    m_label = new Label(this, label);
    m_label->SetFont(row_label_font());
    m_label->SetForegroundColour(DialogTextPrimary);

    m_value = new Label(this, wxEmptyString);
    m_value->SetFont(row_value_font());
    m_value->SetForegroundColour(DialogTextPrimary);

    m_arrow = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("arrow-right-s-line", this, ItemArrowSize));
    m_arrow->SetMinSize(wxSize(FromDIP(ItemArrowSize), FromDIP(ItemArrowSize)));
    m_arrow->SetCursor(wxCursor(wxCURSOR_HAND));

    wxBoxSizer* h_sizer = new wxBoxSizer(wxHORIZONTAL);
    h_sizer->Add(m_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(19));
    h_sizer->AddStretchSpacer(1);
    h_sizer->Add(m_value, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    h_sizer->Add(m_arrow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(19));

    SetSizer(h_sizer);
    SetMinSize(wxSize(-1, FromDIP(ItemHeight)));

    Bind(wxEVT_ENTER_WINDOW, &DeviceSettingItem::on_enter, this);
    Bind(wxEVT_LEAVE_WINDOW, &DeviceSettingItem::on_leave, this);
    Bind(wxEVT_LEFT_DOWN, &DeviceSettingItem::on_left_down, this);

    m_label  ->Bind(wxEVT_LEFT_DOWN, &DeviceSettingItem::on_left_down, this);
    m_arrow->Bind(wxEVT_LEFT_DOWN, &DeviceSettingItem::on_left_down, this);
    m_value  ->Bind(wxEVT_LEFT_DOWN, &DeviceSettingItem::on_left_down, this);
}

DeviceSettingItem::~DeviceSettingItem() {}

void DeviceSettingItem::SetValueText(const wxString& value){
    m_value->SetLabel(value);
    m_value->Show(!value.IsEmpty());
    Layout();
    Refresh();
}

void DeviceSettingItem::on_enter(wxMouseEvent& evt){
    m_hover = true;
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetBackgroundColour(DialogBgRowHover);
    m_label->SetBackgroundColour(DialogBgRowHover);
    m_value->SetBackgroundColour(DialogBgRowHover);
    m_arrow->SetBackgroundColour(DialogBgRowHover);
    Refresh();
    evt.Skip();
}

void DeviceSettingItem::on_leave(wxMouseEvent& evt){
    m_hover = false;
    SetCursor(wxCursor(wxCURSOR_ARROW));
    SetBackgroundColour(DialogBGColour);
    m_label->SetBackgroundColour(DialogBGColour);
    m_value->SetBackgroundColour(DialogBGColour);
    m_arrow->SetBackgroundColour(DialogBGColour);
    Refresh();
    evt.Skip();
}

void DeviceSettingItem::on_left_down(wxMouseEvent& evt)
{
    if (m_on_click) m_on_click();
    evt.Skip();
}

DeviceSettingDialog::DeviceSettingDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Device Settings"),
               wxDefaultPosition, wxDefaultSize,
               wxBORDER_NONE | wxFRAME_NO_TASKBAR)
{
    SetBackgroundColour(DialogMainBGColour);
    SetSize(DialogWidth, DialogHeight);
    CenterOnParent();
    init_device_info();
    SetEscapeId(wxID_CANCEL);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { EndModal(wxID_CANCEL); });

    Bind(wxEVT_BUTTON, &DeviceSettingDialog::on_save, this, ID_HEADER_SAVE);

    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        e.Skip();
        if (e.IsShown()) {
            wxGetApp().CallAfter([this]() { Layout(); });
        }
    });

    creat_ui();
}

DeviceSettingDialog::~DeviceSettingDialog() {
}

void DeviceSettingDialog::init_device_info(){
        auto* mgr = wxGetApp().qdsdevmanager;
    if (!mgr)
        return;
    auto dev = mgr->getSelectedDevice();
    if (!dev)
        return;
    m_device_name = from_u8(dev->m_machine_name);
    m_device_type = dev->m_type;
    m_device_ip = dev->m_ip;
    m_device_mac = dev->m_mac_address;
    m_device_serial = dev->m_serial_number;
    m_device_firmware_version = dev->m_firmware_version;
}

void DeviceSettingDialog::creat_ui(){
    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    m_book = new wxSimplebook(this, wxID_ANY);
    m_book->SetBackgroundColour(DialogMainBGColour);

    m_book->AddPage(build_root_page(), wxEmptyString);
    m_book->AddPage(build_print_options_page(), wxEmptyString);
    m_book->AddPage(build_device_info_page(), wxEmptyString);
    m_book->AddPage(build_device_name_page(), wxEmptyString);

    root_sizer->Add(m_book, 1, wxEXPAND, 0);
    SetSizer(root_sizer);
    Layout();

    ShowPage(PAGE_ROOT);
}

void DeviceSettingDialog::ShowPage(int idx)
{
    if (!m_book) return;
    if (idx < 0 || idx >= PAGE_COUNT) idx = PAGE_ROOT;
    m_book->SetSelection(idx);
    if (wxWindow* page = m_book->GetCurrentPage()) {
        page->SetSize(m_book->GetClientSize());
        page->Layout();
    }
    Layout();
}

wxPanel* DeviceSettingDialog::build_root_page(){
    auto* page = new wxPanel(m_book, wxID_ANY);
    page->SetBackgroundColour(DialogMainBGColour);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(build_header(page, _L("Settings"), false, false, /*show_close=*/true),
               0, wxEXPAND);

    auto* line = new wxPanel(page, wxID_ANY);
    line->SetBackgroundColour(DialogLine);
    line->SetMinSize(wxSize(-1, FromDIP(2)));
    sizer->Add(line, 0, wxEXPAND);

    sizer->AddSpacer(FromDIP(66));

    auto* option_panel = new wxPanel(page, wxID_ANY);
    option_panel->SetBackgroundColour(DialogBGColour);


    auto* content_sizer = new wxBoxSizer(wxHORIZONTAL);
    content_sizer->AddSpacer(FromDIP(ContentMarginLR));

    auto* inner_sizer = new wxBoxSizer(wxVERTICAL);

    m_item_print_options = new DeviceSettingItem(option_panel, _L("Print Options"), [this](){ShowPage(PAGE_PRINT_OPTIONS);});
    m_item_device_info = new DeviceSettingItem(option_panel, _L("Device Info"), [this](){ShowPage(PAGE_DEVICE_INFO);});
    m_item_device_name = new DeviceSettingItem(option_panel, _L("Machine Name"), [this]() {ShowPage(PAGE_DEVICE_NAME); });

    wxString dev_name = m_device_name.IsEmpty() ? wxEmptyString : m_device_name;
    m_item_device_name->SetValueText(m_device_name);

    auto make_hline = [option_panel]() {
        auto* line = new wxPanel(option_panel, wxID_ANY);
        line->SetBackgroundColour(DialogLine_2);
        line->SetBackgroundStyle(wxBG_STYLE_COLOUR);
        line->SetMinSize(wxSize(-1, option_panel->FromDIP(1.5)));
        return line;
    };

    inner_sizer->Add(m_item_print_options, 0, wxEXPAND);
    inner_sizer->Add(make_hline(), 0, wxEXPAND | wxLEFT | wxRIGHT, option_panel->FromDIP(10));
    inner_sizer->Add(m_item_device_info,   0, wxEXPAND);
    inner_sizer->Add(make_hline(), 0, wxEXPAND | wxLEFT | wxRIGHT, option_panel->FromDIP(10));
    inner_sizer->Add(m_item_device_name,   0, wxEXPAND);

    option_panel->SetSizer(inner_sizer);

    content_sizer->Add(option_panel, 1, wxEXPAND);
    content_sizer->AddSpacer(FromDIP(ContentMarginLR));

    sizer->Add(content_sizer, 0, wxEXPAND);
    sizer->Add(0, 0, 1, wxEXPAND);
    page->SetSizer(sizer);
    return page;
}

wxPanel* DeviceSettingDialog::build_header(wxWindow* parent, const wxString& title,
                                           bool show_back, bool show_save, bool show_close)
{
    const bool dark = wxGetApp().dark_mode();
    const wxColour text_fg = dark ? wxColour(100, 100, 105) : wxColour(50, 50, 55);
    m_text_fg = text_fg;

    auto* header = new wxPanel(parent, wxID_ANY);
    header->SetBackgroundColour(DialogMainBGColour);
    header->SetMinSize(wxSize(-1, FromDIP(HeaderHeight)));

    auto* h_sizer = new wxBoxSizer(wxHORIZONTAL);

    h_sizer->AddSpacer(FromDIP(HeaderTitleLeft));

    if(show_back){
        auto* back_btn = new wxStaticBitmap(header, ID_HEADER_BACK,
            create_scaled_bitmap("arrow-left-s-line", header, 30));
        back_btn->SetMinSize(wxSize(FromDIP(30), FromDIP(30)));
        back_btn->SetCursor(wxCursor(wxCURSOR_HAND));
        back_btn->Bind(wxEVT_LEFT_DOWN, &DeviceSettingDialog::on_back, this);
        h_sizer->Add(back_btn, 0, wxALIGN_CENTER_VERTICAL);
        h_sizer->AddSpacer(FromDIP(192));
    }

    auto* title_label = new Label(header, title);
    title_label->SetFont(Label::Head_20);
    title_label->SetForegroundColour(DialogTextPrimary);
    h_sizer->Add(title_label, 0, wxALIGN_CENTER_VERTICAL);

    h_sizer->Add(0, 0, 1);

    if(show_close){
        wxPanel* close_btn = new wxPanel(header, wxID_ANY, wxDefaultPosition,
                                        wxSize(FromDIP(CloseBtnSize), FromDIP(CloseBtnSize)));
        close_btn->SetBackgroundColour(DialogMainBGColour);
        close_btn->SetCursor(wxCURSOR_HAND);
        close_btn->SetCanFocus(false);
        close_btn->Bind(wxEVT_PAINT, [text_fg](wxPaintEvent& evt) {
            auto* w = static_cast<wxWindow*>(evt.GetEventObject());
            wxPaintDC dc(w);
            wxSize sz = w->GetSize();
            dc.SetPen(wxPen(text_fg, 2));
            int m = 8;
            dc.DrawLine(m, m, sz.x - m, sz.y - m);
            dc.DrawLine(sz.x - m, m, m, sz.y - m);
        });
        close_btn->Bind(wxEVT_LEFT_DOWN, &DeviceSettingDialog::on_close, this);
        h_sizer->Add(close_btn, 0, wxALIGN_CENTER_VERTICAL);
    }

    if(show_save){
        auto* save_btn = new Label(header, _L("Save"));
        save_btn->SetFont(Label::Body_14);
        save_btn->SetForegroundColour(wxColour(0x00, 0x9A, 0xFF));
        save_btn->SetCursor(wxCursor(wxCURSOR_HAND));
        save_btn->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& evt) {
            wxCommandEvent cmd(wxEVT_BUTTON, ID_HEADER_SAVE);
            ProcessWindowEvent(cmd);
            evt.Skip();
        });
        h_sizer->Add(save_btn, 0, wxALIGN_CENTER_VERTICAL);
    }

    h_sizer->AddSpacer(FromDIP(HeaderTitleLeft));

    header->SetSizer(h_sizer);

    return header;
}

void DeviceSettingDialog::GoRoot(){
    ShowPage(PAGE_ROOT);
}

void DeviceSettingDialog::on_close(wxMouseEvent& evt)
{
    EndModal(wxID_CANCEL);
    evt.Skip();
}

void DeviceSettingDialog::on_back(wxMouseEvent& evt)
{
    GoRoot();
    evt.Skip();
}

void DeviceSettingDialog::on_save(wxCommandEvent& evt)
{
    auto* mgr = wxGetApp().qdsdevmanager;
    if (!mgr) {
        evt.Skip();
        return;
    }
    auto dev = mgr->getSelectedDevice();
    if (!dev || !m_device_name_input) {
        evt.Skip();
        return;
    }

    if (!dev->is_net_device) {
        evt.Skip();
        return;
    }

    wxString new_name = m_device_name_input->GetTextCtrl()->GetValue();
    if (new_name.IsEmpty()) {
        evt.Skip();
        return;
    }
    std::string utf8_name = std::string(new_name.utf8_str());

#if QDT_RELEASE_TO_PUBLIC
    if (dev->is_net_device) {
        std::string serial = dev->m_id;
        std::string region = wxGetApp().app_config->get("region");
        Environment env = (region == "China") ? PRODUCTIONENV : FOREIGNENV;

        std::thread([serial, env, utf8_name]() {
            HttpData hd;
            hd.env      = env;
            hd.target   = DEVICE;
            hd.taskPath = QIDIMakerUrlBuilder::MakerTaskPath::kUpdateAlias;
            hd.body     = json{{"serialNumber", serial}, {"alias", utf8_name}}.dump();
            bool ok = false;
            MakerHttpHandle::getInstance().httpPostTask(hd, ok);
        }).detach();
    }
#endif
    m_device_name = new_name;
    if (m_item_device_name)
        m_item_device_name->SetValueText(new_name);

    GoRoot();
    evt.Skip();
}

wxPanel* DeviceSettingDialog::build_print_options_page(){
    wxPanel* page = new wxPanel(m_book, wxID_ANY);
    page->SetBackgroundColour(DialogMainBGColour);

    wxBoxSizer* main_v_sizer = new wxBoxSizer(wxVERTICAL);
    main_v_sizer->Add(build_header(page, _L("Print Options"), true, false, false), 0, wxEXPAND);

    wxScrolledWindow* scroll_sizer = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                                            wxDefaultSize, wxVSCROLL);
    scroll_sizer->SetBackgroundColour(DialogMainBGColour);
    scroll_sizer->SetScrollRate(0, FromDIP(10));
    m_print_options_scroll = scroll_sizer;

    wxBoxSizer* content = new wxBoxSizer(wxVERTICAL);

    // ---- AI Detections ----
    ai_refine_panel = new wxPanel(scroll_sizer, wxID_ANY);
    ai_refine_panel->SetBackgroundColour(DialogMainBGColour);
    wxBoxSizer* ai_refine_sizer = new wxBoxSizer(wxVERTICAL);

    {
        wxBoxSizer* line_sizer = new wxBoxSizer(wxHORIZONTAL);
        text_ai_detections = new Label(ai_refine_panel, _L("AI Detections"));
        text_ai_detections->SetFont(Label::Body_14);
        line_sizer->Add(FromDIP(5), 0, 0, 0);
        line_sizer->Add(text_ai_detections, 0, wxLEFT | wxRIGHT | wxDOWN | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        ai_refine_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
        ai_refine_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

        line_sizer = new wxBoxSizer(wxHORIZONTAL);
        text_ai_detections_caption = new Label(ai_refine_panel, _L("Printer will send assistant message or pause printing if any of the following problem is detected."));
        text_ai_detections_caption->SetFont(Label::Body_12);
        text_ai_detections_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
        text_ai_detections_caption->Wrap(FromDIP(400));
        line_sizer->Add(FromDIP(5), 0, 0, 0);
        line_sizer->Add(text_ai_detections_caption, 0,wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        ai_refine_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
        ai_refine_sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    }

    {
        wxBoxSizer* line_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_cb_spaghetti_detection = new CheckBox(ai_refine_panel);
        text_spaghetti_detection = new Label(ai_refine_panel, _L("Spaghetti Detection"));
        text_spaghetti_detection->SetFont(Label::Body_14);
        line_sizer->Add(FromDIP(5), 0, 0, 0);
        line_sizer->Add(m_cb_spaghetti_detection, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        line_sizer->Add(text_spaghetti_detection, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        ai_refine_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

        line_sizer = new wxBoxSizer(wxHORIZONTAL);
        text_spaghetti_detection_caption = new Label(ai_refine_panel, _L("Detects spaghetti failure(scattered lose filament)."));
        text_spaghetti_detection_caption->SetFont(Label::Body_12);
        text_spaghetti_detection_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
        text_spaghetti_detection_caption->Wrap(-1);
        line_sizer->Add(FromDIP(30), 0, 0, 0);
        line_sizer->Add(text_spaghetti_detection_caption, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
        ai_refine_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

        wxBoxSizer* spaghetti_sensitivity_sizer = new wxBoxSizer(wxVERTICAL);
        {
            wxBoxSizer* sensitivity_line = new wxBoxSizer(wxHORIZONTAL);
            Label* text_spaghetti_sensitivity = new Label(ai_refine_panel, _L("Pausing Sensitivity:"));
            text_spaghetti_sensitivity->SetFont(Label::Body_12);
            text_spaghetti_sensitivity->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
            text_spaghetti_sensitivity->Wrap(-1);

            spaghetti_detection_level_list = new ComboBox(ai_refine_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(100), -1), 0, NULL, wxCB_READONLY);
            for (auto i = AiSensitivityLevel::LOW; i < AiSensitivityLevel::LEVELS_NUM; i = (AiSensitivityLevel)((int)i + 1)) {
                spaghetti_detection_level_list->Append(ai_sensitivity_to_label(i));
            }
            if (spaghetti_detection_level_list->GetCount() > 0) {
                spaghetti_detection_level_list->SetSelection(0);
            }

            sensitivity_line->Add(FromDIP(30), 0, 0, 0);
            sensitivity_line->Add(text_spaghetti_sensitivity, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
            sensitivity_line->Add(spaghetti_detection_level_list, 0, wxEXPAND | wxALL, FromDIP(5));
            spaghetti_sensitivity_sizer->Add(sensitivity_line, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
        }
        m_spaghetti_sensitivity_item = ai_refine_sizer->Add(spaghetti_sensitivity_sizer, 0, wxEXPAND, 0);

        m_cb_spaghetti_detection->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
            apply_print_options_to_device();
            evt.Skip();
        });
    }

    {
        ai_refine_sizer->Add(0, 0, 0, wxTOP, FromDIP(12));

        wxBoxSizer* line_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_cb_fod_check = new CheckBox(ai_refine_panel);
        text_fod_check = new Label(ai_refine_panel, _L("Foreign Object Detection"));
        text_fod_check->SetFont(Label::Body_14);
        line_sizer->Add(FromDIP(5), 0, 0, 0);
        line_sizer->Add(m_cb_fod_check, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        line_sizer->Add(text_fod_check, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
        ai_refine_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

        line_sizer = new wxBoxSizer(wxHORIZONTAL);
        text_fod_check_caption = new Label(ai_refine_panel, _L("Checks for any objects on the build plate at the start of a print to avoid collisions."));
        text_fod_check_caption->SetFont(Label::Body_12);
        text_fod_check_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
        text_fod_check_caption->Wrap(FromDIP(400));
        line_sizer->Add(FromDIP(30), 0, 0, 0);
        line_sizer->Add(text_fod_check_caption, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
        ai_refine_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
        ai_refine_sizer->Add(0, 0, 0, wxTOP, FromDIP(12));
    }

    ai_refine_panel->SetSizer(ai_refine_sizer);
    content->Add(ai_refine_panel, 0, wxEXPAND | wxRIGHT, FromDIP(18));
    content->Add(0, 0, 0, wxTOP, FromDIP(20));

    // {
    //     auto* line = new wxPanel(ai_refine_panel, wxID_ANY);
    //     line->SetBackgroundColour(DialogLine);
    //     line->SetMinSize(wxSize(-1, FromDIP(2)));
    //     ai_refine_sizer->Add(line, 0, wxEXPAND);
    // }

    // {
    //     wxPanel* other_panel = new wxPanel(scroll_sizer, wxID_ANY);
    //     other_panel->SetBackgroundColour(DialogMainBGColour);
    //     wxBoxSizer* other_sizer = new wxBoxSizer(wxVERTICAL);

    //     wxBoxSizer* line_sizer = new wxBoxSizer(wxHORIZONTAL);
    //     Label* text_other = new Label(other_panel, _L("Other"));
    //     text_other->SetFont(Label::Body_14);
    //     line_sizer->Add(FromDIP(5), 0, 0, 0);
    //     line_sizer->Add(text_other, 0, wxLEFT | wxRIGHT | wxDOWN | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    //     other_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    //     other_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

    //     line_sizer = new wxBoxSizer(wxHORIZONTAL);
    //     m_rb_extruder_cooler = new CheckBox(other_panel);
    //     text_extruder_cooler = new Label(other_panel, _L("Polar Cooler"));
    //     text_extruder_cooler->SetFont(Label::Body_14);
    //     line_sizer->Add(FromDIP(5), 0, 0, 0);
    //     line_sizer->Add(m_rb_extruder_cooler, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    //     line_sizer->Add(text_extruder_cooler, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    //     other_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

    //     line_sizer = new wxBoxSizer(wxHORIZONTAL);
    //     text_extruder_cooler_caption = new Label(other_panel, _L("Enable the Polar cooler and lower the temperature of the extruder."));
    //     text_extruder_cooler_caption->SetFont(Label::Body_12);
    //     text_extruder_cooler_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    //     text_extruder_cooler_caption->Wrap(FromDIP(400));
    //     line_sizer->Add(FromDIP(30), 0, 0, 0);
    //     line_sizer->Add(text_extruder_cooler_caption, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    //     other_sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    //     other_sizer->Add(0, 0, 0, wxTOP, FromDIP(12));

    //     other_panel->SetSizer(other_sizer);
    //     content->Add(other_panel, 0, wxEXPAND | wxRIGHT, FromDIP(18));
    // }

    m_cb_fod_check->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        apply_print_options_to_device();
        evt.Skip();
    });
    // m_rb_extruder_cooler->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
    //     apply_print_options_to_device();
    //     evt.Skip();
    // });
    spaghetti_detection_level_list->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt) {
        apply_print_options_to_device();
        evt.Skip();
    });

    auto* h_sizer = new wxBoxSizer(wxHORIZONTAL);
    h_sizer->AddStretchSpacer(1);
    h_sizer->Add(content, 0, wxALL, 5);
    h_sizer->AddStretchSpacer(1);

    scroll_sizer->SetSizer(h_sizer);
    main_v_sizer->AddSpacer(FromDIP(HeaderTitleLeft));
    main_v_sizer->Add(scroll_sizer, 1, wxEXPAND);
    page->SetSizer(main_v_sizer);
    update_spaghetti_detection_from_device();
    return page;
}

wxPanel* DeviceSettingDialog::build_device_info_page(){
    auto* page = new wxPanel(m_book, wxID_ANY);
    page->SetBackgroundColour(DialogMainBGColour);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(build_header(page, _L("Device Info"), true, false, false),
               0, wxEXPAND);

    sizer->AddSpacer(FromDIP(51));

    auto* dev_info_panel = new wxPanel(page, wxID_ANY);
    dev_info_panel->SetBackgroundColour(DialogBGColour);

    auto make_hline = [dev_info_panel]() {
        auto* line = new wxPanel(dev_info_panel, wxID_ANY);
        line->SetBackgroundColour(DialogLine_2);
        line->SetBackgroundStyle(wxBG_STYLE_COLOUR);
        line->SetMinSize(wxSize(-1, dev_info_panel->FromDIP(1.5)));
        return line;
    };

    auto* content_sizer = new wxBoxSizer(wxHORIZONTAL);
    content_sizer->AddSpacer(FromDIP(ContentMarginLR));

    auto* inner_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* type_sizer = new wxBoxSizer(wxHORIZONTAL);
    type_sizer->SetMinSize(wxSize(-1, FromDIP(ItemHeight)));
    Label* machine_type = new Label(dev_info_panel, _L("Machine Type"));
    machine_type->SetFont(row_label_font());
    Label* machine_type_value = new Label(dev_info_panel, m_device_type);
    machine_type_value->SetFont(row_value_font());
    type_sizer->Add(FromDIP(19), 0, 0, 0);
    type_sizer->Add(machine_type, 0, wxALIGN_CENTER_VERTICAL);
    type_sizer->AddStretchSpacer();
    type_sizer->Add(machine_type_value, 0, wxALIGN_CENTER_VERTICAL);
    type_sizer->Add(FromDIP(19), 0, 0, 0);

    inner_sizer->Add(type_sizer, 0, wxEXPAND);
    inner_sizer->Add(make_hline(), 0, wxEXPAND | wxLEFT | wxRIGHT, dev_info_panel->FromDIP(10));

    wxBoxSizer* ip_sizer = new wxBoxSizer(wxHORIZONTAL);
    ip_sizer->SetMinSize(wxSize(-1, FromDIP(ItemHeight)));
    Label* machine_ip = new Label(dev_info_panel, _L("IP"));
    machine_ip->SetFont(row_label_font());
    Label* machine_ip_value = new Label(dev_info_panel, m_device_ip);
    machine_ip_value->SetFont(row_value_font());
    ip_sizer->Add(FromDIP(19), 0, 0, 0);
    ip_sizer->Add(machine_ip, 0, wxALIGN_CENTER_VERTICAL);
    ip_sizer->AddStretchSpacer();
    ip_sizer->Add(machine_ip_value, 0, wxALIGN_CENTER_VERTICAL);
    ip_sizer->Add(FromDIP(19), 0, 0, 0);

    inner_sizer->Add(ip_sizer, 0, wxEXPAND);
    inner_sizer->Add(make_hline(), 0, wxEXPAND | wxLEFT | wxRIGHT, dev_info_panel->FromDIP(10));

    wxBoxSizer* mac_sizer = new wxBoxSizer(wxHORIZONTAL);
    mac_sizer->SetMinSize(wxSize(-1, FromDIP(ItemHeight)));
    Label* machine_mac = new Label(dev_info_panel, _L("MAC"));
    machine_mac->SetFont(row_label_font());
    Label* machine_mac_value = new Label(dev_info_panel, m_device_mac);
    machine_mac_value->SetFont(row_value_font());
    mac_sizer->Add(FromDIP(19), 0, 0, 0);
    mac_sizer->Add(machine_mac, 0, wxALIGN_CENTER_VERTICAL);
    mac_sizer->AddStretchSpacer();
    mac_sizer->Add(machine_mac_value, 0, wxALIGN_CENTER_VERTICAL);
    mac_sizer->Add(FromDIP(19), 0, 0, 0);

    inner_sizer->Add(mac_sizer, 0, wxEXPAND);
    inner_sizer->Add(make_hline(), 0, wxEXPAND | wxLEFT | wxRIGHT, dev_info_panel->FromDIP(10));

    wxBoxSizer* serial_sizer = new wxBoxSizer(wxHORIZONTAL);
    serial_sizer->SetMinSize(wxSize(-1, FromDIP(ItemHeight)));
    Label* machine_serial= new Label(dev_info_panel, _L("Machine Serial"));
    machine_serial->SetFont(row_label_font());
    Label* machine_serial_value = new Label(dev_info_panel, m_device_serial);
    machine_serial_value->SetFont(row_value_font());
    serial_sizer->Add(FromDIP(19), 0, 0, 0);
    serial_sizer->Add(machine_serial, 0, wxALIGN_CENTER_VERTICAL);
    serial_sizer->AddStretchSpacer();
    serial_sizer->Add(machine_serial_value, 0, wxALIGN_CENTER_VERTICAL);
    serial_sizer->Add(FromDIP(19), 0, 0, 0);

    inner_sizer->Add(serial_sizer, 0, wxEXPAND);
    inner_sizer->Add(make_hline(), 0, wxEXPAND | wxLEFT | wxRIGHT, dev_info_panel->FromDIP(10));

    wxBoxSizer* firmware_version_sizer = new wxBoxSizer(wxHORIZONTAL);
    firmware_version_sizer->SetMinSize(wxSize(-1, FromDIP(ItemHeight)));
    Label* machine_firmware_version = new Label(dev_info_panel, _L("Firmware Version"));
    machine_firmware_version->SetFont(row_label_font());
    Label* machine_firmware_version_value = new Label(dev_info_panel, m_device_firmware_version);
    machine_firmware_version_value->SetFont(row_value_font());
    firmware_version_sizer->Add(FromDIP(19), 0, 0, 0);
    firmware_version_sizer->Add(machine_firmware_version, 0, wxALIGN_CENTER_VERTICAL);
    firmware_version_sizer->AddStretchSpacer();
    firmware_version_sizer->Add(machine_firmware_version_value, 0, wxALIGN_CENTER_VERTICAL);
    firmware_version_sizer->Add(FromDIP(19), 0, 0, 0);

    inner_sizer->Add(firmware_version_sizer, 0, wxEXPAND);
    inner_sizer->Add(make_hline(), 0, wxEXPAND | wxLEFT | wxRIGHT, dev_info_panel->FromDIP(10));

    dev_info_panel->SetSizer(inner_sizer);

    content_sizer->Add(dev_info_panel, 1, wxEXPAND);
    content_sizer->AddSpacer(FromDIP(ContentMarginLR));

    sizer->Add(content_sizer, 0, wxEXPAND);
    sizer->Add(0, 0, 1, wxEXPAND);
    page->SetSizer(sizer);
    return page;
}

wxPanel* DeviceSettingDialog::build_device_name_page()
{
    auto* page = new wxPanel(m_book, wxID_ANY);
    page->SetBackgroundColour(DialogMainBGColour);

    auto* mgr = wxGetApp().qdsdevmanager;
    auto dev = mgr ? mgr->getSelectedDevice() : nullptr;
    bool can_edit = dev && dev->is_net_device;

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->Add(build_header(page, _L("Machine Name"), true, can_edit, false),
               0, wxEXPAND);

    sizer->AddSpacer(FromDIP(40));

    m_device_name_input = new TextInput(page, m_device_name, _L("Name"), wxEmptyString,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxTE_PROCESS_ENTER);
    m_device_name_input->SetCornerRadius(FromDIP(8));
    m_device_name_input->SetMinSize(wxSize(-1, ItemHeight));

    if (can_edit) {
        m_device_name_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
            wxCommandEvent cmd(wxEVT_BUTTON, ID_HEADER_SAVE);
            ProcessWindowEvent(cmd);
        });
    } else {
        m_device_name_input->GetTextCtrl()->SetEditable(false);
    }

    auto* content_sizer = new wxBoxSizer(wxHORIZONTAL);
    content_sizer->AddSpacer(FromDIP(ContentMarginLR));
    content_sizer->Add(m_device_name_input, 1, wxEXPAND);
    content_sizer->AddSpacer(FromDIP(ContentMarginLR));

    sizer->Add(content_sizer, 0, wxEXPAND);

    page->SetSizer(sizer);
    return page;
}

void DeviceSettingDialog::refresh_print_options_layout()
{
    this->Layout();
    if (m_print_options_scroll) {
        m_print_options_scroll->Layout();
        m_print_options_scroll->FitInside();
    }
}

void DeviceSettingDialog::update_spaghetti_detection_from_device()
{
    auto* mgr = wxGetApp().qdsdevmanager;
    if (!mgr)
        return;
    auto dev = mgr->getSelectedDevice();
    if (!dev)
        return;

    bool cur_detection = m_cb_spaghetti_detection->GetValue();
    //bool cur_coolar = m_rb_extruder_cooler->GetValue();
    bool cur_det_foreign = m_cb_fod_check->GetValue();
    int         level = spaghetti_detection_level_list->GetSelection();
    std::string lvl   = sensitivity_level_to_msg_string((AiMonitorSensitivityLevel) level);

    bool on = dev->is_support_detect_spaghetti.load();
    bool det_foreign_on = dev->is_support_detect_foreign.load();
    //bool coolar_on = dev->m_enable_polar_cooler.load();

    //if(cur_detection == on && lvl == dev->spaghetti_level && coolar_on == cur_coolar && cur_det_foreign == det_foreign_on)
    if(cur_detection == on && lvl == dev->spaghetti_level && cur_det_foreign == det_foreign_on)
        return;

    //m_rb_extruder_cooler->SetValue(coolar_on);
    m_cb_spaghetti_detection->SetValue(on);
    m_cb_fod_check->SetValue(det_foreign_on);
    if (on && spaghetti_detection_level_list) {
        const std::string& s = dev->spaghetti_level;
        int idx = (int)AiMonitorSensitivityLevel::LOW;
        if (s == "HIGH")        
            idx = (int)AiMonitorSensitivityLevel::HIGH;
        else if (s == "MEDIUM") 
            idx = (int)AiMonitorSensitivityLevel::MEDIUM;
        else                    
            idx = (int)AiMonitorSensitivityLevel::LOW;
        if (idx < (int)spaghetti_detection_level_list->GetCount())
            spaghetti_detection_level_list->SetSelection(idx);
    }

    if (m_spaghetti_sensitivity_item)
        m_spaghetti_sensitivity_item->Show(on);
    refresh_print_options_layout();
}

void DeviceSettingDialog::apply_print_options_to_device()
{
    auto* mgr = wxGetApp().qdsdevmanager;
    if (!mgr)
        return;
    auto dev = mgr->getSelectedDevice();
    if (!dev)
        return;

    bool spaghetti_on = m_cb_spaghetti_detection->GetValue();
    bool fod_on       = m_cb_fod_check->GetValue();
    //bool cooler_on    = m_rb_extruder_cooler->GetValue();
    int  sens_sel     = spaghetti_detection_level_list ? spaghetti_detection_level_list->GetSelection() : 0;

//y84
    // Route the print options through PrinterTaskDispatcher (local SAVE_VARIABLE / cloud HTTP posts).
    PrinterTask task;
    task.type              = PrinterTaskType::SetPrintOptions;
    task.device_id         = dev->m_id;
    task.print_opt_spaghetti   = spaghetti_on;
    task.print_opt_fod         = fod_on;
    task.print_opt_sensitivity = sens_sel;

#if QDT_RELEASE_TO_PUBLIC
    if (dev->is_net_device) {
        std::string region = wxGetApp().app_config->get("region");
        Environment env = (region == "China") ? PRODUCTIONENV : FOREIGNENV;
        task.transport = PrinterTaskTransport::Cloud;
        // Cloud posts are async (detached thread) to avoid blocking the UI, matching the original behavior.
        auto task_copy = task;
        auto* mgr_ptr  = mgr;
        std::thread([task_copy, env, mgr_ptr]() {
            PrinterTaskDispatcher dispatcher(mgr_ptr);
            dispatcher.dispatch(task_copy, env, PRINTERTYPE);
        }).detach();
    }
    else
#endif
    {
        task.transport = PrinterTaskTransport::Local;
        PrinterTaskDispatcher dispatcher(mgr);
        dispatcher.dispatch(task);
    }
//y84

    update_spaghetti_detection_from_device();
}

std::string DeviceSettingDialog::sensitivity_level_to_msg_string(enum AiMonitorSensitivityLevel level) {
    switch (level) {
    case LOW:
        return "LOW";
    case MEDIUM:
        return "MEDIUM";
    case HIGH:
        return "HIGH";
    default:
        return "";
    }
    return "";
}
}
}



