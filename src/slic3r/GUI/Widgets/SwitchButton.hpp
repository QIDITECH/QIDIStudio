#ifndef slic3r_GUI_SwitchButton_hpp_
#define slic3r_GUI_SwitchButton_hpp_

#include "../wxExtensions.hpp"
#include "StateColor.hpp"

#include <wx/tglbtn.h>
#include "Label.hpp"
#include "Button.hpp"

wxDECLARE_EVENT(wxCUSTOMEVT_SWITCH_POS, wxCommandEvent);
class SwitchButton : public wxBitmapToggleButton
{
public:
	SwitchButton(wxWindow * parent = NULL, wxWindowID id = wxID_ANY);

public:
	void SetLabels(wxString const & lbl_on, wxString const & lbl_off);

	void SetTextColor(StateColor const &color);

	void SetTextColor2(StateColor const &color);

    void SetTrackColor(StateColor const &color);

	void SetThumbColor(StateColor const &color);

	void SetValue(bool value) override;

	void Rescale();


protected:
	void update();

private:
	ScalableBitmap m_on;
	ScalableBitmap m_off;

	wxString labels[2];
    StateColor   text_color;
    StateColor   text_color2;
	StateColor   track_color;
	StateColor   thumb_color;
};

class SwitchBoard : public wxWindow
{
public:
    SwitchBoard(wxWindow *parent = NULL, wxString leftL = "", wxString right = "", wxSize size = wxDefaultSize);
    wxString leftLabel;
    wxString rightLabel;

	void updateState(wxString target);

	bool switch_left{false};
    bool switch_right{false};
    bool is_enable {true};

    void* client_data = nullptr;/*MachineObject* in StatusPanel*/

public:
    void Enable();
    void Disable();
    bool IsEnabled(){return is_enable;};

    void  SetClientData(void* data) { client_data = data; };
    void* GetClientData() { return client_data; };

    void SetAutoDisableWhenSwitch() { auto_disable_when_switch = true; };

protected:
    void paintEvent(wxPaintEvent& evt);
    void render(wxDC& dc);
    void doRender(wxDC& dc);
    void on_left_down(wxMouseEvent& evt);

private:
    bool auto_disable_when_switch = false;
};

class DeviceSwitchButton : public wxBitmapToggleButton
{
public:
	DeviceSwitchButton(wxWindow* parent = NULL, const wxString& label = wxEmptyString, wxWindowID id = wxID_ANY);

	void update_size();


public:
	void SetLabels(wxString const& lbl_on, wxString const& lbl_off);

	void SetTextColor(StateColor const& color);

	void SetTrackColor(StateColor const& color);

	void SetThumbColor(StateColor const& color);

	void SetValue(bool value) override;

	void Rescale();

	void SysColorChange();

	//B64
	void SetSize(int size);

private:
	void update();

private:
	ScalableBitmap m_on;
	ScalableBitmap m_off;
	//B64
	int m_size = 300;

	wxString labels[2];
	StateColor   text_color;
	StateColor   track_color;
	StateColor   thumb_color;
};

#endif // !slic3r_GUI_SwitchButton_hpp_
