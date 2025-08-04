#ifndef slic3r_Udp_hpp_
#define slic3r_Udp_hpp_

#include <cstdint>
#include <memory>
#include <string>
#include <set>
#include <unordered_map>
#include <functional>

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/shared_ptr.hpp>

#include <boost/array.hpp>

namespace Slic3r {



struct UdpReply
{
	typedef std::unordered_map<std::string, std::string> TxtData;

	boost::asio::ip::address ip;
	uint16_t port;
	std::string service_name;
	std::string hostname;
	std::string full_address;

	//TxtData txt_data;

	UdpReply() = delete;
	UdpReply(boost::asio::ip::address ip,
		uint16_t port,
		std::string service_name,
		std::string hostname);

	std::string path() const;

	bool operator==(const UdpReply &other) const;
	bool operator<(const UdpReply &other) const;
};

std::ostream& operator<<(std::ostream &, const UdpReply &);

/// Udp lookup performer
class Udp : public std::enable_shared_from_this<Udp> {
private:
	struct priv;
public:
	typedef std::shared_ptr<Udp> Ptr;
	typedef std::function<void(UdpReply &&)> ReplyFn;
	typedef std::function<void()> CompleteFn;
	typedef std::function<void(const std::vector<UdpReply>&)> ResolveFn;
	typedef std::set<std::string> TxtKeys;

	Udp(std::string service);
	Udp(Udp &&other);
	~Udp();

	// Set requested service protocol, "tcp" by default
	Udp& set_protocol(std::string protocol);
	// Set which TXT key-values should be collected
	// Note that "path" is always collected
	Udp& set_txt_keys(TxtKeys txt_keys);
	Udp& set_timeout(unsigned timeout);
	Udp& set_retries(unsigned retries);
	// ^ Note: By default there is 1 retry (meaning 1 broadcast is sent).
	// Timeout is per one retry, ie. total time spent listening = retries * timeout.
	// If retries > 1, then care needs to be taken as more than one reply from the same service may be received.
	
	// sets hostname queried by resolve()
	Udp& set_hostname(const std::string& hostname);

	Udp& on_udp_reply(ReplyFn fn);
	Udp& on_complete(CompleteFn fn);

	Udp& on_resolve(ResolveFn fn);
	// lookup all devices by given TxtKeys
	// each correct reply is passed back in ReplyFn, finishes with CompleteFn
	Ptr lookup();
	// performs resolving of hostname into vector of ip adresses passed back by ResolveFn
	// needs set_hostname and on_resolve to be called before.
	Ptr resolve();
	// resolve on the current thread
	void resolve_sync();
private:
	std::unique_ptr<priv> p;
};

struct UdpRequest
{
	static const boost::asio::ip::address_v4 MCAST_IP4;
	static const boost::asio::ip::address_v6 MCAST_IP6;
	static const uint16_t MCAST_PORT;

	std::vector<char> m_data;

	static boost::optional<UdpRequest> make_PTR(const std::string& service, const std::string& protocol);
	static boost::optional<UdpRequest> make_A(const std::string& hostname);
	static boost::optional<UdpRequest> make_AAAA(const std::string& hostname);
private:
	UdpRequest(std::vector<char>&& data) : m_data(std::move(data)) {}
};


class LookupUdpSocket;
class ResolveUdpUdpSocket;

// Session is created for each async_receive of socket. On receive, its handle_receive method is called (Thru io_service->post).
// ReplyFn is called if correct datagram was received. 
class UdpUdpSession 
{
public:
	UdpUdpSession(Udp::ReplyFn rfn);
    virtual void                    handle_receive(const boost::system::error_code &error, size_t bytes, std::string pData) = 0;
	std::vector<char>				buffer;
	boost::asio::ip::udp::endpoint	remote_endpoint;
protected:
	Udp::ReplyFn				replyfn;
};
typedef std::shared_ptr<UdpUdpSession> SharedUdpSession;
// Session for LookupUdpSocket
class LookupUdpSession : public UdpUdpSession
{
public:
    LookupUdpSession(const LookupUdpSocket *sckt, Udp::ReplyFn rfn) : UdpUdpSession(rfn), socket(sckt) {}
    void handle_receive(const boost::system::error_code &error, size_t bytes, std::string pData) override;

protected:
    // const pointer to socket to get needed data as txt_keys etc.
    const LookupUdpSocket *socket;
};
// Session for ResolveUdpUdpSocket
class ResolveUdpSession : public UdpUdpSession 
{
public:
	ResolveUdpSession(const ResolveUdpUdpSocket* sckt, Udp::ReplyFn rfn) : UdpUdpSession(rfn), socket(sckt) {}
    void handle_receive(const boost::system::error_code &error, size_t bytes, std::string pData) override;

protected:
	// const pointer to seocket to get hostname during handle_receive
	const ResolveUdpUdpSocket* socket;
};

// Udp socket, starts receiving answers after first send() call until io_service is stopped.
class UdpUdpSocket
{
public:
	// Two constructors: 1st is with interface which must be resolved before calling this
	UdpUdpSocket(Udp::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, const boost::asio::ip::address& interface_address
		, std::shared_ptr< boost::asio::io_service > io_service);

