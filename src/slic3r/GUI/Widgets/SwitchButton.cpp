#include "SwitchButton.hpp"
#include "Label.hpp"
#include "StaticBox.hpp"

#include "../wxExtensions.hpp"
#include "../Utils/MacDarkMode.hpp"

#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>

SwitchButton::SwitchButton(wxWindow* parent, wxWindowID id)
	: wxBitmapToggleButton(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_EXACTFIT)
	, m_on(this, "toggle_on", 16)
	, m_off(this, "toggle_off", 16)
    , text_color(std::pair{0xfffffe, (int) StateColor::Checked}, std::pair{0x6B6B6B, (int) StateColor::Normal})
	, track_color(0xD9D9D9)
    , thumb_color(std::pair{0x4479FB, (int) StateColor::Checked}, std::pair{0xD9D9D9, (int) StateColor::Normal})	// y96
{
	SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
	Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { update(); e.Skip(); });
	SetFont(Label::Body_12);
	Rescale();
}

void SwitchButton::SetLabels(wxString const& lbl_on, wxString const& lbl_off)
{
	labels[0] = lbl_on;
	labels[1] = lbl_off;
	Rescale();
}

void SwitchButton::SetTextColor(StateColor const& color)
{
	text_color = color;
}

void SwitchButton::SetTextColor2(StateColor const &color)
{
	text_color2 = color;
}

void SwitchButton::SetTrackColor(StateColor const& color)
{
	track_color = color;
}

void SwitchButton::SetThumbColor(StateColor const& color)
{
	thumb_color = color;
}

void SwitchButton::SetValue(bool value)
{
	if (value != GetValue())
		wxBitmapToggleButton::SetValue(value);
	update();
}

void SwitchButton::Rescale()
{
	if (labels[0].IsEmpty()) {
		m_on.msw_rescale();
		m_off.msw_rescale();
	}
	else {
        SetBackgroundColour(StaticBox::GetParentBackgroundColor(GetParent()));
#ifdef __WXOSX__
        auto scale = Slic3r::GUI::mac_max_scaling_factor();
        int BS = (int) scale;
#else
        constexpr int BS = 1;
#endif
		wxSize thumbSize;
		wxSize trackSize;
		wxClientDC dc(this);
#ifdef __WXOSX__
        dc.SetFont(dc.GetFont().Scaled(scale));
#endif
        wxSize textSize[2];
		{
			textSize[0] = dc.GetTextExtent(labels[0]);
			textSize[1] = dc.GetTextExtent(labels[1]);
		}
		float fontScale = 0;
		{
			thumbSize = textSize[0];
			auto size = textSize[1];
			if (size.x > thumbSize.x) thumbSize.x = size.x;
			else size.x = thumbSize.x;
			thumbSize.x += BS * 12;
			thumbSize.y += BS * 6;
			trackSize.x = thumbSize.x + size.x + BS * 10;
			trackSize.y = thumbSize.y + BS * 2;
            auto maxWidth = GetMaxWidth();
#ifdef __WXOSX__
            maxWidth *= scale;
#endif
			if (trackSize.x > maxWidth) {
                fontScale   = float(maxWidth) / trackSize.x;
                thumbSize.x -= (trackSize.x - maxWidth) / 2;
                trackSize.x = maxWidth;
			}
		}
		for (int i = 0; i < 2; ++i) {
			wxMemoryDC memdc(&dc);
#ifdef __WXMSW__
			wxBitmap bmp(trackSize.x, trackSize.y);
			memdc.SelectObject(bmp);
			memdc.SetBackground(wxBrush(GetBackgroundColour()));
			memdc.Clear();
#else
            wxImage image(trackSize);
            image.InitAlpha();
            memset(image.GetAlpha(), 0, trackSize.GetWidth() * trackSize.GetHeight());
            wxBitmap bmp(std::move(image));
            memdc.SelectObject(bmp);
#endif
            memdc.SetFont(dc.GetFont());
            if (fontScale) {
                memdc.SetFont(dc.GetFont().Scaled(fontScale));
                textSize[0] = memdc.GetTextExtent(labels[0]);
                textSize[1] = memdc.GetTextExtent(labels[1]);
			}
			auto state = i == 0 ? StateColor::Enabled : (StateColor::Checked | StateColor::Enabled);
            {
#ifdef __WXMSW__
				wxGCDC dc2(memdc);
#else
                wxDC &dc2(memdc);
#endif
				dc2.SetBrush(wxBrush(track_color.colorForStates(state)));
				dc2.SetPen(wxPen(track_color.colorForStates(state)));
                dc2.DrawRoundedRectangle(wxRect({0, 0}, trackSize), trackSize.y / 2);
				dc2.SetBrush(wxBrush(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled)));
				dc2.SetPen(wxPen(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled)));
				dc2.DrawRoundedRectangle(wxRect({ i == 0 ? BS : (trackSize.x - thumbSize.x - BS), BS}, thumbSize), thumbSize.y / 2);
			}
            memdc.SetTextForeground(text_color.colorForStates(state ^ StateColor::Checked));
            memdc.DrawText(labels[0], {BS + (thumbSize.x - textSize[0].x) / 2, BS + (thumbSize.y - textSize[0].y) / 2});
            memdc.SetTextForeground(text_color2.count() == 0 ? text_color.colorForStates(state) : text_color2.colorForStates(state));
            memdc.DrawText(labels[1], {trackSize.x - thumbSize.x - BS + (thumbSize.x - textSize[1].x) / 2, BS + (thumbSize.y - textSize[1].y) / 2});
			memdc.SelectObject(wxNullBitmap);
#ifdef __WXOSX__
            bmp = wxBitmap(bmp.ConvertToImage(), -1, scale);
#endif
			(i == 0 ? m_off : m_on).bmp() = bmp;
		}
	}
	SetSize(m_on.GetBmpSize());
	update();
}

