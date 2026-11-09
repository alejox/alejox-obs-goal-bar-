#include "providers/youtube_public_provider.hpp"

#include <utility>

namespace goalbar {
namespace {

const char *state_name(ProviderState state)
{
	switch (state) {
	case ProviderState::Idle:
		return "idle";
	case ProviderState::Connecting:
		return "connecting";
	case ProviderState::Live:
		return "live";
	case ProviderState::NotLive:
		return "not live";
	case ProviderState::AuthRequired:
		return "auth required";
	case ProviderState::RateLimited:
		return "rate limited";
	case ProviderState::Error:
		return "error";
	}
	return "unknown";
}

MetricReading error_reading(std::string message)
{
	MetricReading reading;
	reading.state = ProviderState::Error;
	reading.message = std::move(message);
	return reading;
}

} // namespace

MetricReading reading_from_snapshot(const YoutubeLiveSnapshot &snapshot, Metric metric)
{
	MetricReading reading;
	if (metric == Metric::Subscribers) {
		if (snapshot.subscribers) {
			reading.state = ProviderState::Live;
			reading.value = snapshot.subscribers;
			return reading;
		}
		return error_reading("subscriber count not found on the page");
	}
	if (!snapshot.live) {
		reading.state = ProviderState::NotLive;
		return reading;
	}
	reading.state = ProviderState::Live;
	reading.value = snapshot.likes;
	if (!snapshot.likes)
		reading.message = "likes are hidden on this stream";
	return reading;
}

YoutubePublicProvider::YoutubePublicProvider(std::string channel_url, Metric metric, PageFetcher fetch,
					     ProviderLogger log, std::chrono::seconds interval)
	: url_(youtube_live_url(channel_url)),
	  metric_(metric),
	  fetch_(std::move(fetch)),
	  log_(std::move(log)),
	  interval_(interval)
{
	reading_.state = ProviderState::Connecting;
	worker_ = std::thread([this] { run(); });
}

YoutubePublicProvider::~YoutubePublicProvider()
{
	{
		std::lock_guard lock(mutex_);
		stop_ = true;
	}
	wake_.notify_all();
	if (worker_.joinable())
		worker_.join();
}

Platform YoutubePublicProvider::platform() const
{
	return Platform::YouTube;
}

Metric YoutubePublicProvider::metric() const
{
	return metric_;
}

MetricReading YoutubePublicProvider::latest() const
{
	std::lock_guard lock(mutex_);
	return reading_;
}

MetricReading YoutubePublicProvider::poll_once()
{
	const HttpResult result = fetch_(url_);
	if (result.status == 0)
		return error_reading(result.error.empty() ? "no response" : result.error);
	if (result.status < 200 || result.status >= 300)
		return error_reading("HTTP " + std::to_string(result.status));
	return reading_from_snapshot(parse_youtube_live_page(result.body), metric_);
}

void YoutubePublicProvider::publish(MetricReading reading)
{
	bool changed = false;
	{
		std::lock_guard lock(mutex_);
		changed = reading.state != reading_.state || reading.message != reading_.message;
		// Keep the last known value while the source is offline or failing.
		if (!reading.value && reading.state != ProviderState::Live)
			reading.value = reading_.value;
		reading_ = std::move(reading);
	}
	if (changed && log_) {
		const MetricReading now = latest();
		log_("YouTube " + url_ + ": " + state_name(now.state) + (now.message.empty() ? "" : " (" + now.message + ")"));
	}
}

void YoutubePublicProvider::run()
{
	unsigned failures = 0;
	for (;;) {
		{
			std::lock_guard lock(mutex_);
			if (stop_)
				return;
		}
		MetricReading reading = poll_once();
		failures = reading.state == ProviderState::Error ? failures + 1 : 0;
		publish(std::move(reading));

		std::unique_lock lock(mutex_);
		if (wake_.wait_for(lock, next_poll_delay(interval_, failures), [this] { return stop_; }))
			return;
	}
}

} // namespace goalbar
