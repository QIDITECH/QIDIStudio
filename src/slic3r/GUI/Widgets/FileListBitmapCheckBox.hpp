#ifndef slic3r_GUI_FileListBitmapCheckBox_hpp_
#define slic3r_GUI_FileListBitmapCheckBox_hpp_

#include "../wxExtensions.hpp"

#include <wx/tglbtn.h>

//cj_3 ScalableBitmap px_cnt tier for file-list checkmarks; keep in sync with list layout FromDIP(kFileListCheckboxDip).
inline constexpr int kFileListBitmapCheckboxScalablePxCnt = 15;

//cj_3 Bitmap toggle using the tier above (keeps frame visible without changing CheckBox.hpp).
class FileListBitmapCheckBox : public wxBitmapToggleButton
{
public:
	explicit FileListBitmapCheckBox(wxWindow *parent, int id = wxID_ANY);

	void SetValue(bool value) override;

	void SetHalfChecked(bool value = true);

	void Rescale();

#ifdef __WXOSX__
	virtual bool Enable(bool enable = true) wxOVERRIDE;
#endif

protected:
#ifdef __WXMSW__
	virtual State GetNormalState() const wxOVERRIDE;
#endif

#ifdef __WXOSX__
	virtual wxBitmap DoGetBitmap(State which) const wxOVERRIDE;

	void updateBitmap(wxEvent &evt);

	bool m_disable = false;
	bool m_hover = false;
	bool m_focus = false;
#endif

private:
	void update();

private:
	ScalableBitmap m_on;
	ScalableBitmap m_half;
	ScalableBitmap m_off;
	ScalableBitmap m_on_disabled;
	ScalableBitmap m_half_disabled;
	ScalableBitmap m_off_disabled;
	ScalableBitmap m_on_focused;
	ScalableBitmap m_half_focused;
	ScalableBitmap m_off_focused;
	bool m_half_checked = false;
};

#endif // !slic3r_GUI_FileListBitmapCheckBox_hpp_
