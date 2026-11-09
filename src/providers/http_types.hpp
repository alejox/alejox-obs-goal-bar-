// HTTP shapes shared by the provider adapters. The OBS adapter maps them onto
// the system network layer (src/net); the tests use fakes.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace goalbar {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpResult {
	int status = 0; // 0 = no response, see `error`
	std::string body;
	std::string error;
};

} // namespace goalbar
