#ifndef slic3r_BonjourDialog_hpp_
#define slic3r_BonjourDialog_hpp_

#include <cstddef>
#include <memory>

#include <boost/asio/ip/address.hpp>

#include <wx/dialog.h>
#include <wx/string.h>

#include "libslic3r/PrintConfig.hpp"

class wxListView;
class wxStaticText;
class wxTimer;
class wxTimerEvent;
class address;

namespace Slic3r {

class Bonjour;
//B29
class Udp;
class BonjourReplyEvent;
//B29
class UdpReplyEvent;
class ReplySet;
//B29
class UdpReplySet;

class BonjourDialog: public wxDialog
{
public:
	BonjourDialog(wxWindow *parent, Slic3r::PrinterTechnology);
	BonjourDialog(BonjourDialog &&) = delete;
	BonjourDialog(const BonjourDialog &) = delete;
	BonjourDialog &operator=(BonjourDialog &&) = delete;
	BonjourDialog &operator=(const BonjourDialog &) = delete;
	~BonjourDialog();

	bool show_and_lookup();
	wxString get_selected() const;
private:
	wxListView *list;
	std::unique_ptr<ReplySet> replies;
	//B29
    std::unique_ptr<UdpReplySet> udp_replies;
	wxStaticText *label;
	std::shared_ptr<Bonjour> bonjour;
	//B29
    std::shared_ptr<Udp>  udp;
	std::unique_ptr<wxTimer> timer;
	unsigned timer_state;
	Slic3r::PrinterTechnology tech;

	virtual void on_reply(BonjourReplyEvent &);
	//B29
    virtual void on_udp_reply(UdpReplyEvent &);
	void on_timer(wxTimerEvent &);
    void on_timer_process();
};

class IPListDialog : public wxDialog
{
public:
	IPListDialog(wxWindow* parent, const wxString& hostname, const std::vector<boost::asio::ip::address>& ips, size_t& selected_index);
	IPListDialog(IPListDialog&&) = delete;
	IPListDialog(const IPListDialog&) = delete;
	IPListDialog& operator=(IPListDialog&&) = delete;
	IPListDialog& operator=(const IPListDialog&) = delete;
	~IPListDialog();

	virtual void EndModal(int retCode) wxOVERRIDE;
private:
	wxListView*		m_list;
	size_t&			m_selected_index;
};

}

#endif
