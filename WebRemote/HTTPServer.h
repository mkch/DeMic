#pragma once

#include "NetUtil.h"
#include "../Util.h"
#include "WebRemote.h"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <boost/url/url_view.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

namespace urls = boost::urls;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;

using tcp = net::ip::tcp;

#undef max

#pragma comment(lib, "Crypt32.lib")

class AbstractServer {
public:
    virtual ~AbstractServer() = default;
public:
    virtual unsigned short GetListenPort() = 0;
};

template<typename T>
concept ServerStream =
    std::same_as<T, beast::tcp_stream>
    || std::same_as<T, beast::ssl_stream<beast::tcp_stream>>;

template<class T>
concept ServerPolicy =
    requires {
        typename T::StreamType;
    }
    && ServerStream<typename T::StreamType>
    && requires(T policy, tcp::socket&& socket) {
        { policy.CreateStream(std::move(socket)) } -> std::same_as<net::awaitable<typename T::StreamType>>;
    };

class HTTPPolicy {
public:
    using StreamType = beast::tcp_stream;
    net::awaitable<StreamType> CreateStream(tcp::socket&& socket) {
        co_return StreamType{ std::move(socket) };
    }
};

class HTTPSPolicy {
private:
	ssl::context ctx{ ssl::context::tls_server };
public:
	// certPemPath is the path to the certificate file in PEM format, and keyPemPath is the path to the private key file in PEM format.
	// All paths must be ACP encoding(NOT utf-8). ssl::context::use_xxx_file() has no wstring version.
    HTTPSPolicy(const std::string& certPemPath, const std::string& keyPemPath) {
        ctx.use_certificate_chain_file(certPemPath);
        ctx.use_private_key_file(keyPemPath, ssl::context::pem);
    }
public:
	using StreamType = beast::ssl_stream<beast::tcp_stream>;
    net::awaitable<StreamType> CreateStream(tcp::socket&& socket) {
        auto stream = StreamType{ std::move(socket), ctx};
        co_await stream.async_handshake(ssl::stream_base::server, net::use_awaitable);
        co_return stream;
    }
};


template<ServerPolicy PolicyType>
class Server : public AbstractServer {
private:
    // Read & write timeout.
    const static inline auto RW_TIMEOUT = std::chrono::seconds(30);
    const static uint32_t HEADER_LIMIT = 1024; // 1M
    const static uint32_t BODY_LIMIT = 10 * 1024 * 1024; // 10M
public:
    // Conn represents an HTTP connection.
    class Conn {
        friend Server;
    private:
        PolicyType::StreamType* const stream;
        beast::flat_buffer* const buffer;
    private:
        http::request_parser<http::empty_body>&& headerParser;
        http::request_header<> header;
        bool bodyRead = false; // Whether the body has been read.
        bool responseWritten = false; // Whether the response has been written.
        const bool requestKeepAlive; // Whether the request wants to keep the connection alive.
        bool responseKeepAlive = false; // Whether the response wants to keep the connection alive.
    private:
        Conn(Conn&) = delete;
        Conn(http::request_parser<http::empty_body>&& parser, bool keepAlive, PolicyType::StreamType* s, beast::flat_buffer* b)
            : headerParser(std::move(parser)), requestKeepAlive(keepAlive), stream(s), buffer(b) {
            header = headerParser.get();
        }
    private:
        // KeepAlive returns whether the connection should be kept alive after the response is written.
        bool KeepAlive() const {
            return requestKeepAlive && responseKeepAlive;
        }
        template<class Body>
        static void setHttpDateHeader(http::response<Body>& response) {
            response.set(http::field::date, getCachedHttpDate());
        }
        static const std::string getCachedHttpDate() {
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
    public:
        void SetExpiresAfter(const std::chrono::steady_clock::duration& d) {
            beast::get_lowest_layer(*stream).expires_after(d);
        }

        void SetExpiresNever() {
            beast::get_lowest_layer(*stream).expires_never();
        }

        // RequestHeader returns the HTTP request header.
        const http::request_header<>& RequestHeader() const {
            return header;
        }

        // ReadRequestBody reads the request body and returns it.
        template<class Body>
        net::awaitable<typename Body::value_type> ReadRequestBody() {
            if (bodyRead) {
                throw std::logic_error("Body already read");
            }
            bodyRead = true;
            beast::get_lowest_layer(*stream).expires_after(RW_TIMEOUT);
            http::request_parser<Body> bodyParser{ std::move(headerParser) };
            co_await http::async_read(*stream, *buffer, bodyParser, net::use_awaitable);
            co_return bodyParser.get().body();
        }

        // WriteResponse writes the response to the client.
        template<class Body>
        net::awaitable<void> WriteResponse(http::response<Body>&& response) {
            if (responseWritten) {
                throw std::logic_error("Response already written");
            }
            // Set "Server" header.
            response.set(http::field::server, "Demic Web Server");
            // Add "Date" header if not set.
            if (response.find(http::field::date) == response.end()) {
                setHttpDateHeader(response);
            }
            // Add "Content-Length" header if not set.
            if (response.find(http::field::content_length) == response.end()) {
                response.set(http::field::content_length, "0");
            }
            responseWritten = true;
            responseKeepAlive = response.keep_alive();
            beast::get_lowest_layer(*stream).expires_after(RW_TIMEOUT);
            co_await http::async_write(*stream, std::move(response), net::use_awaitable);
        }

        const auto get_executor() {
            return stream->get_executor();
        }
    };

