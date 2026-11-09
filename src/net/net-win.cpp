// Network on Windows with WinHTTP (Windows 8+).

#include "net/net.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

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

bool parse_url(const std::string &url, Url &out)
{
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

} // namespace

Response get(const std::string &url, const Headers &headers, int timeout_ms, CancelToken *cancel)
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
	Handle request(WinHttpOpenRequest(connection, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
					  WINHTTP_DEFAULT_ACCEPT_TYPES, u.secure ? WINHTTP_FLAG_SECURE : 0));
	if (!request) {
		res.error = last_error("WinHttpOpenRequest");
		return res;
	}
	CloseOnCancel cancel_guard(request, cancel);
	if (!cancel_guard.hook.armed())
		return cancelled_response();
	WinHttpSetTimeouts(request, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
	const std::wstring block = header_block(headers);
	if (!WinHttpSendRequest(request, block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : block.c_str(),
				block.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
	    !WinHttpReceiveResponse(request, nullptr)) {
		if (cancel_guard.disarm())
			return cancelled_response();
		res.error = last_error("no response");
		return res;
	}
	res.status = status_code(request);
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

} // namespace goalbar::net
