#include "pch.h"
#include "resource.h"
#include "HTTPServer.h"
#include <thread>
#include <vector>
#include <mutex>

#include <boost/url/url_view.hpp>
#include <boost/url.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>

#include <curl/curl.h>

#include "../Util.h"
#include "WebRemote.h"
#include "MessageWindow.h"
#include "NetUtil.h"

namespace urls = boost::urls;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

template <typename T>
using Channel = net::experimental::concurrent_channel<void(boost::system::error_code, T)>;

static std::mutex ListenersMutex;
using StateType = std::string;
static const StateType Muted = "muted";
static const StateType Unmuted = "unmuted";
using StateChannelType = Channel<StateType>;
static std::vector<std::shared_ptr<StateChannelType>> StateChangeEventListeners;
static StateType CurrentState;

void NotifyStateChange(bool muted) {
    std::vector<std::shared_ptr<StateChannelType>> listeners;
    {
        CurrentState = muted ? Muted : Unmuted;
        std::lock_guard<std::mutex> guard(ListenersMutex);
		StateChangeEventListeners.swap(listeners);  
    }
    for (auto& listener : listeners) {
        boost::system::error_code ec;
        auto future = listener->async_send(ec, CurrentState, boost::asio::use_future);
        if (!ec) {
            future.get();
        } else  if (ec != boost::asio::experimental::error::channel_cancelled) {
            HOST_LOG(LevelError, std::format(L"Failed to notify state change: {}", FromACP(ec.message())).c_str());
        }
    }
}

void CancelStateChangeNotifications() {
    std::lock_guard<std::mutex> guard(ListenersMutex);
    for (auto& listener : StateChangeEventListeners) {
        listener->close();
    }
    StateChangeEventListeners.clear();
}

// async_wait_state_change waits for the microphone state to change
// to a different state other than oldState and returns the new state.
static net::awaitable<StateType> async_wait_state_change(beast::tcp_stream::executor_type executor, const StateType& oldState) {
    ListenersMutex.lock();
    if(oldState != CurrentState) {
		ListenersMutex.unlock();
        co_return CurrentState;
	}
    auto ch = std::make_shared<StateChannelType>(executor);
    StateChangeEventListeners.push_back(ch);
    ListenersMutex.unlock();
    co_return co_await ch->async_receive(net::use_awaitable);
}

// loadTime is the time when this module is loaded.
const std::time_t loadTime = std::time(nullptr);

static std::string make_http_date(const std::time_t& t) {
    std::tm gm{};
    gmtime_s(&gm, &t);
    std::ostringstream oss;
    oss << std::put_time(&gm, "%a, %d %b %Y %H:%M:%S GMT");
    return oss.str();
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
        auto newPtr = std::make_shared<cache>(now, make_http_date(now));
        // CAS
        if (!cachePtr.compare_exchange_strong(
            ptr, newPtr,
            std::memory_order_release,
            std::memory_order_acquire)) {
            // already updated by other thread, use the updated value.
            ptr = cachePtr.load(std::memory_order_acquire);
        } else {
            ptr = newPtr;
        }
    }
    return ptr->httpDate;
}

template<class Body>
static void setHttpDateHeader(http::response<Body>& response) {
    response.set(http::field::date, getCachedHttpDate());
}

