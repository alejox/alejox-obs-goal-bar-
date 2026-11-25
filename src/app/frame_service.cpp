#include "app/frame_service.hpp"

#include <algorithm>
#include <utility>

namespace goalbar::app {

std::string overlay_page()
{
	// Transparent page; the canvas fills the layer and the bar keeps its
	// proportions. While the app is closed the page clears and keeps retrying,
	// so the layer comes back by itself when the app opens again.
	return R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AC Goal Bar</title>
<style>
html, body { margin: 0; height: 100%; background: transparent; overflow: hidden; }
canvas { display: block; width: 100vw; height: 100vh; }
</style>
</head>
<body>
<canvas id="bar"></canvas>
<script>
"use strict";
const canvas = document.getElementById("bar");
const context = canvas.getContext("2d");
const id = Math.random().toString(36).slice(2, 10);
let version = 0;
let failures = 0;
let image = null;

function draw() {
  const ratio = window.devicePixelRatio || 1;
  const width = Math.round(window.innerWidth * ratio);
  const height = Math.round(window.innerHeight * ratio);
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }
  context.clearRect(0, 0, width, height);
  if (!image) return;
  const scale = Math.min(width / image.width, height / image.height);
  const w = image.width * scale, h = image.height * scale;
  context.imageSmoothingQuality = "high";
  context.drawImage(image, (width - w) / 2, (height - h) / 2, w, h);
}

async function next() {
  try {
    const response = await fetch("/goal-bar/frame?since=" + version + "&id=" + id, { cache: "no-store" });
    if (response.status === 200) {
      const bitmap = await createImageBitmap(await response.blob());
      if (image) image.close();
      image = bitmap;
      version = Number(response.headers.get("X-Frame-Version")) || 0;
      draw();
    } else if (response.status !== 204) {
      throw new Error("HTTP " + response.status);
    }
    failures = 0;
    setTimeout(next, 30);
  } catch (error) {
    failures += 1;
    if (failures >= 3 && image) { image.close(); image = null; version = 0; draw(); }
    setTimeout(next, 1000);
  }
}

window.addEventListener("resize", draw);
next();
</script>
</body>
</html>
)HTML";
}

bool FrameService::start(uint16_t port, std::string &error)
{
	stop();
	running_ = server_.start(port, [this](const HttpRequest &request) { return handle(request); }, error);
	return running_;
}

void FrameService::stop()
{
	server_.stop();
	running_ = false;
}

void FrameService::publish(std::string png)
{
	std::lock_guard lock(mutex_);
	png_ = std::move(png);
	++version_;
}

int FrameService::viewers() const
{
	const auto now = std::chrono::steady_clock::now();
	std::lock_guard lock(mutex_);
	return int(std::count_if(seen_.begin(), seen_.end(),
				 [now](const auto &entry) { return now - entry.second < kViewerTimeout; }));
}

HttpResponse FrameService::handle(const HttpRequest &request)
{
	HttpResponse response;
	if (request.path == "/" || request.path == kOverlayPath || request.path == std::string(kOverlayPath) + "/") {
		response.content_type = "text/html; charset=utf-8";
		response.body = overlay_page();
		return response;
	}
	if (request.path == std::string(kOverlayPath) + "/frame") {
		const std::string since = query_param(request.query, "since");
		const std::string id = query_param(request.query, "id");
		std::lock_guard lock(mutex_);
		if (!id.empty() && id.size() <= 16) {
			const auto now = std::chrono::steady_clock::now();
			seen_[id] = now;
			for (auto it = seen_.begin(); it != seen_.end();) // forget closed pages
				it = now - it->second > std::chrono::minutes(1) ? seen_.erase(it) : std::next(it);
		}
		response.headers.emplace_back("X-Frame-Version", std::to_string(version_));
		if (png_.empty() || since == std::to_string(version_)) {
			response.status = 204;
			response.content_type = "text/plain";
			return response;
		}
		response.content_type = "image/png";
		response.body = png_;
		return response;
	}
	if (request.path == "/health") {
		response.content_type = "application/json";
		response.body = R"({"app":"ac-goal-bar-overlay","version":")" + version_text_ + R"("})";
		return response;
	}
	response.status = 404;
	response.body = "not found";
	return response;
}

} // namespace goalbar::app
