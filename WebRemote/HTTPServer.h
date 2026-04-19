#pragma once


#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <string>

#include "NetUtil.h"

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

class Server {
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
        beast::tcp_stream* const stream;
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
        Conn(http::request_parser<http::empty_body>&& parser, bool keepAlive, beast::tcp_stream* s, beast::flat_buffer* b)
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
        static const std::string getCachedHttpDate();
    public:
        void SetExpiresAfter(const std::chrono::steady_clock::duration& d) {
            stream->expires_after(d);
        }

        void SetExpiresNever() {
            stream->expires_never();
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
            stream->expires_after(RW_TIMEOUT);
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
            stream->expires_after(RW_TIMEOUT);
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
    std::unique_ptr<std::thread> serverThread;
    // Factory method to deduce the type of F.
    template<typename F>
    static auto createThread(F&& f) {
        return std::make_unique<std::thread>(std::forward<F>(f));
    }
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
    Server(const std::string& listen_host, const std::string& listen_port, HTTPHandler&& h);

    ~Server() {
        ioc.stop();
        if (serverThread && serverThread->joinable()) {
            serverThread->join();
        }
    }
private:
    net::awaitable<void> do_listen(tcp::acceptor&& acceptor);
    net::awaitable<void> do_session(tcp::socket socket);
};
