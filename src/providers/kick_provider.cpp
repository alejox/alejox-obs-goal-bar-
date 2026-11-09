#include "providers/kick_provider.hpp"

#include "providers/provider_state_name.hpp"

#include <algorithm>
#include <utility>

namespace goalbar {
namespace {

MetricReading reading_with(ProviderState state, std::string message = {})
{
	MetricReading reading;
	reading.state = state;
	reading.message = std::move(message);
	return reading;
}

} // namespace

std::chrono::seconds kick_next_delay(ProviderState state, unsigned failures, std::chrono::seconds interval,
				     std::chrono::seconds auth_recheck, std::optional<std::chrono::seconds> retry_after)
{
	if (state == ProviderState::AuthRequired)
		return auth_recheck;
	std::chrono::seconds delay = next_poll_delay(interval, failures);
	if (retry_after)
		delay = std::min(std::max(delay, *retry_after), kMaxBackoff);
	return delay;
}

KickProvider::KickProvider(std::shared_ptr<KickApiClient> client, std::string slug, Metric metric, Logger log,
			   std::chrono::seconds interval, std::chrono::seconds auth_recheck)
	: client_(std::move(client)),
	  slug_(std::move(slug)),
	  metric_(metric),
	  log_(std::move(log)),
	  interval_(interval),
	  auth_recheck_(auth_recheck)
{
	reading_.state = ProviderState::Connecting;
	worker_ = std::thread([this] { run(); });
}

KickProvider::~KickProvider()
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

Platform KickProvider::platform() const
{
	return Platform::Kick;
}

Metric KickProvider::metric() const
{
	return metric_;
}

MetricReading KickProvider::latest() const
{
	std::lock_guard lock(mutex_);
	return reading_;
}

KickProvider::Poll KickProvider::poll_once()
{
	const KickResponse response = client_->get(kick_channel_url(slug_), cancel_);
	Poll poll;
	poll.retry_after = response.retry_after;
	switch (response.outcome) {
	case KickOutcome::NotSetUp:
		poll.reading = reading_with(ProviderState::AuthRequired, kKickNotSetUp);
		return poll;
	case KickOutcome::BadCredentials:
		poll.reading = reading_with(ProviderState::AuthRequired, kKickBadCredentials);
		return poll;
	case KickOutcome::RateLimited:
		poll.reading = reading_with(ProviderState::RateLimited, "Kick rate limit");
		return poll;
	case KickOutcome::Failed:
		poll.reading = reading_with(ProviderState::Error,
					    response.http.error.empty() ? "no response" : response.http.error);
		return poll;
	case KickOutcome::Ok:
		break;
	}

	const int status = response.http.status;
	if (status == 429) {
		poll.reading = reading_with(ProviderState::RateLimited, "Kick rate limit");
		return poll;
	}
	if (status == 404) {
		poll.reading = reading_with(ProviderState::Error, kKickChannelNotFound);
		return poll;
	}
	if (status < 200 || status >= 300) {
		poll.reading = reading_with(ProviderState::Error, "HTTP " + std::to_string(status));
		return poll;
	}
	const KickChannelLookup lookup = parse_kick_channels(response.http.body, slug_);
	if (lookup.status == KickChannelStatus::NotFound || !lookup.channel) {
		poll.reading = reading_with(ProviderState::Error, lookup.status == KickChannelStatus::NotFound
									  ? kKickChannelNotFound
									  : "unexpected answer from Kick");
		return poll;
	}
	poll.reading = kick_metric_reading(*lookup.channel, metric_);
	return poll;
}

void KickProvider::publish(MetricReading reading)
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
		log_("Kick " + slug_ + " " + std::string(metric_id(metric_)) + ": " + provider_state_name(now.state) +
		     (now.message.empty() ? "" : " (" + now.message + ")"));
	}
}

void KickProvider::run()
{
	unsigned failures = 0;
	for (;;) {
		{
			std::lock_guard lock(mutex_);
			if (stop_)
				return;
		}
		Poll poll = poll_once();
		const ProviderState state = poll.reading.state;
		const bool failed = state == ProviderState::Error || state == ProviderState::RateLimited;
		failures = failed ? failures + 1 : 0;
		publish(std::move(poll.reading));

		const std::chrono::seconds delay =
			kick_next_delay(state, failures, interval_, auth_recheck_, poll.retry_after);
		std::unique_lock lock(mutex_);
		if (wake_.wait_for(lock, delay, [this] { return stop_; }))
			return;
	}
}

} // namespace goalbar
