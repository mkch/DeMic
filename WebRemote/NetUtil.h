#pragma once

#include <string>

namespace net_util {
	
	struct HostPort {
		std::string Host;
		std::string Port;
	};

	HostPort* SplitHostPort(const std::string_view& hostPort, HostPort* result);
}