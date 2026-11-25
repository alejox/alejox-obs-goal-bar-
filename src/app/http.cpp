#include "app/http.hpp"

#include <algorithm>
#include <cctype>

namespace goalbar::app {
namespace {

std::string lowercase(std::string_view text)
{
	std::string out(text);
	std::transform(out.begin(), out.end(), out.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

std::string_view trim(std::string_view text)
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
		text.remove_prefix(1);
	while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
		text.remove_suffix(1);
	return text;
}

} // namespace

std::size_t find_request_end(std::string_view data)
{
	const std::size_t end = data.find("\r\n\r\n");
	return end == std::string_view::npos ? end : end + 4;
}

std::optional<HttpRequest> parse_http_request(std::string_view head)
{
	const std::size_t line_end = head.find("\r\n");
	if (line_end == std::string_view::npos)
		return std::nullopt;
	const std::string_view line = head.substr(0, line_end);
	const std::size_t first = line.find(' ');
	const std::size_t last = line.rfind(' ');
	if (first == std::string_view::npos || last == first)
		return std::nullopt;
	const std::string_view version = line.substr(last + 1);
	if (version != "HTTP/1.1" && version != "HTTP/1.0")
		return std::nullopt;
	HttpRequest request;
	request.method = std::string(line.substr(0, first));
	const std::string_view target = line.substr(first + 1, last - first - 1);
	if (target.empty() || target.front() != '/')
		return std::nullopt;
	const std::size_t question = target.find('?');
	request.path = std::string(target.substr(0, question));
	if (question != std::string_view::npos)
		request.query = std::string(target.substr(question + 1));
	request.keep_alive = version == "HTTP/1.1";

	std::string_view rest = head.substr(line_end + 2);
	while (!rest.empty()) {
		const std::size_t end = rest.find("\r\n");
		const std::string_view header = rest.substr(0, end);
		rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 2);
		const std::size_t colon = header.find(':');
		if (colon == std::string_view::npos)
			continue;
		const std::string name = lowercase(trim(header.substr(0, colon)));
		const std::string value = lowercase(trim(header.substr(colon + 1)));
		if (name == "host")
			request.host = value;
		else if (name == "connection")
			request.keep_alive = value.find("close") == std::string::npos &&
					     (request.keep_alive || value.find("keep-alive") != std::string::npos);
	}
	return request;
}

bool is_loopback_host(std::string_view host, unsigned port)
{
	const std::string suffix = ":" + std::to_string(port);
	for (const char *name : {"127.0.0.1", "localhost", "[::1]"}) {
		if (host == name || host == std::string(name) + suffix)
			return true;
	}
	return false;
}

std::string_view http_reason(int status)
{
	switch (status) {
	case 200:
		return "OK";
	case 204:
		return "No Content";
	case 400:
		return "Bad Request";
	case 403:
		return "Forbidden";
	case 404:
		return "Not Found";
	case 405:
		return "Method Not Allowed";
	case 431:
		return "Request Header Fields Too Large";
	case 503:
		return "Service Unavailable";
	default:
		return "Error";
	}
}

std::string format_http_response(const HttpResponse &response, bool head_only, bool keep_alive)
{
	std::string out = "HTTP/1.1 " + std::to_string(response.status) + " " + std::string(http_reason(response.status)) +
			  "\r\n";
	out += "Content-Type: " + response.content_type + "\r\n";
	out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
	out += "Cache-Control: no-store\r\n";
	out += "X-Content-Type-Options: nosniff\r\n";
	out += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
	for (const auto &[name, value] : response.headers)
		out += name + ": " + value + "\r\n";
	out += "\r\n";
	if (!head_only)
		out += response.body;
	return out;
}

} // namespace goalbar::app