    using HTTPHandler = std::function<net::awaitable<void>(Conn&)>;
private:
    HTTPHandler handler;
    net::io_context ioc{ 1 };
    PolicyType policy;
    std::unique_ptr<std::thread> serverThread;
    // Factory method to deduce the type of F.
    template<typename F>
    static auto createThread(F&& f) {
        return std::make_unique<std::thread>(std::forward<F>(f));
    }
private:
    std::string listenHost;
    unsigned short listenPort = 0;
public:
    virtual unsigned short GetListenPort() { return listenPort; }
public:
    class InvalidPortException : public std::invalid_argument {
    public:
        InvalidPortException() : std::invalid_argument("") {}
        InvalidPortException(const std::string& message) : std::invalid_argument(message) {}
    };
    class ResolveEndpointException : public std::invalid_argument {
    public:
        ResolveEndpointException() : std::invalid_argument("") {}
        ResolveEndpointException(const std::string& message) : std::invalid_argument(message) {}
    };
    class ListenException : public std::invalid_argument {
    public:
        ListenException(const std::string& message) : std::invalid_argument(message) {}
    };
    class BindException : public std::invalid_argument {
    public:
        BindException(const std::string& message) : std::invalid_argument(message) {}
    };
public:
    // Throws
    // InvalidPortException, NoEndpointException,
    // or boost::system::system_error.
    Server(const std::string& listen_host, const std::string& listen_port, HTTPHandler&& h, PolicyType&& serverPolicy = HTTPPolicy{}) 
        : handler(std::move(h)), policy(std::move(serverPolicy)) {
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


    virtual ~Server() {
        ioc.stop();
        if (serverThread && serverThread->joinable()) {
            serverThread->join();
        }
    }
private:
    net::awaitable<void> do_listen(tcp::acceptor&& acceptor) {
        for (;;) {
            tcp::socket socket = co_await acceptor.async_accept(net::use_awaitable);
            net::co_spawn(acceptor.get_executor(),
                do_session(std::move(socket)),
                net::detached);
        }
    }


    net::awaitable<void> do_session(tcp::socket&& socket) {
        try {
            beast::flat_buffer buffer;
            typename PolicyType::StreamType stream = co_await policy.CreateStream(std::move(socket));

            for (;;) {
                beast::get_lowest_layer(stream).expires_after(RW_TIMEOUT);
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
            beast::get_lowest_layer(stream).socket().shutdown(tcp::socket::shutdown_send, ec); // Ignore shutdown errors.
        } catch (const boost::system::system_error& e) {
            auto code = e.code();
            if (code != beast::condition::timeout
                && code != net::error::operation_aborted
                && code != net::error::connection_reset
                && code != net::error::connection_aborted) {
				// Log SSL error as warning since it may be caused by client disconnecting before SSL handshake completes,
				// or the certificate is not trusted by the client, which are common and not necessarily server errors.
				auto logLevel = code.category() == net::error::get_ssl_category() ? LevelWarn : LevelError;
                HOST_LOG(logLevel, std::format(L"Server session error: {} {}", e.code().value(), FromACP(e.code().message())).c_str());
            }
        }
    }
};

template<ServerPolicy PolicyType>
using ServerConn = typename Server<PolicyType>::Conn;