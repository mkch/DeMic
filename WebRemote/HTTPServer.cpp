#include "pch.h"
#include "resource.h"
#include "HTTPServer.h"
#include <thread>
#include <vector>

#include <boost/url/url_view.hpp>

#include <boost/url.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>
#include <mutex>

#include "../Util.h"
#include "WebRemote.h"

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
        net::awaitable<void> WriteResponse(http::response<Body>& response) {
            if (responseWritten) {
                throw std::logic_error("Response already written");
            }
            response.set(http::field::server, "Demic Web Server");
            response.prepare_payload();
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
    Server(HTTPHandler&& h) : handler(std::move(h)) {
        serverThread = std::move(createThread([this] {
            try {
                tcp::endpoint endpoint{ tcp::v4(), 8080 };
                tcp::acceptor acceptor{ ioc, endpoint };

                net::co_spawn(ioc, do_listen(acceptor), net::detached);

                HOST_LOG(LevelDebug, L"HTTP Server started on on http://0.0.0.0:8080");

                ioc.run();

                HOST_LOG(LevelDebug, L"Server exited gracefully");
            } catch (const std::exception& e) {
                HOST_LOG(LevelError, std::format(L"Server error: {}", FromACP(e.what())).c_str());
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
    net::awaitable<void> do_listen(tcp::acceptor& acceptor) {
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
        } catch (const std::exception& e) {
            HOST_LOG(LevelDebug, std::format(L"Server error: {}", FromACP(e.what())).c_str());
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

void InitHTTPServer() {
    IndexResource = LoadModuleResource(hInstance, RT_HTML, MAKEINTRESOURCEW(IDR_SERVER_INDEX_HTML));
}

static net::awaitable<void> handleNotFound(Server::Conn& conn) {
    http::response<http::string_body> response{ http::status::not_found, conn.RequestHeader().version() };
    response.set(http::field::content_type, "text/html");
    co_await conn.WriteResponse(response);
}

static net::awaitable<void> handleIndex(Server::Conn& conn) {
    http::response<http::string_body> response{ http::status::ok, conn.RequestHeader().version() };
    response.set(http::field::server, "Demic Web Server");
    response.set(http::field::content_type, "text/html");
    response.body() = std::string_view(reinterpret_cast<const char*>(IndexResource.data()), IndexResource.size());
    co_await conn.WriteResponse(response);
}

static net::awaitable<void> handleWaitStateChange(Server::Conn& conn, urls::url_view url) {
    auto params = url.params();
	auto old_param_it = params.find("old");
	auto old_param = old_param_it != params.end() ? (*old_param_it).value : "";

    std::string body = co_await async_wait_state_change(conn.get_executor(), old_param);
    http::response<http::string_body> response{ http::status::ok, conn.RequestHeader().version() };
    response.set(http::field::server, "Demic Web Server");
    response.set(http::field::content_type, "text/html");
	response.body() = body;
    co_await conn.WriteResponse(response);
}


static net::awaitable<void> handler(Server::Conn& conn) {
    // Read Header
    auto header = conn.RequestHeader();
	const auto& headerTarget = header.target();
    // Routing
    auto target = urls::parse_origin_form(headerTarget);
    if (!target) {
		HOST_LOG(LevelDebug, std::format(L"Invalid request target: {}", FromUTF8(std::u8string_view((const char8_t*)headerTarget.data(), headerTarget.length())).c_str()).c_str());
        co_return;
    }
    auto url = target.value();
    if(url.path() == "/") {
        co_await handleIndex(conn);
        co_return;
    }
    if (url.path() == "/wait_state_change") {
        co_await handleWaitStateChange(conn, url);
        co_return;
    }

    co_await handleNotFound(conn);
};


bool StartHTTPServer() {
    if (server) {
        return false; // Already running.
    }
    server.reset(new Server(std::move(handler)));
    return true;
}