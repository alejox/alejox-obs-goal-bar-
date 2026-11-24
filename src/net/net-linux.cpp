// Network on Linux with libcurl (OBS already depends on it). For WebSockets,
// curl only opens the TCP/TLS connection (CONNECT_ONLY) and the handshake and
// RFC 6455 framing live here, so the system libcurl needs no WebSocket support.
// Ported from multi-chat's src/net/net-linux.cpp.

#include "net/net.hpp"

#include <curl/curl.h>
#include <poll.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <random>

namespace goalbar::net {

const char *const kUserAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
			       "(KHTML, like Gecko) Chrome/130.0 Safari/537.36";

namespace {

constexpr size_t kMaxBody = 16 * 1024 * 1024;

void global_init()
{
	static std::once_flag once;
	std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Easy {
	CURL *h = curl_easy_init();
	curl_slist *headers = nullptr;
	Easy() = default;
	Easy(const Easy &) = delete;
	Easy &operator=(const Easy &) = delete;
	~Easy()
	{
		if (h)
			curl_easy_cleanup(h);
		curl_slist_free_all(headers);
	}
};

struct Transfer {
	Response *res;
	bool too_big = false;
};

size_t on_header(char *data, size_t size, size_t count, void *user)
{
	auto *transfer = static_cast<Transfer *>(user);
	const size_t n = size * count;
	std::string line(data, n);
	while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
		line.pop_back();
	// Each response of a redirect chain sends its headers: keep only the last one's.
	if (line.rfind("HTTP/", 0) == 0) {
		transfer->res->headers.clear();
		return n;
	}
	const size_t colon = line.find(':');
	if (colon == std::string::npos)
		return n;
	size_t value = colon + 1;
	while (value < line.size() && line[value] == ' ')
		++value;
	transfer->res->headers.emplace_back(line.substr(0, colon), line.substr(value));
	return n;
}

size_t on_body(char *data, size_t size, size_t count, void *user)
{
	auto *transfer = static_cast<Transfer *>(user);
	const size_t n = size * count;
	if (transfer->res->body.size() + n > kMaxBody) {
		transfer->too_big = true;
		return 0; // aborts the transfer
	}
	transfer->res->body.append(data, n);
	return n;
}

// Runs the transfer on a multi handle so a cancel wakes the wait at once
// (curl_multi_wakeup is the one libcurl call that is safe from another thread).
// Needs libcurl 7.68+.
CURLcode perform(CURL *easy, CancelToken *cancel, bool &cancelled)
{
	cancelled = false;
	if (!cancel)
		return curl_easy_perform(easy);
	CURLM *multi = curl_multi_init();
	if (!multi)
		return CURLE_OUT_OF_MEMORY;
	curl_multi_add_handle(multi, easy);

	CURLcode result = CURLE_OK;
	AbortHook hook(cancel, [multi] { curl_multi_wakeup(multi); });
	cancelled = !hook.armed();
	for (int running = 1; running && !cancelled;) {
		if (curl_multi_perform(multi, &running) != CURLM_OK) {
			result = CURLE_FAILED_INIT;
			break;
		}
		if (!running) {
			int queued = 0;
			const CURLMsg *message = curl_multi_info_read(multi, &queued);
			result = message && message->msg == CURLMSG_DONE ? message->data.result : CURLE_OK;
			break;
		}
		curl_multi_poll(multi, nullptr, 0, 1000, nullptr);
		cancelled = cancel->cancelled();
	}
	// Disarm before the multi handle goes away: the hook must never wake a freed handle.
	cancelled = hook.disarm() || cancelled;
	curl_multi_remove_handle(multi, easy);
	curl_multi_cleanup(multi);
	return result;
}

// `body` is null for GET; otherwise it is sent as a POST body of `content_type`.
Response perform_request(const std::string &url, const Headers &headers, const std::string *body,
			 const std::string &content_type, int timeout_ms, CancelToken *cancel)
{
	global_init();
	Response res;
	Easy easy;
	if (!easy.h) {
		res.error = "curl_easy_init failed";
		return res;
	}
	CURL *h = easy.h;
	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_USERAGENT, kUserAgent);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, long(timeout_ms));
	curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, long(timeout_ms));
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_MAXREDIRS, 10L);
	curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, ""); // whatever this libcurl supports
	if (body) {
		curl_easy_setopt(h, CURLOPT_POST, 1L);
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, body->c_str());
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, long(body->size()));
		easy.headers = curl_slist_append(easy.headers, ("Content-Type: " + content_type).c_str());
	} else {
		curl_easy_setopt(h, CURLOPT_HTTPGET, 1L);
	}
	for (const auto &[name, value] : headers)
		easy.headers = curl_slist_append(easy.headers, (name + ": " + value).c_str());
	if (easy.headers)
		curl_easy_setopt(h, CURLOPT_HTTPHEADER, easy.headers);
	Transfer transfer{&res};
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &transfer);
	curl_easy_setopt(h, CURLOPT_HEADERFUNCTION, on_header);
	curl_easy_setopt(h, CURLOPT_HEADERDATA, &transfer);
	bool cancelled = false;
	const CURLcode rc = perform(h, cancel, cancelled);
	if (cancelled) {
		res.body.clear();
		res.error = kCancelledError;
		res.status = 0;
		return res;
	}
	if (rc != CURLE_OK) {
		res.error = transfer.too_big ? "response too large" : curl_easy_strerror(rc);
		res.status = 0;
		return res;
	}
	long status = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
	res.status = int(status);
	return res;
}