void SwitchButton::update()
{
	SetBitmap((GetValue() ? m_on : m_off).bmp());
}



DeviceSwitchButton::DeviceSwitchButton(wxWindow* parent, const wxString& label, wxWindowID id)
	: m_on(this, "toggle_on", 28, 16)
	, m_off(this, "toggle_off", 28, 16)
	, text_color(std::pair{ 0x4479FB, (int)StateColor::Checked }, std::pair{ 0x6B6B6B, (int)StateColor::Normal })
	, track_color(0x333337)
	, thumb_color(std::pair{ 0x4479FB, (int)StateColor::Checked }, std::pair{ 0x333337, (int)StateColor::Normal })
{
	const long style = wxBORDER_NONE | wxBU_EXACTFIT | wxBU_LEFT;
	if (label.IsEmpty())
		wxBitmapToggleButton::Create(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, style);
	else {
#ifdef __WXGTK3__
		wxSize label_size = parent->GetTextExtent(label);
		wxSize def_size = wxSize(label_size.GetX() + 20, label_size.GetY());
#else
		wxSize def_size = wxDefaultSize;
#endif
		// Call Create() from wxToggleButton instead of wxBitmapToggleButton to allow add Label text under Linux
		wxToggleButton::Create(parent, id, label, wxDefaultPosition, def_size, style);
	}

#ifdef __WXMSW__
	if (parent) {
		SetBackgroundColour(parent->GetBackgroundColour());
		SetForegroundColour(parent->GetForegroundColour());
	}
#elif __WXGTK3__
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

	Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) {
		update();

		wxCommandEvent evt(wxEVT_CHECKBOX);
		evt.SetInt(int(GetValue()));
		wxPostEvent(this, evt);

		e.Skip();
		});
	Rescale();
}

void DeviceSwitchButton::update_size()
{
#ifndef __WXGTK3__
	wxSize best_sz = GetBestSize();
	wxBitmapToggleButton::SetSize(best_sz);
#endif
}

void DeviceSwitchButton::SetLabels(wxString const& lbl_on, wxString const& lbl_off)
{
	labels[0] = lbl_on;
	labels[1] = lbl_off;
	Rescale();
}

void DeviceSwitchButton::SetTextColor(StateColor const& color)
{
	text_color = color;
}

void DeviceSwitchButton::SetTrackColor(StateColor const& color)
{
	track_color = color;
}

void DeviceSwitchButton::SetThumbColor(StateColor const& color)
{
	thumb_color = color;
}

void DeviceSwitchButton::SetValue(bool value)
{
	if (value != GetValue())
		wxBitmapToggleButton::SetValue(value);
	update();
}

void DeviceSwitchButton::SetSize(int size)
{
	m_size = size;
	update();
	Rescale();
}

