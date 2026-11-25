// Overlay app: a small web server on 127.0.0.1 only. It serves the overlay
// page and its frames to the streaming app's browser layer (Meld Studio, OBS,
// Streamlabs…). One thread accepts; each connection gets its own thread and
// may send several requests (keep-alive), as a browser fetching frames does.
#pragma once

#include "app/http.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace goalbar::app {

class HttpServer {
public:
	// Called on a connection thread for each request from a loopback host;
	// may run on several threads at once.
	using Handler = std::function<HttpResponse(const HttpRequest &request)>;

	HttpServer() = default;
	~HttpServer();
	HttpServer(const HttpServer &) = delete;
	HttpServer &operator=(const HttpServer &) = delete;

	// Listens on 127.0.0.1:`port` (0 picks a free port). False with `error`
	// set when the port cannot be used (for example, it is taken).
	bool start(uint16_t port, Handler handler, std::string &error);
	// Closes every connection and joins the threads. Safe to call twice.
	void stop();
	uint16_t port() const { return port_; }

private:
	struct Connection;
	void accept_loop();
	void serve(const std::shared_ptr<Connection> &connection);
	void reap_finished();

	Handler handler_;
	std::atomic<bool> stopping_{false};
	std::intptr_t listener_ = -1;
	uint16_t port_ = 0;
	std::mutex mutex_;
	std::list<std::shared_ptr<Connection>> connections_;
	std::thread acceptor_;
};

} // namespace goalbar::app
