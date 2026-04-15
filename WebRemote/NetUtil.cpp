#include "pch.h"
#include <boost/url.hpp>
#include "NetUtil.h"

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
		if(host.empty()) {
			return nullptr;
		}
		result->Host = std::string(host.data(), host.size());
		auto port = authority->port();
		result->Port = result->Port = std::string(port.data(), port.size());
		return result;
	}
}
