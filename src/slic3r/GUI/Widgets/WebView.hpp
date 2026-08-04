#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <wx/webview.h>

class WebView
{
public:
    static wxWebView *CreateWebView(wxWindow *parent, wxString const &url);
    
    static void LoadUrl(wxWebView * webView, wxString const &url);

    static bool RunScript(wxWebView * webView, wxString const & msg);

    //y83
    static void SetTokenCookie(wxWebView * webView, wxString const & name, wxString const & value,
                               wxString const & domain, wxString const & path,
                               bool secure, bool httpOnly, double expires);

    static void RecreateAll();

    /*Find a user data path*/
    static wxString BuildEdgeUserDataPath();
};

#endif // !slic3r_GUI_WebView_hpp_
