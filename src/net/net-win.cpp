// Network on Windows with WinHTTP (HTTPS and WebSockets, Windows 8+).

#include "net/net.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <mutex>

namespace goalbar::net {

const char *const kUserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
			       "(KHTML, like Gecko) Chrome/130.0 Safari/537.36";

namespace {

constexpr size_t kMaxBody = 16 * 1024 * 1024;

std::wstring widen(const std::string &s)
{
	if (s.empty())
		return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring out(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
	return out;
}

std::string last_error(const char *what)
{
	return std::string(what) + " (WinHTTP " + std::to_string(GetLastError()) + ")";
}

struct Handle {
	HINTERNET h = nullptr;
	explicit Handle(HINTERNET handle) : h(handle) {}
	Handle(const Handle &) = delete;
	Handle &operator=(const Handle &) = delete;
	~Handle()
	{
		if (h)
			WinHttpCloseHandle(h);
	}
	operator HINTERNET() const { return h; }
	HINTERNET release()
	{
		HINTERNET out = h;
		h = nullptr;
		return out;
	}
};

// One session for the whole plugin (WinHTTP sessions are thread-safe).
HINTERNET session()
{
	static HINTERNET s = [] {
		const std::wstring ua = widen(kUserAgent);
		HINTERNET h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
					  WINHTTP_NO_PROXY_BYPASS, 0);
		if (!h) // older Windows without automatic proxy
			h = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS, 0);
		if (h) {
			DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
			WinHttpSetOption(h, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
			DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
			if (!WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
				protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
				WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
			}
		}
		return h;
	}();
	return s;
}

struct Url {
	std::wstring host, path;
	INTERNET_PORT port = 0;
	bool secure = false;
};

bool parse_url(std::string url, Url &out)
{
	// WinHttpCrackUrl knows neither ws:// nor wss://.
	if (url.rfind("wss://", 0) == 0)
		url = "https://" + url.substr(6);
	else if (url.rfind("ws://", 0) == 0)
		url = "http://" + url.substr(5);
	const std::wstring w = widen(url);
	URL_COMPONENTS uc = {};
	uc.dwStructSize = sizeof(uc);
	uc.dwSchemeLength = (DWORD)-1;
	uc.dwHostNameLength = (DWORD)-1;
	uc.dwUrlPathLength = (DWORD)-1;
	uc.dwExtraInfoLength = (DWORD)-1;
	if (!WinHttpCrackUrl(w.c_str(), (DWORD)w.size(), 0, &uc))
		return false;
	out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
	out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
	if (uc.lpszExtraInfo)
		out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
	if (out.path.empty())
		out.path = L"/";
	out.port = uc.nPort;
	out.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
	return true;
}

std::wstring header_block(const Headers &headers)
{
	std::string block;
	for (const auto &[name, value] : headers)
		block += name + ": " + value + "\r\n";
	return widen(block);
}

std::string narrow(const std::wstring &w)
{
	if (w.empty())
		return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string out(n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), n, nullptr, nullptr);
	return out;
}

Headers response_headers(HINTERNET request)
{
	Headers out;
	DWORD size = 0;
	WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
			    WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0)
		return out;
	std::wstring raw(size / sizeof(wchar_t), L'\0');
	if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(),
				 &size, WINHTTP_NO_HEADER_INDEX))
		return out;
	raw.resize(size / sizeof(wchar_t));
	const std::string text = narrow(raw);
	size_t pos = text.find("\r\n"); // the first line is "HTTP/1.1 200 OK"
	while (pos != std::string::npos) {
		const size_t start = pos + 2;
		pos = text.find("\r\n", start);
		const std::string line = text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
		const size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		size_t value = colon + 1;
		while (value < line.size() && line[value] == ' ')
			++value;
		out.emplace_back(line.substr(0, colon), line.substr(value));
	}
	return out;
}

int status_code(HINTERNET request)
{
	DWORD status = 0, size = sizeof(status);
	WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
	return (int)status;
}

// Closes the request handle when the token is cancelled: WinHTTP then fails
// the blocking call in progress at once. The handle is never closed twice.
struct CloseOnCancel {
	Handle &request;
	AbortHook hook;
	CloseOnCancel(Handle &target, CancelToken *cancel)
		: request(target),
		  hook(cancel, [h = target.h] { WinHttpCloseHandle(h); })
	{
	}
	CloseOnCancel(const CloseOnCancel &) = delete;
	CloseOnCancel &operator=(const CloseOnCancel &) = delete;
	~CloseOnCancel() { disarm(); }
	// True when the cancel closed the handle.
	bool disarm()
	{
		const bool closed = hook.disarm();
		if (closed)
			request.h = nullptr;
		return closed;
	}
};

Response cancelled_response()
{
	Response res;
	res.error = kCancelledError;
	return res;
}

