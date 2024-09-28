#include "Tab.hpp"
#include "Project.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Format/qds_3mf.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>
#include <wx/wupdlock.h>
#include <wx/dataview.h>
#include <wx/tokenzr.h>
#include <wx/arrstr.h>
#include <wx/tglbtn.h>

#include <boost/log/trivial.hpp>

#include "wxExtensions.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "MainFrame.hpp"
#include <slic3r/GUI/Widgets/WebView.hpp>

namespace Slic3r {
    namespace GUI {

        

        FilamentPanel::FilamentPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style) : wxPanel(parent, id, pos, size, style)
        {
            m_project_home_url = wxString::Format("file://%s/web/filament/index.html", from_u8(resources_dir()));
            wxString strlang = wxGetApp().current_language_code_safe();
            if (strlang != "")
                m_project_home_url = wxString::Format("file://%s/web/filament/index.html?lang=%s", from_u8(resources_dir()), strlang);

            wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

            m_browser = WebView::CreateWebView(this, m_project_home_url);
            if (m_browser == nullptr) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("load web view of project page failed");
                return;
            }
            //m_browser->Hide();
            main_sizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));



            SetSizer(main_sizer);
            Layout();
            Fit();
        }

        FilamentPanel::~FilamentPanel() {}

        

    }
} // namespace Slic3r::GUI
