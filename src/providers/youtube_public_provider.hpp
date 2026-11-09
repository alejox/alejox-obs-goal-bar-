// Provider adapter: YouTube in public mode (reads the channel's public `/live`
// page, no login, no API key). No OBS, no Qt: HTTP and logging are injected.
#pragma once

#include "core/cancel_token.hpp"
#include "core/metric_provider.hpp"
#include "core/poll_schedule.hpp"
#include "core/youtube_page.hpp"
#include "providers/http_types.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace goalbar {

// Blocking GET of one URL; called only from the provider's worker thread.
// It must return promptly (status 0) once `cancel` is cancelled.
using PageFetcher = std::function<HttpResult(const std::string &url, CancelToken &cancel)>;
using ProviderLogger = std::function<void(const std::string &message)>;

// Maps one parsed page onto a reading for `metric` (Likes or Subscribers).
// Subscribers are readable on any channel page, so they are Live whenever the
// count is present; Likes are Live only while a broadcast is on air.
MetricReading reading_from_snapshot(const YoutubeLiveSnapshot &snapshot, Metric metric);

class YoutubePublicProvider final : public MetricProvider {
public:
	// Starts polling `youtube_live_url(channel_url)` at once and then every
	// `interval` (at least kMinPollInterval), backing off on errors.
	YoutubePublicProvider(std::string channel_url, Metric metric, PageFetcher fetch, ProviderLogger log,
			      std::chrono::seconds interval = kDefaultPollInterval);
	// Cancels a request in flight, wakes the worker and joins it.
	~YoutubePublicProvider() override;

	YoutubePublicProvider(const YoutubePublicProvider &) = delete;
	YoutubePublicProvider &operator=(const YoutubePublicProvider &) = delete;

	Platform platform() const override;
	Metric metric() const override;
	MetricReading latest() const override;

private:
	void run();
	MetricReading poll_once();
	void publish(MetricReading reading);

	const std::string url_;
	const Metric metric_;
	const PageFetcher fetch_;
	const ProviderLogger log_;
	const std::chrono::seconds interval_;

	mutable std::mutex mutex_;
	std::condition_variable wake_;
	bool stop_ = false;
	CancelToken cancel_; // aborts the fetch in flight on destruction
	MetricReading reading_;
	std::thread worker_; // last member: starts after everything above exists
};

} // namespace goalbar
