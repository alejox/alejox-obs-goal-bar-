// Network adapter: HTTPS with the system APIs (NSURLSession on macOS, WinHTTP
// on Windows, libcurl on Linux). OBS does not ship Qt's TLS plugins, so
// QtNetwork cannot do HTTPS inside OBS. Adapted from multi-chat's src/net.
// Every call blocks: use it only from worker threads.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace goalbar::net {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Response {
	int status = 0; // 0 = no response (see error)
	std::string body;
	std::string error;
	bool ok() const { return status >= 200 && status < 300; }
};

// Follows redirects. Bodies above 16 MiB fail with an error.
Response get(const std::string &url, const Headers &headers = {}, int timeout_ms = 10000);

// Desktop Chrome user agent for the current OS.
extern const char *const kUserAgent;

} // namespace goalbar::net
