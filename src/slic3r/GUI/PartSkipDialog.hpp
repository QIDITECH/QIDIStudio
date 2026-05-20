#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/webrequest.h>
#include <wx/control.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/zstream.h>
#include <wx/window.h>
#include <wx/dcgraph.h>
#include <wx/simplebook.h>

#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/AnimaController.hpp"
#include "PartSkipCommon.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"

#include <atomic>


namespace Slic3r { namespace GUI {

class SkipPartCanvas;

class PartSkipConfirmDialog : public DPIDialog
{
private:
protected:
    Label  *m_msg_label;
    Label  *m_tip_label;
    Button *m_apply_button;

public:
    PartSkipConfirmDialog(wxWindow *parent);
    ~PartSkipConfirmDialog();

    void    on_dpi_changed(const wxRect &suggested_rect);
    Button *GetConfirmButton();
    void    SetMsgLabel(wxString msg);
    void    SetTipLabel(wxString msg);
    bool    Show(bool show);
};

class PartSkipDialog : public DPIDialog
{
public:
    PartSkipDialog(wxWindow *parent);
    ~PartSkipDialog();
    void on_dpi_changed(const wxRect &suggested_rect);
    bool Show(bool show);

    //cj_4
    // The caller passes the device's FRP base URL, a device identifier, and
    // the live excluded-objects list pushed from Klipper. The dialog downloads
    // pick_1.png / model_settings.config / slice_info.config for canvas image
    // and model metadata, but the skipped state is now resolved in InitDialogUI
    // against m_excluded_objects instead of reading the XML skipped attribute.
    void SetSimplebookPage(int page);
    //cj_4
    // Excluded object names pushed from Klipper (exclude_object/excluded_objects).
    std::vector<std::string> m_excluded_objects;

    //cj_4
    // plate_index: current plate index from Klipper print_stats/plateindex,
    // used to select which pick_<N>.png and plate-specific metadata to load.
    void InitSchedule(const std::string &frp_url, const std::string &dev_id, const std::vector<std::string>& excluded_objects, int plate_index);

    
    void InitDialogUI();
    int  GetAllSkippedPartsNum();
    
    // cj_4 Get the list of skipped object names
    std::vector<std::string> GetSkippedObjectNames();

    wxSimplebook *m_simplebook;
    wxPanel      *m_book_third_panel;
    wxPanel      *m_book_second_panel;
    wxPanel      *m_book_first_panel;

    SkipPartCanvas   *m_canvas;
    Button           *m_zoom_in_btn;
    Button           *m_zoom_out_btn;
    Button           *m_switch_drag_btn;
    CheckBox         *m_all_checkbox;
    Button           *m_percent_label;
    Label            *m_all_label;
    wxPanel          *m_line;
    wxPanel          *m_line_top;
    wxScrolledWindow *m_list_view;

    wxPanel *m_dlg_placeholder;
    Label   *m_cnt_label;
    Label   *m_tot_label;

    Button *m_apply_btn;

    Label          *m_loading_label;
    Label          *m_retry_label;
    ScalableBitmap *m_retry_icon;
    wxStaticBitmap *m_retry_bitmap;

    wxBoxSizer *m_sizer;
    wxBoxSizer *m_dlg_sizer;
    wxBoxSizer *m_dlg_content_sizer;
    wxBoxSizer *m_dlg_btn_sizer;
    wxBoxSizer *m_canvas_sizer;
    wxBoxSizer *m_canvas_btn_sizer;
    wxBoxSizer *m_list_sizer;
    wxBoxSizer *m_scroll_sizer;
    wxBoxSizer *m_book_first_sizer;
    wxBoxSizer *m_book_second_sizer;
    wxBoxSizer *m_book_second_btn_sizer;
    Button     *m_second_retry_btn;
    AnimaIcon  *m_loading_icon;

private:
    //cj_4
    // plate_index is hard-wired to 1 for this display-only phase.
    int  m_plate_idx{1};
    int  m_zoom_percent{100};
    bool m_is_drag{false};
    bool m_print_lock{true};
    bool m_enable_apply_btn{false};
    bool is_model_support_partskip{false};

    std::map<uint32_t, PartState>   m_parts_state;
    std::map<uint32_t, std::string> m_parts_name;
    std::vector<int>                m_partskip_ids;

    PartsInfo GetPartsInfo();
    bool      is_drag_mode();

    //cj_4
    // Inputs supplied by the caller (QDSDevice). m_frp_url is the HTTP base
    // such as "http://192.168.x.x:7125"; the dialog appends
    // "/server/files/.temp/<filename>" to form download URLs.
    std::string m_frp_url;
    std::string m_dev_id;

    std::string              m_timestamp;
    std::string              m_tmp_path;
    std::vector<std::string> m_local_paths;
    std::vector<std::string> m_target_paths; // kept for logging/diagnostics only

    //cj_4
    // Async download bookkeeping. Counts files still in flight and whether any
    // of them failed. Updated from HTTP callbacks that are marshalled back onto
    // the GUI thread via CallAfter().
    std::atomic<int>  m_pending_downloads{0};
    std::atomic<bool> m_download_failed{false};

    std::string create_tmp_path();
    bool        is_local_file_existed(const std::vector<std::string> &local_paths);

    void DownloadPartsFile();
    void DownloadOneFile(const std::string &remote_name, const std::string &local_path);
    void OnAllDownloadsFinished();

    void OnZoomIn(wxCommandEvent &event);
    void OnZoomOut(wxCommandEvent &event);
    void OnSwitchDrag(wxCommandEvent &event);
    void OnZoomPercent(wxCommandEvent &event);
    void UpdatePartsStateFromCanvas(wxCommandEvent &event);

    void UpdateZoomPercent();
    void UpdateCountLabel();
    void UpdateDialogUI();
    void UpdateApplyButtonStatus();
    bool IsAllChecked();
    bool IsAllCancled();

    void OnRetryButton(wxCommandEvent &event);
    void OnAllCheckbox(wxCommandEvent &event);
    void OnApplyDialog(wxCommandEvent &event);
};

}} // namespace Slic3r::GUI