#include "app/http_server.hpp"

#include "test_harness.hpp"

#include <chrono>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define CLOSE_SOCKET ::close
#endif

using namespace goalbar::app;

namespace {

// Sends `request` to 127.0.0.1:`port` and reads until `reads` responses' worth
// of data or the connection closes.
std::string round_trip(uint16_t port, const std::string &request)
{
	SocketHandle socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
		CLOSE_SOCKET(socket);
		return {};
	}
	::send(socket, request.data(), int(request.size()), 0);
	std::string out;
	char buffer[4096];
	for (;;) {
		const auto n = ::recv(socket, buffer, sizeof(buffer), 0);
		if (n <= 0)
			break;
		out.append(buffer, std::size_t(n));
	}
	CLOSE_SOCKET(socket);
	return out;
}

HttpResponse echo(const HttpRequest &request)
{
	HttpResponse response;
	response.body = "path=" + request.path;
	return response;
}

} // namespace

TEST_CASE(serves_requests_on_loopback)
{
	HttpServer server;
	std::string error;
	REQUIRE(server.start(0, echo, error));
	const uint16_t port = server.port();
	CHECK(port != 0);
	const std::string host = "127.0.0.1:" + std::to_string(port);

	const std::string one =
		round_trip(port, "GET /goal-bar?x=1 HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n");
	CHECK(one.rfind("HTTP/1.1 200 OK", 0) == 0);
	CHECK(one.find("path=/goal-bar") != std::string::npos);

	// Two requests on one keep-alive connection, the second closing it.
	const std::string two = round_trip(port, "GET /a HTTP/1.1\r\nHost: " + host + "\r\n\r\nGET /b HTTP/1.1\r\nHost: " +
							 host + "\r\nConnection: close\r\n\r\n");
	CHECK(two.find("path=/a") != std::string::npos);
	CHECK(two.find("path=/b") != std::string::npos);
	server.stop();
}

TEST_CASE(refuses_other_hosts_and_methods)
{
	HttpServer server;
	std::string error;
	REQUIRE(server.start(0, echo, error));
	const uint16_t port = server.port();
	CHECK(round_trip(port, "GET / HTTP/1.1\r\nHost: evil.example\r\n\r\n").rfind("HTTP/1.1 403", 0) == 0);
	CHECK(round_trip(port, "POST / HTTP/1.1\r\nHost: localhost:" + std::to_string(port) + "\r\n\r\n")
		      .rfind("HTTP/1.1 405", 0) == 0);
	CHECK(round_trip(port, "garbage\r\n\r\n").rfind("HTTP/1.1 400", 0) == 0);
}

TEST_CASE(a_taken_port_is_reported)
{
	HttpServer first;
	std::string error;
	REQUIRE(first.start(0, echo, error));
	HttpServer second;
	CHECK(!second.start(first.port(), echo, error));
	CHECK(error.find(std::to_string(first.port())) != std::string::npos);
}

TEST_CASE(stops_with_an_idle_connection_open)
{
	HttpServer server;
	std::string error;
	REQUIRE(server.start(0, echo, error));
	SocketHandle idle = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(server.port());
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECK(::connect(idle, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
	const auto start = std::chrono::steady_clock::now();
	server.stop(); // must not wait for the 15 s idle timeout
	CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
	CLOSE_SOCKET(idle);
}
