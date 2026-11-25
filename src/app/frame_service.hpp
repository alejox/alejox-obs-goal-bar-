// Overlay app: what the streaming app's browser layer loads. /goal-bar is a
// page with a canvas that fetches the bar's latest frame (a transparent PNG
// painted by the app with the plugin's painter) and draws it scaled to fit.
// A frame is only sent again when it changed (the page asks with its last
// version and gets 204 otherwise).
#pragma once

#include "app/http_server.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace goalbar::app {

inline constexpr std::string_view kOverlayPath = "/goal-bar";

// The overlay page (HTML, CSS and JS in one file).
std::string overlay_page();

class FrameService {
public:
	// Viewers are pages that asked for a frame within this long.
	static constexpr std::chrono::seconds kViewerTimeout{3};

	explicit FrameService(std::string app_version) : version_text_(std::move(app_version)) {}
	~FrameService() { stop(); }

	bool start(uint16_t port, std::string &error);
	void stop();
	bool running() const { return running_; }
	uint16_t port() const { return server_.port(); }

	// A new frame (PNG bytes). Thread-safe.
	void publish(std::string png);
	// Pages that fetched a frame recently: paint only while someone looks.
	int viewers() const;

	// The handler, public for the tests.
	HttpResponse handle(const HttpRequest &request);

private:
	HttpServer server_;
	bool running_ = false;
	const std::string version_text_;

	mutable std::mutex mutex_;
	std::string png_;
	uint64_t version_ = 0;
	std::map<std::string, std::chrono::steady_clock::time_point> seen_; // page id → last request
};

} // namespace goalbar::app
