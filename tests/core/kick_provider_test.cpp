#include "providers/kick_provider.hpp"

#include "test_harness.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

constexpr const char *kLive =
	R"({"data":[{"slug":"streamer","active_subscribers_count":87,"stream":{"is_live":true,"viewer_count":1532}}]})";
constexpr const char *kOffline =
	R"({"data":[{"slug":"streamer","active_subscribers_count":87,"stream":{"is_live":false,"viewer_count":0}}]})";

struct FakeClient final : KickApiClient {
	std::mutex mutex;
	KickResponse answer{KickOutcome::Ok, HttpResult{200, kLive, ""}, std::nullopt};
	std::vector<std::string> urls;

	KickResponse get(const std::string &url, CancelToken &) override
	{
		std::lock_guard lock(mutex);
		urls.push_back(url);
		return answer;
	}
};

// Blocks every request until it is cancelled, like a hung network call.
struct HangingClient final : KickApiClient {
	KickResponse get(const std::string &, CancelToken &cancel) override
	{
		std::mutex m;
		std::condition_variable cv;
		bool aborted = false;
		AbortHook hook(&cancel, [&] {
			std::lock_guard lock(m);
			aborted = true;
			cv.notify_all();
		});
		std::unique_lock lock(m);
		if (hook.armed())
			cv.wait_for(lock, 10s, [&] { return aborted; });
		lock.unlock();
		hook.disarm();
		return KickResponse{KickOutcome::Failed, HttpResult{0, "", "cancelled"}, std::nullopt};
	}
};

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

MetricReading reading_for(KickResponse answer, Metric metric = Metric::Viewers)
{
	auto client = std::make_shared<FakeClient>();
	client->answer = std::move(answer);
	KickProvider provider(client, "streamer", metric, nullptr);
	return settled(provider);
}

} // namespace

TEST_CASE(kick_viewers_become_a_live_reading)
{
	auto client = std::make_shared<FakeClient>();
	KickProvider provider(client, "streamer", Metric::Viewers, nullptr);
	const MetricReading reading = settled(provider);
	CHECK_EQ(reading.state, ProviderState::Live);
	CHECK_EQ(reading.value.value_or(0), uint64_t{1532});
	CHECK_EQ(provider.platform(), Platform::Kick);
	CHECK_EQ(provider.metric(), Metric::Viewers);
	std::lock_guard lock(client->mutex);
	REQUIRE(client->urls.size() == 1);
	CHECK_EQ(client->urls[0], kick_channel_url("streamer"));
}

TEST_CASE(kick_subscribers_are_read_even_offline)
{
	const MetricReading reading =
		reading_for(KickResponse{KickOutcome::Ok, HttpResult{200, kOffline, ""}, std::nullopt},
			    Metric::Subscribers);
	CHECK_EQ(reading.state, ProviderState::Live);
	CHECK_EQ(reading.value.value_or(0), uint64_t{87});
}

TEST_CASE(kick_offline_viewers_are_not_live)
{
	const MetricReading reading =
		reading_for(KickResponse{KickOutcome::Ok, HttpResult{200, kOffline, ""}, std::nullopt});
	CHECK_EQ(reading.state, ProviderState::NotLive);
}

TEST_CASE(kick_account_problems_require_auth)
{
	MetricReading reading = reading_for(KickResponse{KickOutcome::NotSetUp, {}, std::nullopt});
	CHECK_EQ(reading.state, ProviderState::AuthRequired);
	CHECK_EQ(reading.message, std::string(kKickNotSetUp));
	reading = reading_for(KickResponse{KickOutcome::BadCredentials, {}, std::nullopt});
	CHECK_EQ(reading.state, ProviderState::AuthRequired);
	CHECK_EQ(reading.message, std::string(kKickBadCredentials));
}

TEST_CASE(kick_errors_are_classified)
{
	CHECK_EQ(reading_for(KickResponse{KickOutcome::Ok, HttpResult{200, R"({"data":[]})", ""}, std::nullopt})
			 .message,
		 std::string(kKickChannelNotFound));
	CHECK_EQ(reading_for(KickResponse{KickOutcome::Ok, HttpResult{404, "", ""}, std::nullopt}).message,
		 std::string(kKickChannelNotFound));
	const MetricReading limited =
		reading_for(KickResponse{KickOutcome::Ok, HttpResult{429, "", ""}, std::nullopt});
	CHECK_EQ(limited.state, ProviderState::RateLimited);
	CHECK_EQ(reading_for(KickResponse{KickOutcome::RateLimited, {}, std::nullopt}).state,
		 ProviderState::RateLimited);
	const MetricReading network =
		reading_for(KickResponse{KickOutcome::Failed, HttpResult{0, "", "timed out"}, std::nullopt});
	CHECK_EQ(network.state, ProviderState::Error);
	CHECK_EQ(network.message, std::string("timed out"));
	const MetricReading server =
		reading_for(KickResponse{KickOutcome::Ok, HttpResult{500, "", ""}, std::nullopt});
	CHECK_EQ(server.state, ProviderState::Error);
	CHECK_EQ(server.message, std::string("HTTP 500"));
	CHECK_EQ(reading_for(KickResponse{KickOutcome::Ok, HttpResult{200, "<html>", ""}, std::nullopt}).state,
		 ProviderState::Error);
}

TEST_CASE(kick_delay_backs_off_and_honors_retry_after)
{
	CHECK_EQ(kick_next_delay(ProviderState::Live, 0, 20s, 3s, std::nullopt).count(), 20LL);
	CHECK_EQ(kick_next_delay(ProviderState::NotLive, 0, 20s, 3s, std::nullopt).count(), 20LL);
	CHECK_EQ(kick_next_delay(ProviderState::AuthRequired, 0, 20s, 3s, std::nullopt).count(), 3LL);
	CHECK_EQ(kick_next_delay(ProviderState::RateLimited, 1, 20s, 3s, std::nullopt).count(), 40LL);
	CHECK_EQ(kick_next_delay(ProviderState::RateLimited, 2, 20s, 3s, std::nullopt).count(), 80LL);
	CHECK_EQ(kick_next_delay(ProviderState::RateLimited, 1, 20s, 3s, 120s).count(), 120LL);
	CHECK_EQ(kick_next_delay(ProviderState::RateLimited, 3, 20s, 3s, 10s).count(), 160LL);
	CHECK_EQ(kick_next_delay(ProviderState::Error, 9, 20s, 3s, std::nullopt).count(), 300LL);
	// A Retry-After hint is capped like any backoff.
	CHECK_EQ(kick_next_delay(ProviderState::RateLimited, 1, 20s, 3s, 3600s).count(), 300LL);
}

TEST_CASE(kick_provider_stops_promptly_during_a_request)
{
	auto client = std::make_shared<HangingClient>();
	auto provider = std::make_unique<KickProvider>(client, "streamer", Metric::Viewers, nullptr);
	std::this_thread::sleep_for(30ms); // the request is in flight
	const auto start = std::chrono::steady_clock::now();
	provider.reset();
	CHECK(std::chrono::steady_clock::now() - start < 200ms);
}
