#include "pch.h"
#include "resource.h"
#include <mutex>
#include <set>

#include <boost/url.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/experimental/concurrent_channel.hpp>

#include <curl/curl.h>

#include "Server.h"
#include "HTTPServer.h"
#include "../Util.h"
#include "WebRemote.h"
#include "MessageWindow.h"
#include "NetUtil.h"
#include "VerificationCode.h"
#include "CryptoUtil.h"

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
    if (oldState != CurrentState) {
        ListenersMutex.unlock();
        co_return CurrentState;
    }
    auto ch = std::make_shared<StateChannelType>(executor);
    StateChangeEventListeners.push_back(ch);
    ListenersMutex.unlock();
    co_return co_await ch->async_receive(net::use_awaitable);
}

template<class Body>
static void setHttpLastModifiedHeader(http::response<Body>& response, const time_t& t) {
    response.set(http::field::last_modified, net_util::MakeHttpDate(t));
}

static std::unique_ptr<Server> server;

bool StopHTTPServer() {
    if (!server) {
        return false; // Not running.
    }
    server.reset();
    return true;
}

static std::span<const char> IndexResource;
static std::span<const char> VerifyResource;
static std::span<const std::byte> MutedPngResource;
static std::span<const std::byte> UnmutedPngResource;
static std::span<const std::byte> LoadingPngResource;

