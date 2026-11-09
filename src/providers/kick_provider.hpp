// Provider adapter: Kick viewers or active subscribers of one channel through
// the official public API, using the shared KickAccount (or any
// KickApiClient). No OBS, no Qt.
#pragma once

#include "core/cancel_token.hpp"
#include "core/metric_provider.hpp"
#include "core/poll_schedule.hpp"
#include "providers/kick_account.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace goalbar {

// Kick documents no rate limits: poll gently (raised to kMinPollInterval).
inline constexpr std::chrono::seconds kKickPollInterval{20};

// While the app is not set up (or was rejected) the provider re-checks this
// often. The check is local (no request), so saved credentials show up fast.
inline constexpr std::chrono::seconds kKickAuthRecheck{3};

class KickProvider final : public MetricProvider {
public:
	using Logger = std::function<void(const std::string &message)>;

	// `slug` is a normalized channel slug; `metric` is Viewers or Subscribers.
	// Polls at once, then every `interval`, backing off on errors and 429s.
	KickProvider(std::shared_ptr<KickApiClient> client, std::string slug, Metric metric, Logger log,
		     std::chrono::seconds interval = kKickPollInterval,
		     std::chrono::seconds auth_recheck = kKickAuthRecheck);
	// Cancels a request in flight, wakes the worker and joins it.
	~KickProvider() override;

	KickProvider(const KickProvider &) = delete;
	KickProvider &operator=(const KickProvider &) = delete;

	Platform platform() const override;
	Metric metric() const override;
	MetricReading latest() const override;

	struct Poll {
		MetricReading reading;
		std::optional<std::chrono::seconds> retry_after;
	};

private:
	void run();
	Poll poll_once();
	void publish(MetricReading reading);

	const std::shared_ptr<KickApiClient> client_;
	const std::string slug_;
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

// Delay before the next poll after a reading in `state`, with `failures`
// consecutive errors or rate limits so far. A Retry-After hint is honored
// when it is longer than the backoff.
std::chrono::seconds kick_next_delay(ProviderState state, unsigned failures, std::chrono::seconds interval,
				     std::chrono::seconds auth_recheck,
				     std::optional<std::chrono::seconds> retry_after);

} // namespace goalbar
