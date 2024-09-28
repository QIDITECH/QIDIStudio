#ifndef slic3r_GUI_Filament_web_hpp_
#define slic3r_GUI_Filament_web_hpp_

#include "Tabbook.hpp"
#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>

#include "nlohmann/json.hpp"
#include "slic3r/Utils/json_diff.hpp"

#include <map>
#include <vector>
#include <memory>
#include "Event.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "wxExtensions.hpp"


namespace Slic3r {
    namespace GUI {

        struct filament_file {
            std::string filepath;
            std::string filename;
            std::string size;
        };

        class FilamentPanel : public wxPanel
        {
        private:
            bool       m_web_init_completed = { false };
            bool       m_reload_already = { false };

            wxWebView* m_browser = { nullptr };
            wxString   m_project_home_url;
            wxString   m_root_dir;
            std::map<std::string, std::string> m_model_id_map;
            static inline int m_sequence_id = 8000;


        public:
            FilamentPanel(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
            ~FilamentPanel();


        };

    }
} // namespace Slic3r::GUI

#endif
