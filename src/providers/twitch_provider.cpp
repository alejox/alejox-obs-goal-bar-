#include "providers/twitch_provider.hpp"

#include "core/twitch_helix.hpp"
#include "providers/provider_state_name.hpp"

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

TwitchProvider::TwitchProvider(std::shared_ptr<TwitchHelixClient> client, Metric metric, Logger log,
			       std::chrono::seconds interval, std::chrono::seconds auth_recheck)
	: client_(std::move(client)),
	  metric_(metric),
	  log_(std::move(log)),
	  interval_(interval),
	  auth_recheck_(auth_recheck)
{
	reading_.state = ProviderState::Connecting;
	worker_ = std::thread([this] { run(); });
}

TwitchProvider::~TwitchProvider()
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

Platform TwitchProvider::platform() const
{
	return Platform::Twitch;
}

Metric TwitchProvider::metric() const
{
	return metric_;
}

MetricReading TwitchProvider::latest() const
{
	std::lock_guard lock(mutex_);
	return reading_;
}

MetricReading TwitchProvider::poll_once()
{
	const HelixUserResult user = client_->current_user(cancel_);
	if (user.outcome == HelixOutcome::AuthRequired)
		return reading_with(ProviderState::AuthRequired, "Twitch account not connected");
	if (user.outcome != HelixOutcome::Ok || !user.user)
		return reading_with(ProviderState::Error, user.error.empty() ? "no response" : user.error);

	const bool subscribers = metric_ == Metric::Subscribers;
	if (subscribers && !user.user->can_have_subscribers())
		return reading_with(ProviderState::Error, kTwitchNeedsAffiliate);

	const std::string url = subscribers ? helix_subscriptions_url(user.user->id)
					    : helix_followers_url(user.user->id);
	const HelixResponse response = client_->get(url, cancel_);
	if (response.outcome == HelixOutcome::AuthRequired)
		return reading_with(ProviderState::AuthRequired, "Twitch account not connected");
	if (response.outcome != HelixOutcome::Ok)
		return reading_with(ProviderState::Error,
				    response.http.error.empty() ? "no response" : response.http.error);

	const int status = response.http.status;
	if (subscribers && (status == 401 || status == 403))
		return reading_with(ProviderState::Error, kTwitchNeedsAffiliate);
	if (status < 200 || status >= 300)
		return reading_with(ProviderState::Error, "HTTP " + std::to_string(status));
	const std::optional<uint64_t> total = parse_helix_total(response.http.body);
	if (!total)
		return reading_with(ProviderState::Error, "unexpected answer from Twitch");
	MetricReading reading = reading_with(ProviderState::Live);
	reading.value = total;
	return reading;
}

void TwitchProvider::publish(MetricReading reading)
{
	bool changed = false;
	{
		std::lock_guard lock(mutex_);
		changed = reading.state != reading_.state || reading.message != reading_.message;
		// Keep the last known value while failing.
		if (!reading.value)
			reading.value = reading_.value;
		reading_ = std::move(reading);
	}
	if (changed && log_) {
		const MetricReading now = latest();
		log_(std::string("Twitch ") + (metric_ == Metric::Subscribers ? "subscribers" : "followers") + ": " +
		     provider_state_name(now.state) + (now.message.empty() ? "" : " (" + now.message + ")"));
	}
}

void TwitchProvider::run()
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
		failures = state == ProviderState::Error ? failures + 1 : 0;
		publish(std::move(reading));

		const std::chrono::seconds delay = state == ProviderState::AuthRequired
							   ? auth_recheck_
							   : next_poll_delay(interval_, failures);
		std::unique_lock lock(mutex_);
		if (wake_.wait_for(lock, delay, [this] { return stop_; }))
			return;
	}
}

} // namespace goalbar
