#include "pch.h"
#include "HTTPServer.h"
#include "../Util.h"
#include "WebRemote.h"

#include <thread>
#include <atomic>
#include <boost/url/url_view.hpp>

namespace urls = boost::urls;

#undef max

Server::Server(const std::string& listen_host, const std::string& listen_port, HTTPHandler&& h) : handler(std::move(h)) {
    net::ip::port_type portNumber = 0;
    // tcp::resolver does not reject invalid numberic port.
    // So we need to manually check whether the port is a valid port number.
    int32_t n = 0;
    auto r = std::from_chars(listen_port.data(), listen_port.data() + listen_port.size(), n);
    if (r.ec == std::error_code{}) { // has leading port number
        if (r.ptr != listen_port.data() + listen_port.size()) { // has extra characters
			throw InvalidPortException();
        }
        if (n < 0 || n > std::numeric_limits<net::ip::port_type>::max()) { // out of range
			throw InvalidPortException();
        }
		portNumber = (net::ip::port_type)n;
    } else if (r.ec == std::errc::result_out_of_range) { // out of range
		throw InvalidPortException();
    } else {
        try {
            auto hosts = tcp::resolver(ioc).resolve("0.0.0.0", listen_port);
            if (hosts.empty()) {
                throw InvalidPortException();
            }
            portNumber = hosts.begin()->endpoint().port();
        } catch (const boost::system::system_error& e) {
            throw InvalidPortException(e.code().message());
        }
    }

    if (portNumber == 0) {
        throw InvalidPortException();
    }

    tcp::endpoint endpoint;
    tcp::acceptor acceptor{ ioc };
    bool emptyHost = listen_host.empty();
    if (emptyHost) {
        // Empty host means listening on all interfaces.
		boost::system::error_code ec;
        // Try resove v6 first
        if (!tcp::resolver(ioc).resolve(tcp::endpoint(tcp::v6(), 0), ec).empty() && !ec) { // IPv6 is supported.
            endpoint = tcp::endpoint(tcp::v6(), (net::ip::port_type)portNumber);
            acceptor.open(endpoint.protocol());
            // Allow both v4 and v6 connections.
            acceptor.set_option(net::ip::v6_only(false));
		} else { // IPv6 is not supported, fallback to v4.
            endpoint = tcp::endpoint(tcp::v4(), (net::ip::port_type)portNumber);
            acceptor.open(endpoint.protocol());
        }
    } else {
        tcp::resolver::results_type endpoints;
        try {
            endpoints = tcp::resolver(ioc).resolve(listen_host, listen_port);
        } catch (const boost::system::system_error& e) {
            throw ResolveEndpointException(e.code().message());
        }
        if (endpoints.empty()) {
            throw ResolveEndpointException();
        }
        endpoint = endpoints.begin()->endpoint();
        acceptor.open(endpoint.protocol());
    }
    try {
        acceptor.bind(endpoint);
    } catch (const boost::system::system_error& e) {
        throw BindException(e.code().message());
    }
    try {
        acceptor.listen();
    } catch (const boost::system::system_error& e) {
        throw ListenException(e.code().message());
    }
	listenHost = listen_host;
	listenPort = endpoint.port();
    serverThread = std::move(createThread([this, acceptor = std::move(acceptor)]() mutable {
        try {
            net::co_spawn(ioc, do_listen(std::move(acceptor)), net::detached);
            ioc.run();
            HOST_LOG(LevelDebug, L"Server exited gracefully");
        } catch (const boost::system::system_error& e) {
            HOST_LOG(LevelError, std::format(L"Server thread error: {}", FromACP(e.code().message())).c_str());
        }
    }));

}

net::awaitable<void> Server::do_listen(tcp::acceptor&& acceptor) {
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
        net::co_spawn(acceptor.get_executor(),
            do_session(std::move(socket)),
            net::detached);
    }
}

net::awaitable<void> Server::do_session(tcp::socket&& socket) {
    try {
        beast::flat_buffer buffer;
        beast::tcp_stream stream(std::move(socket));

        for (;;) {
            stream.expires_after(RW_TIMEOUT);
            http::request_parser<http::empty_body> headerParser;
            headerParser.header_limit(HEADER_LIMIT);
            headerParser.body_limit(BODY_LIMIT);
            co_await http::async_read_header(stream, buffer, headerParser, net::use_awaitable);

            auto requestKeepAlive = headerParser.get().keep_alive();
            Conn conn{ std::move(headerParser), requestKeepAlive , &stream, &buffer };
            co_await handler(conn);
            if (!conn.KeepAlive()) {
                break;
            }
        }

        // Gracefully close the socket.
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_send, ec); // Ignore shutdown errors.
    } catch (const boost::system::system_error& e) {
        auto code = e.code();
        if (code != beast::condition::timeout 
            && code != net::error::operation_aborted
            && code != net::error::connection_reset
            && code != net::error::connection_aborted) {
            HOST_LOG(LevelError, std::format(L"Server session error: {} {}", e.code().value(), FromACP(e.code().message())).c_str());
        }
    }
}

const std::string Server::Conn::getCachedHttpDate() {
    struct cache {
        cache(time_t time, std::string&& date) : lastTime(time), httpDate(std::move(date)) {}
        time_t lastTime = 0;
        std::string httpDate;
    };
    static std::atomic<std::shared_ptr<cache>> cachePtr = std::make_shared<cache>(0, "");

    auto ptr = cachePtr.load(std::memory_order_acquire);
    auto now = std::time(nullptr);
    if (now != ptr->lastTime) {
        auto newPtr = std::make_shared<cache>(now, net_util::MakeHttpDate(now));
        if (!cachePtr.compare_exchange_strong(
            ptr, newPtr,
            std::memory_order_release,
            std::memory_order_acquire)) {
            ptr = cachePtr.load(std::memory_order_acquire);
        } else {
            ptr = newPtr;
        }
    }
    return ptr->httpDate;
}