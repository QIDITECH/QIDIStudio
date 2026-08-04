#ifndef slic3r_HMSDialog_hpp_
#define slic3r_HMSDialog_hpp_

#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/timer.h>
#include <vector>
#include <string>

namespace Slic3r { namespace GUI {

struct QDSDeviceErrorData;

//cj_5 HMS notification popup, accepts real HMS data from StatusPanel.
class HMSDialog : public wxDialog
{
public:
    HMSDialog(wxWindow* parent, const std::vector<QDSDeviceErrorData>& items, const std::string& machine_type);

    //cj_5 Called by StatusPanel when new HMS data arrives while dialog is open
    void notify_new_data();

private:
    void refresh_ui();
    void clear_panels();

    wxScrolledWindow*    m_scrolled{ nullptr };
    wxBoxSizer*          m_top_sizer{ nullptr };
    const std::vector<QDSDeviceErrorData>& m_items;
    std::string          m_machine_type;
    //cj_5 Dark-mode aware colours, set in constructor.
    wxColour m_page_bg;
    wxColour m_card_bg;
    wxColour m_sep_col;
    wxColour m_empty_fg;
    wxColour m_text_fg;
};

//cj_5 HMS indicator — swaps between notification_default / notification_dot bitmaps.
class HMSIndicatorButton : public wxPanel
{
public:
    HMSIndicatorButton(wxWindow* parent);

    void set_has_errors(bool has);

private:
    wxStaticBitmap* m_bmp{ nullptr };
    wxBitmap m_img_default;
    wxBitmap m_img_dot;
    bool     m_has_errors{ false };
};

}} // namespace

#endif