// Host, port and path of a WebSocket URL, and the https:// URL curl can connect to.
struct WsUrl {
	std::string connect, host, path;
	bool ok = false;
};

WsUrl parse_ws_url(const std::string &url)
{
	WsUrl out;
	CURLU *u = curl_url();
	if (!u)
		return out;
	std::string fixed = url;
	if (fixed.rfind("wss://", 0) == 0)
		fixed = "https://" + fixed.substr(6);
	else if (fixed.rfind("ws://", 0) == 0)
		fixed = "http://" + fixed.substr(5);
	char *scheme = nullptr, *host = nullptr, *port = nullptr, *path = nullptr, *query = nullptr;
	if (curl_url_set(u, CURLUPART_URL, fixed.c_str(), 0) == CURLUE_OK &&
	    curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
	    curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
	    curl_url_get(u, CURLUPART_PATH, &path, 0) == CURLUE_OK) {
		curl_url_get(u, CURLUPART_PORT, &port, 0); // only when the URL has one
		curl_url_get(u, CURLUPART_QUERY, &query, 0);
		out.host = host;
		if (port)
			out.host += std::string(":") + port;
		out.path = path;
		if (query)
			out.path += std::string("?") + query;
		out.connect = std::string(scheme) + "://" + out.host + "/";
		out.ok = true;
	}
	curl_free(scheme);
	curl_free(host);
	curl_free(port);
	curl_free(path);
	curl_free(query);
	curl_url_cleanup(u);
	return out;
}

std::string base64(const unsigned char *data, size_t size)
{
	static const char *const table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	for (size_t i = 0; i < size; i += 3) {
		const unsigned v = unsigned(data[i]) << 16 | (i + 1 < size ? unsigned(data[i + 1]) << 8 : 0u) |
				   (i + 2 < size ? unsigned(data[i + 2]) : 0u);
		out += table[(v >> 18) & 63];
		out += table[(v >> 12) & 63];
		out += i + 1 < size ? table[(v >> 6) & 63] : '=';
		out += i + 2 < size ? table[v & 63] : '=';
	}
	return out;
}

class CurlWebSocket : public WebSocket {
public:
	CurlWebSocket(CURL *h, curl_socket_t socket) : h_(h), socket_(socket), random_(std::random_device{}()) {}
	~CurlWebSocket() override
	{
		close();
		curl_easy_cleanup(h_);
	}

