// Provider adapter: Kick followers or viewers of one channel from the public
// channel endpoint (see core/kick_api.hpp). No credentials, no OBS, no Qt:
// the HTTP GET is injected.
#pragma once

#include "core/cancel_token.hpp"
#include "core/kick_api.hpp"
#include "core/metric_provider.hpp"
#include "providers/http_types.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace goalbar {

// Kick documents no rate limits: poll gently (raised to kMinPollInterval).
inline constexpr std::chrono::seconds kKickPollInterval{20};

// Blocking GET. It must return promptly (status 0) once `cancel` fires.
using KickFetch = std::function<HttpResult(const std::string &url, CancelToken &cancel)>;

class KickProvider final : public MetricProvider {
public:
	using Logger = std::function<void(const std::string &message)>;

	// `slug` is a normalized channel slug; `metric` is Followers or Viewers.
	// Polls at once, then every `interval`, backing off on errors, blocks and
	// 429s (see next_poll_delay).
	KickProvider(KickFetch fetch, std::string slug, Metric metric, Logger log,
		     std::chrono::seconds interval = kKickPollInterval);
	// Cancels a request in flight, wakes the worker and joins it.
	~KickProvider() override;

	KickProvider(const KickProvider &) = delete;
	KickProvider &operator=(const KickProvider &) = delete;

	Platform platform() const override;
	Metric metric() const override;
	MetricReading latest() const override;

private:
	void run();
	MetricReading poll_once();
	void publish(MetricReading reading);

	const KickFetch fetch_;
	const std::string slug_;
	const Metric metric_;
	const Logger log_;
	const std::chrono::seconds interval_;

	mutable std::mutex mutex_;
	std::condition_variable wake_;
	bool stop_ = false;
	CancelToken cancel_; // aborts the request in flight on destruction
	MetricReading reading_;
	std::thread worker_; // last member: starts after everything above exists
};

} // namespace goalbar
