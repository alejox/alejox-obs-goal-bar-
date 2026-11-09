// Provider adapter: Twitch followers or subscribers through Helix, using the
// shared TwitchAccount (or any TwitchHelixClient). No OBS, no Qt.
#pragma once

#include "core/cancel_token.hpp"
#include "core/metric_provider.hpp"
#include "core/poll_schedule.hpp"
#include "providers/twitch_account.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace goalbar {

// MetricReading::message when the channel cannot have subscribers.
inline constexpr const char *kTwitchNeedsAffiliate = "Subscriptions need Twitch Affiliate or Partner";

// While no account is connected the provider re-checks this often. The check
// is local (no request), so a fresh connection shows up within seconds.
inline constexpr std::chrono::seconds kTwitchAuthRecheck{3};

class TwitchProvider final : public MetricProvider {
public:
	using Logger = std::function<void(const std::string &message)>;

	// `metric` is Followers or Subscribers. Polls at once, then every
	// `interval` (at least kMinPollInterval), backing off on errors.
	TwitchProvider(std::shared_ptr<TwitchHelixClient> client, Metric metric, Logger log,
		       std::chrono::seconds interval = kDefaultPollInterval,
		       std::chrono::seconds auth_recheck = kTwitchAuthRecheck);
	// Cancels a request in flight, wakes the worker and joins it.
	~TwitchProvider() override;

	TwitchProvider(const TwitchProvider &) = delete;
	TwitchProvider &operator=(const TwitchProvider &) = delete;

	Platform platform() const override;
	Metric metric() const override;
	MetricReading latest() const override;

private:
	void run();
	MetricReading poll_once();
	void publish(MetricReading reading);

	const std::shared_ptr<TwitchHelixClient> client_;
	const Metric metric_;
	const Logger log_;
	const std::chrono::seconds interval_;
	const std::chrono::seconds auth_recheck_;

	mutable std::mutex mutex_;
	std::condition_variable wake_;
	bool stop_ = false;
	CancelToken cancel_; // aborts the request in flight on destruction
	MetricReading reading_;
	std::thread worker_; // last member: starts after everything above exists
};

} // namespace goalbar
