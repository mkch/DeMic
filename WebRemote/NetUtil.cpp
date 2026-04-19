#include "pch.h"
#include "NetUtil.h"

#include <boost/url.hpp>
#include <sstream>
#include <iomanip>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

namespace net_util {
    namespace urls = boost::urls;

    HostPort* SplitHostPort(const std::string_view& hostPort, HostPort* result) {
        if (hostPort.starts_with(":")) {
            // Empty host, all interfaces.
            result->Port = std::string(hostPort.substr(1));
            return result;
        }
        auto authority = urls::parse_authority(hostPort);
        if (!authority) {
            return nullptr;
        }
        auto host = authority->host();
        if (host.empty()) {
            return nullptr;
        }
        result->Host = std::string(host.data(), host.size());
        auto port = authority->port();
        result->Port = result->Port = std::string(port.data(), port.size());
        return result;
    }

    std::string JoinHostPort(const std::string_view& host, const std::string_view& port) {
        if (host.empty()) {
            // Empty host means all interfaces,
            // and "*" is a common notation for that in server listen address.
			return "*" + std::string(port); 
        }
        if(host.find(':') != std::string_view::npos) { // IPv6 literal
            return std::format("[{}]:{}", host, port);
		}
        return std::format("{}:{}", host, port);
	}

    std::string MakeHttpDate(const std::time_t& t) {
        std::tm gm{};
        gmtime_s(&gm, &t);
        std::ostringstream oss;
        oss << std::put_time(&gm, "%a, %d %b %Y %H:%M:%S GMT");
        return oss.str();
    }

    // 获取所有可用于 socket bind() 的本机 IP 地址
    // 如果发生错误，返回空 vector，并通过 errorCode 返回 WSAGetLastError()
    std::vector<std::string> GetAllBindableAddresses(DWORD& errorCode) {
        errorCode = 0;
        std::vector<std::string> bindableIPs;

        // 初始化 Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            errorCode = WSAGetLastError();
            return bindableIPs;
        }

        // 推荐缓冲区大小，减少 ERROR_BUFFER_OVERFLOW 概率
        ULONG bufferSize = 32768;  // 32KB
        PIP_ADAPTER_ADDRESSES pAddresses = nullptr;

        ULONG flags = GAA_FLAG_INCLUDE_PREFIX
            | GAA_FLAG_SKIP_ANYCAST
            | GAA_FLAG_SKIP_MULTICAST
            | GAA_FLAG_SKIP_DNS_SERVER;

        // 第一次调用获取所需缓冲区大小
        DWORD ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, nullptr, &bufferSize);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            pAddresses = (PIP_ADAPTER_ADDRESSES)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufferSize);
        } else if (ret != NO_ERROR) {
            errorCode = ret;
            WSACleanup();
            return bindableIPs;
        }

        if (pAddresses == nullptr) {
            pAddresses = (PIP_ADAPTER_ADDRESSES)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufferSize);
        }

        // 正式获取适配器信息
        ret = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, pAddresses, &bufferSize);
        if (ret != NO_ERROR) {
            errorCode = ret;
            if (pAddresses) HeapFree(GetProcessHeap(), 0, pAddresses);
            WSACleanup();
            return bindableIPs;
        }

        char ipString[INET6_ADDRSTRLEN] = {};

        for (PIP_ADAPTER_ADDRESSES adapter = pAddresses; adapter != nullptr; adapter = adapter->Next) {
            // 只考虑状态为 Up 的适配器（推荐）
            if (adapter->OperStatus != IfOperStatusUp)
                continue;

            for (PIP_ADAPTER_UNICAST_ADDRESS ua = adapter->FirstUnicastAddress;
                ua != nullptr;
                ua = ua->Next) {
                auto* sockaddr = ua->Address.lpSockaddr;
                if (sockaddr == nullptr)
                    continue;

                // 只处理 IPv4 和 IPv6
                if (sockaddr->sa_family != AF_INET && sockaddr->sa_family != AF_INET6)
                    continue;

                // 转换为字符串
                if (getnameinfo(sockaddr, ua->Address.iSockaddrLength,
                    ipString, sizeof(ipString),
                    nullptr, 0, NI_NUMERICHOST) != 0)
                    continue;

                std::string ipWStr = std::string(ipString, ipString + strlen(ipString));

                // 1. 排除 loopback
                if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                    auto* sin = (sockaddr_in*)ua->Address.lpSockaddr;
                    if (sin->sin_addr.S_un.S_addr == htonl(INADDR_LOOPBACK))
                        continue;
                } else if (ua->Address.lpSockaddr->sa_family == AF_INET6) {
                    auto* sin6 = (sockaddr_in6*)ua->Address.lpSockaddr;
                    if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr))
                        continue;
                    if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
                        continue;
                }

                // 排除 IPv6 临时地址（Privacy Extensions，地址会定期变化，不适合服务器长期绑定）
                // 如果你希望包含临时地址，请注释掉下面这行
                if ((sockaddr->sa_family == AF_INET6) && (ua->Flags & IP_ADAPTER_ADDRESS_TRANSIENT))
                    continue;
                bindableIPs.push_back(ipWStr);
            }
        }

        if (pAddresses) {
            HeapFree(GetProcessHeap(), 0, pAddresses);
        }

        WSACleanup();

        return bindableIPs;
    }
}