	Result receive(std::string &message) override
	{
		message.clear();
		int message_type = -1; // the message in progress: text or binary
		for (;;) {
			unsigned char opcode = 0;
			bool fin = false;
			std::string payload;
			if (!read_frame(opcode, fin, payload))
				return Result::Closed;
			switch (opcode) {
			case 0x8: // the server closes: answer and stop
				send_frame(0x8, payload.substr(0, 2));
				closed_ = true;
				return Result::Closed;
			case 0x9: // ping → pong with the same data
				if (!send_frame(0xA, payload))
					return Result::Closed;
				continue;
			case 0xA: // pong
				continue;
			case 0x0: // continuation
				if (message_type < 0)
					return Result::Closed;
				break;
			case 0x1:
			case 0x2:
				if (message_type >= 0)
					return Result::Closed;
				message_type = opcode;
				break;
			default:
				return Result::Closed;
			}
			message += payload;
			if (message.size() > kMaxBody)
				return Result::Closed;
			if (fin)
				return Result::Message;
		}
	}

	bool send(const std::string &text) override { return send_frame(0x1, text); }
	bool send_binary(const std::string &data) override { return send_frame(0x2, data); }

	// receive() notices within a tenth of a second.
	void close() override { closed_ = true; }

	// Sends the HTTP upgrade request and reads the answer. Returns the HTTP status.
	int handshake(const std::string &hello, std::string &error)
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!send_all(hello)) {
				error = "could not send the WebSocket handshake";
				return 0;
			}
		}
		// The answer ends with an empty line; anything after it is already frames.
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
		size_t end;
		while ((end = in_.find("\r\n\r\n")) == std::string::npos) {
			if (in_.size() > 65536 || std::chrono::steady_clock::now() > deadline || !fill()) {
				error = "no answer to the WebSocket handshake";
				return 0;
			}
		}
		const std::string response = in_.substr(0, end);
		in_.erase(0, end + 4);
		const size_t space = response.find(' ');
		return space == std::string::npos ? 0 : std::atoi(response.c_str() + space + 1);
	}

private:
	// Waits up to 100 ms for the socket to be readable or writable.
	bool wait(short events)
	{
		pollfd pfd = {};
		pfd.fd = int(socket_);
		pfd.events = events;
		return poll(&pfd, 1, 100) >= 0;
	}

	// Reads what is there, without blocking (send() shares the TLS connection).
	bool fill()
	{
		for (;;) {
			if (closed_)
				return false;
			char buffer[16384];
			size_t got = 0;
			CURLcode rc;
			{
				std::lock_guard<std::mutex> lock(mutex_);
				rc = curl_easy_recv(h_, buffer, sizeof(buffer), &got);
			}
			if (rc == CURLE_OK) {
				if (got == 0) // the server closed the connection
					return false;
				in_.append(buffer, got);
				return true;
			}
			if (rc != CURLE_AGAIN || !wait(POLLIN))
				return false;
		}
	}

	bool need(size_t bytes)
	{
		while (in_.size() < bytes)
			if (!fill())
				return false;
		return true;
	}

	bool read_frame(unsigned char &opcode, bool &fin, std::string &payload)
	{
		if (!need(2))
			return false;
		const auto byte = [this](size_t i) {
			return static_cast<unsigned char>(in_[i]);
		};
		fin = byte(0) & 0x80;
		opcode = byte(0) & 0x0F;
		const bool masked = byte(1) & 0x80;
		uint64_t length = byte(1) & 0x7F;
		size_t header = 2;
		if (length == 126) {
			if (!need(4))
				return false;
			length = uint64_t(byte(2)) << 8 | byte(3);
			header = 4;
		} else if (length == 127) {
			if (!need(10))
				return false;
			length = 0;
			for (size_t i = 2; i < 10; ++i)
				length = length << 8 | byte(i);
			header = 10;
		}
		if (length > kMaxBody)
			return false;
		const size_t mask_at = header;
		if (masked)
			header += 4;
		if (!need(header + size_t(length)))
			return false;
		payload = in_.substr(header, size_t(length));
		if (masked)
			for (size_t i = 0; i < payload.size(); ++i)
				payload[i] = char(payload[i] ^ in_[mask_at + i % 4]);
		in_.erase(0, header + size_t(length));
		return true;
	}

	// Client frames are always masked.
	bool send_frame(unsigned char opcode, const std::string &data)
	{
		std::string frame;
		frame += char(0x80 | opcode);
		const size_t n = data.size();
		if (n < 126) {
			frame += char(0x80 | n);
		} else if (n <= 0xFFFF) {
			frame += char(0x80 | 126);
			frame += char(n >> 8);
			frame += char(n & 0xFF);
		} else {
			frame += char(0x80 | 127);
			for (int shift = 56; shift >= 0; shift -= 8)
				frame += char((uint64_t(n) >> shift) & 0xFF);
		}
		std::lock_guard<std::mutex> lock(mutex_);
		unsigned char mask[4];
		for (unsigned char &m : mask)
			m = static_cast<unsigned char>(random_() & 0xFF);
		frame.append(reinterpret_cast<const char *>(mask), 4);
		const size_t start = frame.size();
		frame += data;
		for (size_t i = start; i < frame.size(); ++i)
			frame[i] = char(frame[i] ^ mask[(i - start) % 4]);
		return send_all(frame);
	}

	// Caller holds mutex_.
	bool send_all(const std::string &data)
	{
		size_t offset = 0;
		while (offset < data.size()) {
			size_t sent = 0;
			const CURLcode rc = curl_easy_send(h_, data.data() + offset, data.size() - offset, &sent);
			if (rc == CURLE_OK) {
				offset += sent;
			} else if (rc != CURLE_AGAIN || closed_ || !wait(POLLOUT)) {
				return false;
			}
		}
		return true;
	}

	CURL *h_;
	curl_socket_t socket_;
	std::string in_;
	std::atomic<bool> closed_{false};
	std::mutex mutex_;
	std::mt19937 random_;
};

} // namespace

