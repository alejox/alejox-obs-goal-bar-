#include "app/http_server.hpp"

#include <chrono>
#include <cstring>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kNoSocket = -1;
#endif

namespace goalbar::app {
namespace {

constexpr int kIdleTimeoutMs = 15000; // a quiet keep-alive connection is closed

void close_socket(SocketHandle socket)
{
#ifdef _WIN32
	closesocket(socket);
#else
	::close(socket);
#endif
}

void shutdown_socket(SocketHandle socket)
{
#ifdef _WIN32
	::shutdown(socket, SD_BOTH);
#else
	::shutdown(socket, SHUT_RDWR);
#endif
}

// Waits until `socket` can be read; false on timeout or error.
bool wait_readable(SocketHandle socket, int timeout_ms)
{
#ifdef _WIN32
	WSAPOLLFD fd{socket, POLLRDNORM, 0};
	return WSAPoll(&fd, 1, timeout_ms) > 0;
#else
	pollfd fd{socket, POLLIN, 0};
	return ::poll(&fd, 1, timeout_ms) > 0;
#endif
}

bool send_all(SocketHandle socket, const std::string &data)
{
	std::size_t sent = 0;
	while (sent < data.size()) {
#ifdef _WIN32
		const int n = ::send(socket, data.data() + sent, int(data.size() - sent), 0);
#elif defined(MSG_NOSIGNAL)
		const auto n = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else // macOS: SO_NOSIGPIPE is set on the socket instead
		const auto n = ::send(socket, data.data() + sent, data.size() - sent, 0);
#endif
		if (n <= 0)
			return false;
		sent += std::size_t(n);
	}
	return true;
}

bool network_ready()
{
#ifdef _WIN32
	static const bool ready = [] {
		WSADATA data;
		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}();
	return ready;
#else
	return true;
#endif
}

} // namespace

struct HttpServer::Connection {
	SocketHandle socket = kNoSocket;
	std::atomic<bool> done{false};
	std::thread thread;
};

HttpServer::~HttpServer()
{
	stop();
}

bool HttpServer::start(uint16_t port, Handler handler, std::string &error)
{
	if (!network_ready()) {
		error = "the network is not available";
		return false;
	}
	SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == kNoSocket) {
		error = "could not create a socket";
		return false;
	}
#ifdef _WIN32
	// Refuse a second app on the same port instead of sharing it.
	BOOL exclusive = TRUE;
	setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
		   sizeof(exclusive));
#else
	int reuse = 1; // restart right away after a quit (TIME_WAIT)
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
	    ::listen(listener, 16) != 0) {
		close_socket(listener);
		error = "port " + std::to_string(port) + " is in use";
		return false;
	}
	socklen_t length = sizeof(address);
	::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length);
	port_ = ntohs(address.sin_port);
	listener_ = static_cast<std::intptr_t>(listener);
	handler_ = std::move(handler);
	stopping_ = false;
	acceptor_ = std::thread([this] { accept_loop(); });
	return true;
}

void HttpServer::stop()
{
	if (!acceptor_.joinable())
		return;
	stopping_ = true;
	acceptor_.join(); // wakes within its poll interval
	close_socket(static_cast<SocketHandle>(listener_));
	listener_ = -1;
	std::list<std::shared_ptr<Connection>> connections;
	{
		std::lock_guard lock(mutex_);
		connections.swap(connections_);
	}
	for (const auto &connection : connections)
		shutdown_socket(connection->socket); // ends a pending read
	for (const auto &connection : connections) {
		if (connection->thread.joinable())
			connection->thread.join();
		close_socket(connection->socket);
	}
}

void HttpServer::accept_loop()
{
	const auto listener = static_cast<SocketHandle>(listener_);
	while (!stopping_) {
		if (!wait_readable(listener, 200)) {
			reap_finished();
			continue;
		}
		const SocketHandle socket = ::accept(listener, nullptr, nullptr);
		if (socket == kNoSocket)
			continue;
		int no_delay = 1; // small frames go out at once
		setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&no_delay), sizeof(no_delay));
#ifdef SO_NOSIGPIPE
		int no_sigpipe = 1; // a closed browser tab must not kill the app
		setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
		auto connection = std::make_shared<Connection>();
		connection->socket = socket;
		std::lock_guard lock(mutex_);
		if (stopping_) {
			close_socket(socket);
			return;
		}
		connections_.push_back(connection);
		connection->thread = std::thread([this, connection] { serve(connection); });
	}
}

void HttpServer::reap_finished()
{
	std::list<std::shared_ptr<Connection>> finished;
	{
		std::lock_guard lock(mutex_);
		for (auto it = connections_.begin(); it != connections_.end();) {
			if ((*it)->done) {
				finished.push_back(*it);
				it = connections_.erase(it);
			} else {
				++it;
			}
		}
	}
	for (const auto &connection : finished) {
		connection->thread.join();
		close_socket(connection->socket);
	}
}

void HttpServer::serve(const std::shared_ptr<Connection> &connection)
{
	const SocketHandle socket = connection->socket;
	std::string buffer;
	char chunk[4096];
	while (!stopping_) {
		std::size_t end = find_request_end(buffer);
		while (end == std::string::npos) {
			if (buffer.size() > kMaxRequestHead || !wait_readable(socket, kIdleTimeoutMs)) {
				connection->done = true;
				return;
			}
			const auto n = ::recv(socket, chunk, sizeof(chunk), 0);
			if (n <= 0 || stopping_) {
				connection->done = true;
				return;
			}
			buffer.append(chunk, std::size_t(n));
			end = find_request_end(buffer);
		}
		const std::optional<HttpRequest> request = parse_http_request(std::string_view(buffer).substr(0, end));
		buffer.erase(0, end); // the overlay's requests have no body
		HttpResponse response;
		bool keep_alive = false;
		bool head_only = false;
		if (!request) {
			response.status = 400;
			response.body = "bad request";
		} else if (!is_loopback_host(request->host, port_)) {
			response.status = 403;
			response.body = "only for this computer";
		} else if (request->method != "GET" && request->method != "HEAD") {
			response.status = 405;
			response.body = "method not allowed";
		} else {
			head_only = request->method == "HEAD";
			keep_alive = request->keep_alive;
			response = handler_(*request);
		}
		if (!send_all(socket, format_http_response(response, head_only, keep_alive)) || !keep_alive)
			break;
	}
	connection->done = true;
}

} // namespace goalbar::app