// `body` is null for GET; otherwise it is sent as a POST body of `content_type`.
Response perform(const wchar_t *method, const std::string &url, Headers headers, const std::string *body,
		 const char *content_type, int timeout_ms, CancelToken *cancel)
{
	Response res;
	Url u;
	if (!session()) {
		res.error = last_error("WinHttpOpen");
		return res;
	}
	if (!parse_url(url, u)) {
		res.error = "invalid URL: " + url;
		return res;
	}
	Handle connection(WinHttpConnect(session(), u.host.c_str(), u.port, 0));
	if (!connection) {
		res.error = last_error("WinHttpConnect");
		return res;
	}
	Handle request(WinHttpOpenRequest(connection, method, u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
					  WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0));
	if (!request) {
		res.error = last_error("WinHttpOpenRequest");
		return res;
	}
	CloseOnCancel cancel_guard(request, cancel);
	if (!cancel_guard.hook.armed())
		return cancelled_response();
	WinHttpSetTimeouts(request, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
	if (body)
		headers.emplace_back("Content-Type", content_type);
	const std::wstring block = header_block(headers);
	LPVOID data = body ? const_cast<char *>(body->data()) : WINHTTP_NO_REQUEST_DATA;
	const DWORD length = body ? (DWORD)body->size() : 0;
	if (!WinHttpSendRequest(request, block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : block.c_str(),
				block.empty() ? 0 : (DWORD)-1L, data, length, length, 0) ||
	    !WinHttpReceiveResponse(request, nullptr)) {
		if (cancel_guard.disarm())
			return cancelled_response();
		res.error = last_error("no response");
		return res;
	}
	res.status = status_code(request);
	res.headers = response_headers(request);
	char buffer[16384];
	for (;;) {
		DWORD read = 0;
		if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
			if (cancel_guard.disarm())
				return cancelled_response();
			res.error = last_error("read");
			res.status = 0;
			return res;
		}
		if (read == 0)
			break;
		res.body.append(buffer, read);
		if (res.body.size() > kMaxBody) {
			res.error = "response too large";
			res.status = 0;
			return res;
		}
	}
	if (cancel_guard.disarm())
		return cancelled_response();
	return res;
}

class WinWebSocket : public WebSocket {
public:
	WinWebSocket(HINTERNET connection, HINTERNET ws) : connection_(connection), ws_(ws) {}
	~WinWebSocket() override
	{
		close();
		WinHttpCloseHandle(connection_);
	}

	Result receive(std::string &message) override
	{
		message.clear();
		char buffer[16384];
		for (;;) {
			HINTERNET ws = ws_.load();
			if (!ws)
				return Result::Closed;
			DWORD read = 0;
			WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
			if (WinHttpWebSocketReceive(ws, buffer, sizeof(buffer), &read, &type) != NO_ERROR)
				return Result::Closed;
			if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
				return Result::Closed;
			message.append(buffer, read);
			if (message.size() > kMaxBody)
				return Result::Closed;
			if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
			    type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
				return Result::Message;
			// *_FRAGMENT_BUFFER_TYPE: keep reading the rest of the message.
		}
	}

	bool send(const std::string &text) override
	{
		return send_buffer(WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, text);
	}

	bool send_binary(const std::string &data) override
	{
		return send_buffer(WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, data);
	}

	void close() override
	{
		// Closing the handle cancels a WinHttpWebSocketReceive in progress.
		if (HINTERNET ws = ws_.exchange(nullptr))
			WinHttpCloseHandle(ws);
	}

private:
	bool send_buffer(WINHTTP_WEB_SOCKET_BUFFER_TYPE type, const std::string &data)
	{
		std::lock_guard<std::mutex> lock(send_mutex_);
		HINTERNET ws = ws_.load();
		return ws && WinHttpWebSocketSend(ws, type, (PVOID)data.data(), (DWORD)data.size()) == NO_ERROR;
	}

	HINTERNET connection_;
	std::atomic<HINTERNET> ws_;
	std::mutex send_mutex_;
};

} // namespace

Response get(const std::string &url, const Headers &headers, int timeout_ms, CancelToken *cancel)
{
	return perform(L"GET", url, headers, nullptr, nullptr, timeout_ms, cancel);
}

Response post_form(const std::string &url, const std::string &body, const Headers &headers, int timeout_ms,
		   CancelToken *cancel)
{
	return perform(L"POST", url, headers, &body, "application/x-www-form-urlencoded", timeout_ms, cancel);
}

Response post(const std::string &url, const std::string &body, const std::string &content_type, const Headers &headers,
	      int timeout_ms, CancelToken *cancel)
{
	return perform(L"POST", url, headers, &body, content_type.c_str(), timeout_ms, cancel);
}

std::shared_ptr<WebSocket> connect_websocket(const std::string &url, const Headers &headers, std::string &error)
{
	Url u;
	if (!session()) {
		error = last_error("WinHttpOpen");
		return nullptr;
	}
	if (!parse_url(url, u)) {
		error = "invalid URL: " + url;
		return nullptr;
	}
	Handle connection(WinHttpConnect(session(), u.host.c_str(), u.port, 0));
	if (!connection) {
		error = last_error("WinHttpConnect");
		return nullptr;
	}
	Handle request(WinHttpOpenRequest(connection, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
					  WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0));
	if (!request) {
		error = last_error("WinHttpOpenRequest");
		return nullptr;
	}
	// No receive timeout on the socket: a quiet chat can go minutes without a message.
	WinHttpSetTimeouts(request, 15000, 15000, 15000, 0);
	if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
		error = last_error("WebSocket upgrade");
		return nullptr;
	}
	const std::wstring block = header_block(headers);
	if (!WinHttpSendRequest(request, block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : block.c_str(),
				block.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(request, nullptr)) {
		error = last_error("no response");
		return nullptr;
	}
	const int status = status_code(request);
	if (status != 101) {
		error = "the server refused the WebSocket (HTTP " + std::to_string(status) + ")";
		return nullptr;
	}
	HINTERNET ws = WinHttpWebSocketCompleteUpgrade(request, 0);
	if (!ws) {
		error = last_error("WebSocket");
		return nullptr;
	}
	return std::make_shared<WinWebSocket>(connection.release(), ws);
}

} // namespace goalbar::net
