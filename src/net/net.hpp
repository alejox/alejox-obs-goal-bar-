// Network adapter: HTTPS and WebSockets with the system APIs (NSURLSession on
// macOS, WinHTTP on Windows, libcurl on Linux). OBS does not ship Qt's TLS
// plugins, so QtNetwork cannot do HTTPS inside OBS. Adapted from multi-chat's
// src/net. Every call blocks: use it only from worker threads.
#pragma once

#include "core/cancel_token.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace goalbar::net {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Response {
	int status = 0; // 0 = no response (see error)
	std::string body;
	std::string error;
	Headers headers; // of the final response (after redirects)
	bool ok() const { return status >= 200 && status < 300; }
	// A response header's value (case-insensitive name), or empty.
	std::string header(const std::string &name) const;
};

// Follows redirects. Bodies above 16 MiB fail with an error. Cancelling
// `cancel` from another thread aborts the request promptly with the error
// "cancelled".
Response get(const std::string &url, const Headers &headers = {}, int timeout_ms = 10000,
	     CancelToken *cancel = nullptr);

// POST of an application/x-www-form-urlencoded `body`; same rules as get().
Response post_form(const std::string &url, const std::string &body, const Headers &headers = {},
		   int timeout_ms = 10000, CancelToken *cancel = nullptr);

// POST of `body` with the given Content-Type; same rules as get().
Response post(const std::string &url, const std::string &body, const std::string &content_type,
	      const Headers &headers = {}, int timeout_ms = 10000, CancelToken *cancel = nullptr);

inline constexpr const char *kCancelledError = "cancelled";

// A client WebSocket (text messages). receive() blocks until a whole message
// arrives or the connection ends; close() may be called from any thread and
// makes a pending receive() return Closed promptly.
class WebSocket {
public:
	enum class Result { Message, Closed };

	virtual ~WebSocket() = default;
	virtual Result receive(std::string &message) = 0;
	virtual bool send(const std::string &text) = 0;
	virtual bool send_binary(const std::string &data) = 0;
	virtual void close() = 0;
};

inline std::string Response::header(const std::string &name) const
{
	const auto lower = [](std::string s) {
		for (char &c : s)
			if (c >= 'A' && c <= 'Z')
				c = char(c - 'A' + 'a');
		return s;
	};
	const std::string want = lower(name);
	for (const auto &[key, value] : headers)
		if (lower(key) == want)
			return value;
	return {};
}

// Opens a ws:// or wss:// connection. Returns null and sets `error` on failure;
// on macOS a failed connection shows up as Closed on the first receive().
std::shared_ptr<WebSocket> connect_websocket(const std::string &url, const Headers &headers, std::string &error);

// Desktop Chrome user agent for the current OS.
extern const char *const kUserAgent;

} // namespace goalbar::net
