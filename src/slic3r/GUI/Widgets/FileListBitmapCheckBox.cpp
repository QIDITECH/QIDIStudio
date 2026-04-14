#include "FileListBitmapCheckBox.hpp"

FileListBitmapCheckBox::FileListBitmapCheckBox(wxWindow *parent, int id)
    : wxBitmapToggleButton(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_on(this, "check_on", kFileListBitmapCheckboxScalablePxCnt)
    , m_half(this, "check_half", kFileListBitmapCheckboxScalablePxCnt)
    , m_off(this, "check_off", kFileListBitmapCheckboxScalablePxCnt)
    , m_on_disabled(this, "check_on_disabled", kFileListBitmapCheckboxScalablePxCnt)
    , m_half_disabled(this, "check_half_disabled", kFileListBitmapCheckboxScalablePxCnt)
    , m_off_disabled(this, "check_off_disabled", kFileListBitmapCheckboxScalablePxCnt)
    , m_on_focused(this, "check_on_focused", kFileListBitmapCheckboxScalablePxCnt)
    , m_half_focused(this, "check_half_focused", kFileListBitmapCheckboxScalablePxCnt)
    , m_off_focused(this, "check_off_focused", kFileListBitmapCheckboxScalablePxCnt)
{
	if (parent)
		SetBackgroundColour(parent->GetBackgroundColour());
	Bind(wxEVT_TOGGLEBUTTON, [this](auto &e) {
		m_half_checked = false;
		update();
		e.Skip();
	});
#ifdef __WXOSX__
	Bind(wxEVT_SET_FOCUS, &FileListBitmapCheckBox::updateBitmap, this);
	Bind(wxEVT_KILL_FOCUS, &FileListBitmapCheckBox::updateBitmap, this);
	Bind(wxEVT_ENTER_WINDOW, &FileListBitmapCheckBox::updateBitmap, this);
	Bind(wxEVT_LEAVE_WINDOW, &FileListBitmapCheckBox::updateBitmap, this);
#endif
	SetSize(m_on.GetBmpSize());
	SetMinSize(m_on.GetBmpSize());
	update();
}

void FileListBitmapCheckBox::SetValue(bool value)
{
	if (wxBitmapToggleButton::GetValue() != value) {
		wxBitmapToggleButton::SetValue(value);
		update();
	}
}

void FileListBitmapCheckBox::SetHalfChecked(bool value)
{
	m_half_checked = value;
	update();
}

void FileListBitmapCheckBox::Rescale()
{
	m_on.msw_rescale();
	m_half.msw_rescale();
	m_off.msw_rescale();
	m_on_disabled.msw_rescale();
	m_half_disabled.msw_rescale();
	m_off_disabled.msw_rescale();
	m_on_focused.msw_rescale();
	m_half_focused.msw_rescale();
	m_off_focused.msw_rescale();
	SetSize(m_on.GetBmpSize());
	SetMinSize(m_on.GetBmpSize());
	update();
}

void FileListBitmapCheckBox::update()
{
	SetBitmapLabel((m_half_checked ? m_half : GetValue() ? m_on : m_off).bmp());
	SetBitmapDisabled((m_half_checked ? m_half_disabled : GetValue() ? m_on_disabled : m_off_disabled).bmp());
#ifdef __WXMSW__
	SetBitmapFocus((m_half_checked ? m_half_focused : GetValue() ? m_on_focused : m_off_focused).bmp());
#endif
	SetBitmapCurrent((m_half_checked ? m_half_focused : GetValue() ? m_on_focused : m_off_focused).bmp());
#ifdef __WXOSX__
	wxCommandEvent e(wxEVT_UPDATE_UI);
	updateBitmap(e);
#endif
}

#ifdef __WXMSW__

FileListBitmapCheckBox::State FileListBitmapCheckBox::GetNormalState() const { return State_Normal; }

#endif

#ifdef __WXOSX__

bool FileListBitmapCheckBox::Enable(bool enable)
{
	bool result = wxBitmapToggleButton::Enable(enable);
	if (result) {
		m_disable = !enable;
		wxCommandEvent e(wxEVT_ACTIVATE);
		updateBitmap(e);
	}
	return result;
}

wxBitmap FileListBitmapCheckBox::DoGetBitmap(State which) const
{
	if (m_disable) {
		return wxBitmapToggleButton::DoGetBitmap(State_Disabled);
	}
	if (m_focus) {
		return wxBitmapToggleButton::DoGetBitmap(State_Current);
	}
	return wxBitmapToggleButton::DoGetBitmap(which);
}

void FileListBitmapCheckBox::updateBitmap(wxEvent &evt)
{
	evt.Skip();
	if (evt.GetEventType() == wxEVT_ENTER_WINDOW) {
		m_hover = true;
	} else if (evt.GetEventType() == wxEVT_LEAVE_WINDOW) {
		m_hover = false;
	} else {
		if (evt.GetEventType() == wxEVT_SET_FOCUS) {
			m_focus = true;
		} else if (evt.GetEventType() == wxEVT_KILL_FOCUS) {
			m_focus = false;
		}
		wxMouseEvent e;
		if (m_hover)
			OnEnterWindow(e);
		else
			OnLeaveWindow(e);
	}
}

#endif
