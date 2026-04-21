#pragma once

#define WIN32_LEAN_AND_MEAN 
#include <windows.h>
#include <string>
#include <ctime>
#include <vector>
#include <optional>

namespace net_util {
	
	struct HostPort {
		std::string Host;
		std::string Port;
	};

	HostPort* SplitHostPort(const std::string_view& hostPort, HostPort* result);
	std::string JoinHostPort(const std::string_view& host, const std::string_view& port);

	std::string MakeHttpDate(const std::time_t& t);

	std::vector<std::string> GetAllBindableAddresses(DWORD& errorCode);

    class AcceptLanguageMatcher {
    public:
        // candidates example: {"zh","zh-CN","en","en-US"}
        static std::optional<std::string> Match(
            std::string_view header,
            const std::vector<std::string>& candidates);
    private:
        struct Item {
            std::string tag;
            double q = 1.0;
            size_t order = 0;
        };
        static std::vector<Item> Parse(std::string_view header);
        static std::string Normalize(std::string s);
        static void ToLower(std::string& s);
        static void ToUpper(std::string& s);
        static std::optional<std::string>
            FindExact(const std::string& tag,
                const std::vector<std::string>& candidates);
        static std::optional<std::string>
            FindBase(const std::string& tag,
                const std::vector<std::string>& candidates);
    };
}