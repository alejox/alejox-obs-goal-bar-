#include "providers/youtube_public_provider.hpp"

#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

constexpr const char *kLivePage = R"(<link rel="canonical" href="https://www.youtube.com/watch?v=4t4AQ15yj1M">)"
				  R"({"isLiveNow":true,"likeCount":"42",)"
				  R"("subscriberCountText":{"simpleText":"12.3K subscribers"}})";

// Polls `latest()` until it leaves Connecting, for at most two seconds.
MetricReading settled(const MetricProvider &provider)
{
	const auto deadline = std::chrono::steady_clock::now() + 2s;
	MetricReading reading = provider.latest();
	while (reading.state == ProviderState::Connecting && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(5ms);
		reading = provider.latest();
	}
	return reading;
}

PageFetcher serving(int status, std::string body, std::atomic<int> *calls = nullptr, std::string *url = nullptr)
{
	return [=](const std::string &requested) {
		if (calls)
			++*calls;
		if (url)
			*url = requested;
		return HttpResult{status, body, status ? "" : "offline"};
	};
}

} // namespace

TEST_CASE(snapshot_maps_to_likes_reading)
{
	YoutubeLiveSnapshot live{true, std::string("4t4AQ15yj1M"), uint64_t{42}, uint64_t{12300}};
	const MetricReading likes = reading_from_snapshot(live, Metric::Likes);
	CHECK_EQ(likes.state, ProviderState::Live);
	CHECK_EQ(likes.value.value_or(0), uint64_t{42});

	const MetricReading offline = reading_from_snapshot(YoutubeLiveSnapshot{}, Metric::Likes);
	CHECK_EQ(offline.state, ProviderState::NotLive);
	CHECK(!offline.value.has_value());

	live.likes.reset();
	const MetricReading hidden = reading_from_snapshot(live, Metric::Likes);
	CHECK_EQ(hidden.state, ProviderState::Live);
	CHECK(!hidden.value.has_value());
}

TEST_CASE(snapshot_maps_to_subscribers_reading_even_when_offline)
{
	YoutubeLiveSnapshot channel;
	channel.subscribers = 12300;
	const MetricReading subs = reading_from_snapshot(channel, Metric::Subscribers);
	CHECK_EQ(subs.state, ProviderState::Live);
	CHECK_EQ(subs.value.value_or(0), uint64_t{12300});

	CHECK_EQ(reading_from_snapshot(YoutubeLiveSnapshot{}, Metric::Subscribers).state, ProviderState::Error);
}

TEST_CASE(provider_fetches_the_live_page_and_publishes_likes)
{
	std::atomic<int> calls{0};
	std::string url;
	YoutubePublicProvider provider("https://www.youtube.com/@LofiGirl", Metric::Likes,
				       serving(200, kLivePage, &calls, &url), nullptr);
	const MetricReading reading = settled(provider);
	CHECK_EQ(reading.state, ProviderState::Live);
	CHECK_EQ(reading.value.value_or(0), uint64_t{42});
	CHECK_EQ(calls.load(), 1);
	CHECK_EQ(url, std::string("https://www.youtube.com/@LofiGirl/live?hl=en"));
	CHECK_EQ(provider.platform(), Platform::YouTube);
}

TEST_CASE(provider_reports_network_and_http_errors)
{
	YoutubePublicProvider offline("https://www.youtube.com/@x", Metric::Likes, serving(0, ""), nullptr);
	CHECK_EQ(settled(offline).state, ProviderState::Error);
	CHECK_EQ(settled(offline).message, std::string("offline"));

	YoutubePublicProvider refused("https://www.youtube.com/@x", Metric::Likes, serving(429, ""), nullptr);
	CHECK_EQ(settled(refused).message, std::string("HTTP 429"));
}

TEST_CASE(provider_logs_state_changes_once)
{
	std::atomic<int> logs{0};
	{
		YoutubePublicProvider provider("https://www.youtube.com/@x", Metric::Likes, serving(200, kLivePage),
					       [&](const std::string &) { ++logs; });
		settled(provider);
	}
	CHECK_EQ(logs.load(), 1);
}

TEST_CASE(provider_stops_quickly_between_polls)
{
	const auto start = std::chrono::steady_clock::now();
	{
		YoutubePublicProvider provider("https://www.youtube.com/@x", Metric::Likes, serving(200, kLivePage),
					       nullptr, 30s);
		settled(provider);
	}
	CHECK(std::chrono::steady_clock::now() - start < 1s);
}
