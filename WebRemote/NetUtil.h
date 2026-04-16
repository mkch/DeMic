#pragma once

#include <string>
#include <ctime>

namespace net_util {
	
	struct HostPort {
		std::string Host;
		std::string Port;
	};

	HostPort* SplitHostPort(const std::string_view& hostPort, HostPort* result);

	std::string MakeHttpDate(const std::time_t& t);
}