// Overlay app, pure part of its local web server: parses one HTTP/1.1
// request head and writes one response. No sockets here (see http_server).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace goalbar::app {

// A request head bigger than this is refused (the overlay's requests are tiny).
inline constexpr std::size_t kMaxRequestHead = 8192;

struct HttpRequest {
	std::string method; // "GET", "HEAD", …
	std::string path;   // without the query, e.g. "/goal-bar/frame.png"
	std::string query;  // after "?", without it
	std::string host;   // the Host header, lowercase
	bool keep_alive = true;
};

struct HttpResponse {
	int status = 200;
	std::string content_type = "text/plain; charset=utf-8";
	std::string body;
	std::vector<std::pair<std::string, std::string>> headers; // extra ones
};

// Where a request head ends ("\r\n\r\n"), or npos while incomplete.
std::size_t find_request_end(std::string_view data);

// Parses a request head (up to and including the blank line); nullopt when
// it is not a valid HTTP/1.x request line.
std::optional<HttpRequest> parse_http_request(std::string_view head);

// Whether `host` (a Host header) names this machine's loopback on `port`:
// 127.0.0.1, localhost or [::1]. Anything else is refused, so that a web page
// cannot reach the app through a DNS name that points to 127.0.0.1.
bool is_loopback_host(std::string_view host, unsigned port);

// The bytes to send. HEAD gets the headers only.
std::string format_http_response(const HttpResponse &response, bool head_only, bool keep_alive);

std::string_view http_reason(int status);

// The value of `name` in a query string ("a=1&b=2"), or "" (no decoding: the
// overlay only sends numbers and letters).
std::string query_param(std::string_view query, std::string_view name);

} // namespace goalbar::app
