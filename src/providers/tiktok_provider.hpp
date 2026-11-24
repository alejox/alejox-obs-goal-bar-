// Provider adapter: TikTok followers or viewers of one creator from the room
// endpoint of tiktok.com (see core/tiktok_live.hpp). No credentials, no OBS,
// no Qt: the HTTP GET is injected (it sends tiktok_room_headers()).
#pragma once

#include "core/cancel_token.hpp"
#include "core/metric_provider.hpp"
#include "core/tiktok_live.hpp"
#include "providers/http_types.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace goalbar {

// TikTok documents no rate limits for this endpoint: poll gently.
inline constexpr std::chrono::seconds kTikTokPollInterval{30};

// Blocking GET. It must return promptly (status 0) once `cancel` fires.
using TikTokFetchRoom = std::function<HttpResult(const std::string &url, CancelToken &cancel)>;

class TikTokProvider final : public MetricProvider {
public:
	using Logger = std::function<void(const std::string &message)>;

	// `handle` is a normalized TikTok handle; `metric` is Followers or Viewers.
	// Polls at once, then every `interval`, backing off on errors, blocks and
	// 429s (see next_poll_delay).
	TikTokProvider(TikTokFetchRoom fetch, std::string handle, Metric metric, Logger log,
		     std::chrono::seconds interval = kTikTokPollInterval);
	// Cancels a request in flight, wakes the worker and joins it.
	~TikTokProvider() override;

	TikTokProvider(const TikTokProvider &) = delete;
	TikTokProvider &operator=(const TikTokProvider &) = delete;

	Platform platform() const override;
	Metric metric() const override;
	MetricReading latest() const override;

private:
	void run();
	MetricReading poll_once();
	void publish(MetricReading reading);

	const TikTokFetchRoom fetch_;
	const std::string handle_;
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
