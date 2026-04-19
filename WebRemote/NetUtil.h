#pragma once

#include <windows.h>
#include <string>
#include <ctime>
#include <vector>

namespace net_util {
	
	struct HostPort {
		std::string Host;
		std::string Port;
	};

	HostPort* SplitHostPort(const std::string_view& hostPort, HostPort* result);
	std::string JoinHostPort(const std::string_view& host, const std::string_view& port);

	std::string MakeHttpDate(const std::time_t& t);

	std::vector<std::string> GetAllBindableAddresses(DWORD& errorCode);
}