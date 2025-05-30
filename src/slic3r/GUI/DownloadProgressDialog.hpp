#ifndef slic3r_DownloadProgressDialog_hpp_
#define slic3r_DownloadProgressDialog_hpp_

#include <string>
#include <unordered_map>

#include "GUI_Utils.hpp"
#include <wx/dialog.h>
#include <wx/font.h>
#include <wx/bitmap.h>
#include <wx/msgdlg.h>
#include <wx/richmsgdlg.h>
#include <wx/textctrl.h>
#include <wx/statline.h>
#include <wx/simplebook.h>
#include "Widgets/Button.hpp"
#include "QDTStatusBar.hpp"
#include "QDTStatusBarSend.hpp"
#include "Jobs/UpgradeNetworkJob.hpp"

class wxBoxSizer;
class wxCheckBox;
class wxStaticBitmap;

#define MSG_DIALOG_BUTTON_SIZE wxSize(FromDIP(58), FromDIP(24))
#define MSG_DIALOG_MIDDLE_BUTTON_SIZE wxSize(FromDIP(76), FromDIP(24))
#define MSG_DIALOG_LONG_BUTTON_SIZE wxSize(FromDIP(90), FromDIP(24))


namespace Slic3r {
namespace GUI {

class DownloadProgressDialog : public DPIDialog
{
protected:
    bool Show(bool show) override;
    void on_close(wxCloseEvent& event);

public:
    DownloadProgressDialog(wxString title, bool post_login = false);
    wxString format_text(wxStaticText* st, wxString str, int warp);
    ~DownloadProgressDialog();

    void on_dpi_changed(const wxRect &suggested_rect) override;
    void update_release_note(std::string release_note, std::string version);

    wxSimplebook* m_simplebook_status{nullptr};

	std::shared_ptr<QDTStatusBarSend> m_status_bar;
    std::shared_ptr<UpgradeNetworkJob> m_upgrade_job { nullptr };
    wxPanel *                         m_panel_download;

protected:
    virtual std::shared_ptr<UpgradeNetworkJob> make_job(std::shared_ptr<ProgressIndicator> pri);
    virtual void                               on_finish();

    bool m_post_login { false };
};


}
}

#endif