Response get(const std::string &url, const Headers &headers, int timeout_ms, CancelToken *cancel)
{
	return perform_request(url, headers, nullptr, {}, timeout_ms, cancel);
}

Response post_form(const std::string &url, const std::string &body, const Headers &headers, int timeout_ms,
		   CancelToken *cancel)
{
	return perform_request(url, headers, &body, "application/x-www-form-urlencoded", timeout_ms, cancel);
}

Response post(const std::string &url, const std::string &body, const std::string &content_type, const Headers &headers,
	      int timeout_ms, CancelToken *cancel)
{
	return perform_request(url, headers, &body, content_type, timeout_ms, cancel);
}

std::shared_ptr<WebSocket> connect_websocket(const std::string &url, const Headers &headers, std::string &error)
{
	global_init();
	const WsUrl u = parse_ws_url(url);
	if (!u.ok) {
		error = "invalid URL: " + url;
		return nullptr;
	}
	CURL *h = curl_easy_init();
	if (!h) {
		error = "curl_easy_init failed";
		return nullptr;
	}
	curl_easy_setopt(h, CURLOPT_URL, u.connect.c_str());
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 15000L);
	curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 1L);
	// The WebSocket handshake is HTTP/1.1: keep TLS from negotiating HTTP/2.
	curl_easy_setopt(h, CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
	CURLcode rc = curl_easy_perform(h);
	curl_socket_t socket = CURL_SOCKET_BAD;
	if (rc == CURLE_OK)
		rc = curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &socket);
	if (rc != CURLE_OK || socket == CURL_SOCKET_BAD) {
		error = std::string("could not connect: ") + curl_easy_strerror(rc);
		curl_easy_cleanup(h);
		return nullptr;
	}

	unsigned char key[16];
	std::random_device random;
	for (unsigned char &k : key)
		k = static_cast<unsigned char>(random() & 0xFF);
	std::string hello = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host +
			    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
			    "Sec-WebSocket-Key: " +
			    base64(key, sizeof(key)) + "\r\nUser-Agent: " + kUserAgent + "\r\n";
	for (const auto &[name, value] : headers)
		hello += name + ": " + value + "\r\n";
	hello += "\r\n";

	auto ws = std::make_shared<CurlWebSocket>(h, socket);
	const int status = ws->handshake(hello, error);
	if (status != 101) {
		if (error.empty())
			error = "the server refused the WebSocket (HTTP " + std::to_string(status) + ")";
		return nullptr;
	}
	return ws;
}

} // namespace goalbar::net
