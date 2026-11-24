#include "providers/tiktok_provider.hpp"

#include "core/poll_schedule.hpp"
#include "providers/provider_state_name.hpp"

#include <utility>

namespace goalbar {

TikTokProvider::TikTokProvider(TikTokFetchRoom fetch, std::string handle, Metric metric, Logger log,
			   std::chrono::seconds interval)
	: fetch_(std::move(fetch)),
	  handle_(std::move(handle)),
	  metric_(metric),
	  log_(std::move(log)),
	  interval_(interval)
{
	reading_.state = ProviderState::Connecting;
	worker_ = std::thread([this] { run(); });
}

TikTokProvider::~TikTokProvider()
{
	{
		std::lock_guard lock(mutex_);
		stop_ = true;
	}
	cancel_.cancel();
	wake_.notify_all();
	if (worker_.joinable())
		worker_.join();
}

Platform TikTokProvider::platform() const
{
	return Platform::TikTok;
}

Metric TikTokProvider::metric() const
{
	return metric_;
}

MetricReading TikTokProvider::latest() const
{
	std::lock_guard lock(mutex_);
	return reading_;
}

MetricReading TikTokProvider::poll_once()
{
	const HttpResult response = fetch_(tiktok_room_url(handle_), cancel_);
	if (response.status == 0) {
		MetricReading reading;
		reading.state = ProviderState::Error;
		reading.message = response.error.empty() ? "no response" : response.error;
		return reading;
	}
	return tiktok_metric_reading(parse_tiktok_room(response.status, response.body), metric_);
}

void TikTokProvider::publish(MetricReading reading)
{
	bool changed = false;
	{
		std::lock_guard lock(mutex_);
		changed = reading.state != reading_.state || reading.message != reading_.message;
		// Keep the last known value while offline or failing.
		if (!reading.value)
			reading.value = reading_.value;
		reading_ = std::move(reading);
	}
	if (changed && log_) {
		const MetricReading now = latest();
		log_("TikTok @" + handle_ + " " + std::string(metric_id(metric_)) + ": " + provider_state_name(now.state) +
		     (now.message.empty() ? "" : " (" + now.message + ")"));
	}
}

void TikTokProvider::run()
{
	unsigned failures = 0;
	for (;;) {
		{
			std::lock_guard lock(mutex_);
			if (stop_)
				return;
		}
		MetricReading reading = poll_once();
		const ProviderState state = reading.state;
		const bool failed = state == ProviderState::Error || state == ProviderState::RateLimited;
		failures = failed ? failures + 1 : 0;
		publish(std::move(reading));

		const std::chrono::seconds delay = next_poll_delay(interval_, failures);
		std::unique_lock lock(mutex_);
		if (wake_.wait_for(lock, delay, [this] { return stop_; }))
			return;
	}
}

} // namespace goalbar