template<class Body>
static void setHttpLastModifiedHeader(http::response<Body>& response, const time_t& t) {
    response.set(http::field::last_modified, make_http_date(t));
}


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
            if(response.find(http::field::date) == response.end()) {
				setHttpDateHeader(response);
			}
			// Add "Content-Length" header if not set.
            if(response.find(http::field::content_length) == response.end()) {
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
	Server(const std::string& listen_host, const std::string& listen_port, HTTPHandler&& h) : handler(std::move(h)) {
        if (listen_port.empty()) {
			throw InvalidPortException();
        }

        unsigned long portNumber = 0;
        try {
            portNumber = std::stoul(listen_port);
        } catch (const std::exception&) {
            throw InvalidPortException();
        }

        if (portNumber > 0xFFFF) {
            throw InvalidPortException();
        }

        tcp::endpoint endpoint;
        tcp::acceptor acceptor{ ioc };
        bool emptyHost = listen_port.empty();
        if (emptyHost) {
            // Empty host means listening on all interfaces.
            // Listen on v6 first, and allow both v4 and v6 later on by:
            // acceptor.set_option(net::ip::v6_only(false));
            endpoint = tcp::endpoint(tcp::v6(), (net::ip::port_type)portNumber);
            acceptor.open(endpoint.protocol());
            // Allow both v4 and v6 connections.
            acceptor.set_option(net::ip::v6_only(false));
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

        serverThread = std::move(createThread([this, acceptor = std::move(acceptor)]() mutable {
            try {
                net::co_spawn(ioc, do_listen(std::move(acceptor)), net::detached);
                ioc.run();
                HOST_LOG(LevelDebug, L"Server exited gracefully");
            } catch (const boost::system::system_error& e) {
                HOST_LOG(LevelError, std::format(L"Server error: {}", FromACP(e.code().message())).c_str());
            }
            }));
    }

    ~Server() {
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

    net::awaitable<void> do_session(tcp::socket socket) {
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
            HOST_LOG(LevelDebug, std::format(L"Server error: {}", FromACP(e.code().message())).c_str());
        }
    }
};

static std::unique_ptr<Server> server;

bool StopHTTPServer() {
    if (!server) {
		return false; // Not running.
	}
    server.reset();
    return true;
}

static std::span<const std::byte> IndexResource;
static std::span<const std::byte> MutedPngResource;
static std::span<const std::byte> UnmutedPngResource;
static std::span<const std::byte> LoadingPngResource;

void InitHTTPServer() {
    IndexResource = LoadModuleResource(hInstance, RT_HTML, MAKEINTRESOURCEW(IDR_SERVER_INDEX_HTML));
	MutedPngResource = LoadModuleResource(hInstance, L"PNG", MAKEINTRESOURCEW(IDB_MUTED));
	UnmutedPngResource = LoadModuleResource(hInstance, L"PNG", MAKEINTRESOURCEW(IDB_UNMUTED));
	LoadingPngResource = LoadModuleResource(hInstance, L"PNG", MAKEINTRESOURCEW(IDB_LOADING));
}

// handleHtppIfModifiedSince checks the "If-Modified-Since" header and
// writes a 304 Not Modified response if the resource has not been modified since the specified time.
// It returns true if a response has been written, and false otherwise.
static net::awaitable<bool> handleHttpIfModifiedSince(Server::Conn& conn, time_t lastModified) {
    const auto& requestHeader = conn.RequestHeader();
    auto ifModSinceHeader = requestHeader.find(http::field::if_modified_since);
    if (ifModSinceHeader == requestHeader.end()) {
        co_return false;
    }
    auto ifModSince = curl_getdate(std::string(ifModSinceHeader->value()).c_str(), NULL);
    if (ifModSince == -1) {
        HOST_LOG_WSTRING(LevelError, std::format(L"{}: invalid \"If-Modified-Since\" value {}", 
            FromUTF8(std::u8string_view((const char8_t*)requestHeader.target().data(), requestHeader.target().size())),
            FromUTF8(std::u8string_view((const char8_t*)ifModSinceHeader->value().data(), ifModSinceHeader->value().size()))));
		co_await conn.WriteResponse(http::response<http::string_body>{ http::status::bad_request, requestHeader.version() });
        co_return true;
    }
    if(lastModified > ifModSince) {
        // Modified
		co_return false;
    }
	// Write 304 Not Modified response.
    co_await conn.WriteResponse(http::response<http::string_body>{ http::status::not_modified, requestHeader.version() });
    co_return true;
}

static net::awaitable<void> handleNotFound(Server::Conn& conn) {
    http::response<http::empty_body> response{ http::status::not_found, conn.RequestHeader().version() };
    co_await conn.WriteResponse(std::move(response));
}

static net::awaitable<void> handleServerError(Server::Conn& conn, const std::string& reason) {
    http::response<http::string_body> response{ http::status::internal_server_error, conn.RequestHeader().version() };
    response.set(http::field::content_type, "text/html");
	response.body() = reason;
    co_await conn.WriteResponse(std::move(response));
}

static net::awaitable<void> handleIndex(Server::Conn& conn) {
    using status = http::status;
    
	const auto version = conn.RequestHeader().version(); 
    const auto method = conn.RequestHeader().method();

    if (method != http::verb::get && method != http::verb::head) {
        co_await conn.WriteResponse(http::response<http::empty_body>{ status::method_not_allowed, version});
        co_return;
    }
    if (co_await handleHttpIfModifiedSince(conn, loadTime)) {
        co_return;
    }
    http::response<http::string_body> response{ http::status::ok, version };
    setHttpLastModifiedHeader(response, loadTime);
    response.set(http::field::content_type, "text/html");
    if (method == http::verb::head) {
        response.set(http::field::content_length, std::to_string(IndexResource.size_bytes()));
    } else {
        response.body() = std::string_view(reinterpret_cast<const char*>(IndexResource.data()), IndexResource.size());
        response.prepare_payload();
    }
    co_await conn.WriteResponse(std::move(response));
}

static net::awaitable<void> handleWaitStateChange(Server::Conn& conn, urls::url_view url) {
    conn.SetExpiresNever(); // Long-polling connection. Never expires.

    auto params = url.params();
	auto old_param_it = params.find("old");
	auto old_param = old_param_it != params.end() ? (*old_param_it).value : "";

    std::string body = co_await async_wait_state_change(conn.get_executor(), old_param);
    http::response<http::string_body> response{ http::status::ok, conn.RequestHeader().version() };
    response.set(http::field::content_type, "text/html");
	response.body() = body;
    response.prepare_payload();
    co_await conn.WriteResponse(std::move(response));
}

static net::awaitable<void> handlePNG(Server::Conn& conn, std::span<const std::byte> res) {
    using status = http::status;

    const auto version = conn.RequestHeader().version();
    const auto method = conn.RequestHeader().method();

    if (method != http::verb::get && method != http::verb::head) {
        co_return co_await conn.WriteResponse(http::response<http::empty_body>{ status::method_not_allowed, version});
    }
    if (co_await handleHttpIfModifiedSince(conn, loadTime)) {
        co_return;
    }
    http::response<http::buffer_body> response{ http::status::ok, version };
    setHttpLastModifiedHeader(response, loadTime);
    response.set(http::field::content_type, "image/png");
    if (method == http::verb::head) {
		response.set(http::field::content_length, std::to_string(res.size_bytes()));
    } else {
        response.set(http::field::content_type, "image/png");
        response.body().data = const_cast<std::byte*>(res.data());
        response.body().size = res.size();
        response.body().more = false;
        response.prepare_payload();
    }
    co_await conn.WriteResponse(std::move(response));
}

static net::awaitable<void>handleToggle(Server::Conn& conn) {
    using status = http::status;

    const auto version = conn.RequestHeader().version();
    const auto method = conn.RequestHeader().method();

    if (method != http::verb::get) {
        co_return co_await conn.WriteResponse(http::response<http::empty_body>{ status::method_not_allowed, version});
    }

    auto ok = PostToggle();
    if (!ok) {
		co_return co_await handleServerError(conn, "Failed to toggle microphone state.");
    }
	co_await conn.WriteResponse(std::move(http::response<http::empty_body>{ http::status::ok, version }));
}


static net::awaitable<void> handler(Server::Conn& conn) {
    // Read Header
    const auto& header = conn.RequestHeader();
	const auto& headerTarget = header.target();
    // Routing
    auto target = urls::parse_origin_form(headerTarget);
    if (!target) {
		HOST_LOG(LevelDebug, std::format(L"Invalid request target: {}", FromUTF8(std::u8string_view((const char8_t*)headerTarget.data(), headerTarget.length())).c_str()).c_str());
        co_return;
    }
    auto url = target.value();
	const auto path = url.path();
    if(path == "/") {
        co_return co_await handleIndex(conn);
    }
    if (path == "/wait_state_change") {
        co_return co_await handleWaitStateChange(conn, url);
    }
    if(path == "/res/muted.png") {
        co_return co_await handlePNG(conn, MutedPngResource);
	}
    if (path == "/res/unmuted.png") {
        co_return co_await handlePNG(conn, UnmutedPngResource);
    }
    if (path == "/res/loading.png") {
        co_return co_await handlePNG(conn, LoadingPngResource);
    }
    if (path == "/toggle") {
		co_return co_await handleToggle(conn);
    }

    co_await handleNotFound(conn);
};


HTTPServerResult StartHTTPServer(const std::wstring& address, std::wstring& errorMessage) {
    if (server) {
		throw std::logic_error("Server already running");
    }
    auto u8addr = ToUTF8(address);
    auto addr = std::string_view((const char*)u8addr.data(), u8addr.size());

    net_util::HostPort hostPort{};
    if (!net_util::SplitHostPort(addr, &hostPort)) {
        return SERVER_INVALID_ADDRESS_FORMAT;
    }
    try {
        server.reset(new Server(hostPort.Host, hostPort.Port, std::move(handler)));
    } catch (const Server::InvalidPortException& e) {
        errorMessage = FromACP(e.what());
        return SERVER_INVALID_PORT;
    } catch (const Server::ResolveEndpointException& e) {
        errorMessage = FromACP(e.what());
        return SERVER_RESOLVE_ENDPOINT;
    } catch (const Server::BindException& e) {
        errorMessage = FromACP(e.what());
        return SERVER_BIND_ERROR;
    } catch (const Server::ListenException& e) {
		errorMessage = FromACP(e.what());
        return SERVER_LISTEN_ERROR; 
    } catch (const boost::system::system_error& e) {
        errorMessage = FromACP(e.code().message());
        return SERVER_ERROR;
	}
    return SERVER_OK;
}