void InitHTTPServer() {
    IndexResource = LoadModuleResource<char>(hInstance, RT_HTML, MAKEINTRESOURCEW(IDR_SERVER_INDEX_HTML));
	VerifyResource = LoadModuleResource<char>(hInstance, RT_HTML, MAKEINTRESOURCEW(IDR_SERVER_VERIFY_HTML));
    MutedPngResource = LoadModuleResource<std::byte>(hInstance, L"PNG", MAKEINTRESOURCEW(IDB_MUTED));
    UnmutedPngResource = LoadModuleResource<std::byte>(hInstance, L"PNG", MAKEINTRESOURCEW(IDB_UNMUTED));
    LoadingPngResource = LoadModuleResource<std::byte>(hInstance, L"PNG", MAKEINTRESOURCEW(IDB_LOADING));
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
    if (lastModified > ifModSince) {
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

// handleModuleResource writes the specified resource to the response with the specified content type.
// It also handles the "If-Modified-Since" header and writes a `304 Not Modified` response if the resource
// has not been modified since the specified time.
//
// The contentType parameter can't be of type `const std::string&` because of warnning C26811.
// But the data referenced by contentType must be valid until the response is written, i.e. passing
// a string literal.
template<class SpanElementType>
static net::awaitable<void> handleModuleResource(Server::Conn& conn,  const std::string_view contentType, std::span<SpanElementType> res) {
    using status = http::status;

	// The last modified time of the resource.
    // Since the resource is embedded in the module, we can use the first call time as the last modified time.
    static const std::time_t lastModified = std::time(nullptr);

    const auto version = conn.RequestHeader().version();
    const auto method = conn.RequestHeader().method();

    if (method != http::verb::get && method != http::verb::head) {
        co_await conn.WriteResponse(http::response<http::empty_body>{ status::method_not_allowed, version});
        co_return;
    }
    if (co_await handleHttpIfModifiedSince(conn, lastModified)) { // HERE
        co_return;
    }
    http::response<http::buffer_body> response{ http::status::ok, version };
    setHttpLastModifiedHeader(response, lastModified);
    response.set(http::field::content_type, contentType);
    if (method == http::verb::head) {
        response.set(http::field::content_length, std::to_string(res.size_bytes()));
    } else {
        response.body().data = (void*)(res.data());
        response.body().size = res.size_bytes();
        response.body().more = false;
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

class SessionStore {
private:
    static std::string GenerateSessionId() {
        constexpr std::string_view candidateChars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";
        return GenerateRandomCode<char>(candidateChars, 32);
    }
private:
    std::mutex mutex;
    std::set<std::string> validSessions;
public:
    std::string CreateSession() {
        std::lock_guard<std::mutex> guard(mutex);
        std::string sessionId;
        do {
            sessionId = GenerateSessionId();
        } while (validSessions.find(sessionId) != validSessions.end());
        validSessions.insert(sessionId);
        return sessionId;
    }
    bool VerifySession(const std::string& sessionId) {
        std::lock_guard<std::mutex> guard(mutex);
        auto it = validSessions.find(sessionId);
        if (it == validSessions.end()) {
            return false;
        }
        return true;
    }
};

static SessionStore SessionStoreInstance;

static net::awaitable<void>handleVerifyCode(Server::Conn& conn, urls::url_view target) {
    using status = http::status;

    const auto version = conn.RequestHeader().version();
    const auto method = conn.RequestHeader().method();

    if (method != http::verb::get) {
        co_return co_await conn.WriteResponse(http::response<http::empty_body>{ status::method_not_allowed, version});
    }

    auto params = target.params();
    auto code = params.find("code");
    if (code == params.end() || !VerifyCode((*code).value)) {
        co_return co_await conn.WriteResponse(std::move(http::response<http::empty_body>{ status::bad_request, version }));
    }
    
	const auto sessionId = SessionStoreInstance.CreateSession();
    http::response<http::empty_body>response{ status::ok, version };
    response.set(http::field::set_cookie,
        std::format("sessionid={}; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400", sessionId)); // 86400s = 24h
    co_await conn.WriteResponse(std::move(response));
}

static bool VerifySession(const beast::http::request_header<>& requestHeader) {
    auto cookieHeader = requestHeader.find(http::field::cookie);
    if (cookieHeader == requestHeader.end()) {
        return false; // No cookie.
    }
    std::string cookieString = cookieHeader->value();
    std::string_view cookies = cookieString;
    std::string_view sessionId;
    while (!cookies.empty()) {
        auto pos = cookies.find(';');
        std::string_view cookie = cookies.substr(0, pos);
        auto eqPos = cookie.find('=');
        if (eqPos != std::string_view::npos) {
            std::string_view name = cookie.substr(0, eqPos);
            std::string_view value = cookie.substr(eqPos + 1);
            if (name == "sessionid") {
                sessionId = value;
                break;
            }
        }
        if (pos == std::string_view::npos) {
            break;
        }
        cookies.remove_prefix(pos + 1);
    }
    return SessionStoreInstance.VerifySession(std::string(sessionId));
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

    if (path == "/verify") {
        co_return co_await handleModuleResource(conn, "text/html", VerifyResource);
    }
    if (path == "/verify_code") {
        co_return co_await handleVerifyCode(conn, url);
    }

    if(!VerifySession(header)) {
        if (path == "/") {
			// Redirect to /verify if not verified when trying to access the index page.
            http::response<http::string_body> response{ http::status::found, header.version() };
            response.set(http::field::location, "/verify");
            response.prepare_payload();
            co_return co_await conn.WriteResponse(std::move(response));
        }
        // Not verified. Return 401 Unauthorized.
        co_await conn.WriteResponse(std::move(http::response<http::empty_body>{ http::status::unauthorized, header.version() }));
        co_return;
	}

    if (path == "/") {
        co_return co_await handleModuleResource(conn, "text/html", IndexResource);
    }
    if (path == "/res/muted.png") {
        co_return co_await handleModuleResource(conn, "image/png", MutedPngResource);
    }
    if (path == "/res/unmuted.png") {
        co_return co_await handleModuleResource(conn, "image/png", UnmutedPngResource);
    }
    if (path == "/res/loading.png") {
        co_return co_await handleModuleResource(conn, "image/png", LoadingPngResource);
    }
    if (path == "/wait_state_change") {
        co_return co_await handleWaitStateChange(conn, url);
    }
    if (path == "/toggle") {
        co_return co_await handleToggle(conn);
    }
    if(path == "/alladdr") {
        DWORD errCode = 0;
        auto addrs = net_util::GetAllBindableAddresses(errCode);
        http::response<http::string_body> response{http::status::ok, conn.RequestHeader().version()};
        response.set(http::field::content_type, "text/html");
        std::stringstream s;
        s << errCode << "<br>";
        for (auto addr : addrs) {
            s << addr << "<br>";
        }
        response.body() = s.str();
        response.prepare_payload();
        co_return co_await conn.WriteResponse(std::move(response));
    }

    co_await handleNotFound(conn);
};


HTTPServerResult StartHTTPServer(const std::string& host, const std::string& port, std::wstring& errorMessage) {
    if (server) {
        throw std::logic_error("Server already running");
    }

    try {
        server.reset(new Server(host, port, std::move(handler)));
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

bool HTTPServerRunning() {
    return server != nullptr;
}

std::string GetHTTPServerListenHost() {
	return server->GetListenHost();
}
unsigned short GetHTTPServerListenPort() {
    return server->GetListenPort();
}