	UdpUdpSocket(Udp::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, std::shared_ptr< boost::asio::io_service > io_service);

	void send();
	void async_receive();
	void cancel() { socket.cancel(); }
protected:
	void receive_handler(SharedUdpSession session, const boost::system::error_code& error, size_t bytes);
	virtual SharedUdpSession create_session() const = 0;

	Udp::ReplyFn								replyfn;
	boost::asio::ip::address					    multicast_address;
	boost::asio::ip::udp::socket					socket;
	boost::asio::ip::udp::endpoint					mcast_endpoint;
	std::shared_ptr< boost::asio::io_service >	io_service;
	std::vector<UdpRequest>						requests;
    boost::array<char, 81920> m_recvBuf;
};

class LookupUdpSocket : public UdpUdpSocket
{
public:
	LookupUdpSocket(Udp::TxtKeys txt_keys
		, std::string service
		, std::string service_dn
		, std::string protocol
		, Udp::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, const boost::asio::ip::address& interface_address
		, std::shared_ptr< boost::asio::io_service > io_service)
		: UdpUdpSocket(replyfn, multicast_address, interface_address, io_service)
		, txt_keys(txt_keys)
		, service(service)
		, service_dn(service_dn)
		, protocol(protocol)
	{
		assert(!service.empty() && replyfn);
		create_request();
	}

	LookupUdpSocket(Udp::TxtKeys txt_keys
		, std::string service
		, std::string service_dn
		, std::string protocol
		, Udp::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, std::shared_ptr< boost::asio::io_service > io_service)
		: UdpUdpSocket(replyfn, multicast_address, io_service)
		, txt_keys(txt_keys)
		, service(service)
		, service_dn(service_dn)
		, protocol(protocol)
	{
		assert(!service.empty() && replyfn);
		create_request();
	}

	const Udp::TxtKeys		get_txt_keys()   const { return txt_keys; }
	const std::string			get_service()    const { return service; }
	const std::string			get_service_dn() const { return service_dn; }

protected:
	SharedUdpSession create_session() const override;
	void		  create_request()
	{
		requests.clear();
		// create PTR request
		if (auto rqst = UdpRequest::make_PTR(service, protocol); rqst)
			requests.push_back(std::move(rqst.get()));
	}
	boost::optional<UdpRequest> request;
	Udp::TxtKeys				txt_keys;
	std::string						service;
	std::string						service_dn;
	std::string						protocol;
};

class ResolveUdpUdpSocket : public UdpUdpSocket
{
public:
	ResolveUdpUdpSocket(const std::string& hostname
		, Udp::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, const boost::asio::ip::address& interface_address
		, std::shared_ptr< boost::asio::io_service > io_service)
		: UdpUdpSocket(replyfn, multicast_address, interface_address, io_service)
		, hostname(hostname)

	{
		assert(!hostname.empty() && replyfn);
		create_requests();
	}

	ResolveUdpUdpSocket(const std::string& hostname
		, Udp::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, std::shared_ptr< boost::asio::io_service > io_service)
		: UdpUdpSocket(replyfn, multicast_address, io_service)
		, hostname(hostname)

	{
		assert(!hostname.empty() && replyfn);
		create_requests();
	}

	std::string get_hostname() const { return hostname; }
protected:
	SharedUdpSession create_session() const override;
	void		  create_requests()
	{
		requests.clear();
		// UdpRequest::make_A / AAAA is now implemented to add .local correctly after the hostname.
			// If that is unsufficient, we need to change make_A / AAAA and pass full hostname.
		std::string trimmed_hostname = hostname;
		if (size_t dot_pos = trimmed_hostname.find_first_of('.'); dot_pos != std::string::npos)
			trimmed_hostname = trimmed_hostname.substr(0, dot_pos);
		if (auto rqst = UdpRequest::make_A(trimmed_hostname); rqst)
			requests.push_back(std::move(rqst.get()));

		trimmed_hostname = hostname;
		if (size_t dot_pos = trimmed_hostname.find_first_of('.'); dot_pos != std::string::npos)
			trimmed_hostname = trimmed_hostname.substr(0, dot_pos);
		if (auto rqst = UdpRequest::make_AAAA(trimmed_hostname); rqst)
			requests.push_back(std::move(rqst.get()));
	}

	std::string hostname;
};

}

#endif