//B64
void DeviceSwitchButton::Rescale()
{
	if (!labels[0].IsEmpty()) {
#ifdef __WXOSX__
		auto scale = Slic3r::GUI::mac_max_scaling_factor();
		int BS = (int)scale;
#else
		constexpr int BS = 1;
#endif
		wxSize thumbSize;
		wxSize trackSize;
		wxClientDC dc(this);
#ifdef __WXOSX__
		dc.SetFont(dc.GetFont().Scaled(scale));
#endif
		wxSize textSize[2];
		{
			textSize[0] = dc.GetTextExtent(labels[0]);
			textSize[1] = dc.GetTextExtent(labels[1]);
		}
		{
			thumbSize = textSize[0];
			auto size = textSize[1];
			if (size.x > thumbSize.x) thumbSize.x = size.x;
			else size.x = thumbSize.x;
			//thumbSize.x += BS * 12 *10;
			//thumbSize.y += BS * 6;
			thumbSize.x = m_size / 2;
			thumbSize.y = 30;
			trackSize.x = m_size;
			trackSize.y = 35;	// y13

			auto maxWidth = GetMaxWidth();
#ifdef __WXOSX__
			maxWidth *= scale;
#endif
			if (trackSize.x > maxWidth) {
				thumbSize.x -= (trackSize.x - maxWidth) / 2;
				trackSize.x = maxWidth;
			}
		}

		for (int i = 0; i < 2; ++i) {
			wxMemoryDC memdc(&dc);
			wxBitmap bmp(trackSize.x, trackSize.y);
			memdc.SelectObject(bmp);
			memdc.SetBackground(wxBrush(GetBackgroundColour()));
			memdc.Clear();
			//memdc.SetFont(dc.GetFont());
			// y13
			memdc.SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));

			auto state = i == 0 ? StateColor::Enabled : (StateColor::Checked | StateColor::Enabled);
			{
#ifdef __WXMSW__
				wxGCDC dc2(memdc);
#else
				wxDC& dc2(memdc);
#endif
				dc2.SetBrush(wxBrush(track_color.colorForStates(state)));
				dc2.SetPen(wxPen(track_color.colorForStates(state)));
				dc2.DrawRectangle(wxRect({ 0, 0 }, trackSize));
				dc2.SetBrush(wxBrush(track_color.colorForStates(state)));
				dc2.SetPen(wxPen(track_color.colorForStates(state)));
				dc2.DrawRectangle(wxRect({ i == 0 ? BS : (trackSize.x - thumbSize.x - BS), BS }, thumbSize));

				// y18
				dc2.SetPen(wxPen(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled), 3));
				dc2.DrawLine(i == 0 ? 1 :
					trackSize.x / 2 + 2 * BS,
					thumbSize.y - 1, i == 0 ? trackSize.x / 2 - 2 * BS : trackSize.x,
					thumbSize.y - 1);
				dc2.SetPen(wxPen(wxColour(66, 66, 69), 1));
				dc2.DrawLine(trackSize.x / 2 , 1, trackSize.x / 2, thumbSize.y - 1);
				dc2.DrawLine(0, thumbSize.y, trackSize.x, thumbSize.y);

			}
			// y14
			memdc.SetTextForeground(text_color.colorForStates(state ^ StateColor::Checked));
			memdc.DrawText(labels[0], { (thumbSize.x - textSize[0].x) / 2 - 7, (trackSize.y - textSize[0].y) / 2 -  4 * BS});
			memdc.SetTextForeground(text_color.colorForStates(state));
			memdc.DrawText(labels[1], { trackSize.x / 2 + (thumbSize.x - textSize[1].x) / 2 - 4, (trackSize.y - textSize[1].y) / 2 - 4 * BS });

			//memdc.SetPen(wxPen(wxColour(68, 121, 251)));
   //         if (!GetValue())
   //             memdc.DrawLine(BS + BS * 10 * 7, thumbSize.y, BS + BS * 10 * 10, thumbSize.y);
   //         else
   //             memdc.DrawLine(trackSize.x - thumbSize.x + BS * 10 * 7 - BS * 4, thumbSize.y,
   //                            trackSize.x - thumbSize.x + BS * 10 * 10 - BS * 4, thumbSize.y);

			memdc.SelectObject(wxNullBitmap);
#ifdef __WXOSX__
			bmp = wxBitmap(bmp.ConvertToImage(), -1, scale);
#endif
			(i == 0 ? m_off : m_on).bmp() = bmp;
		}
	}

	update();
}

void DeviceSwitchButton::SysColorChange()
{
	m_on.msw_rescale();
	m_off.msw_rescale();

	update();
}

void DeviceSwitchButton::update()
{
	SetBitmap((GetValue() ? m_on : m_off).bmp());
	update_size();
}