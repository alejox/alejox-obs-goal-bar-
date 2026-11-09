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

constexpr const char *kLive = R"({"slug":"streamer","followers_count":120,)"
			      R"("livestream":{"is_live":true,"viewer_count":1532}})";
constexpr const char *kOffline = R"({"slug":"streamer","followers_count":120,"livestream":null})";

// Records every request and answers each with `answer`.
struct FakeKick {
	std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
	std::shared_ptr<std::vector<std::string>> urls = std::make_shared<std::vector<std::string>>();
	std::shared_ptr<HttpResult> answer = std::make_shared<HttpResult>(HttpResult{200, kLive, ""});

	KickFetch fetch() const
	{
		return [mutex = mutex, urls = urls, answer = answer](const std::string &url, CancelToken &) {
			std::lock_guard lock(*mutex);
			urls->push_back(url);
			return *answer;
		};
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

MetricReading reading_for(HttpResult answer, Metric metric = Metric::Followers)
{
	FakeKick kick;
	*kick.answer = std::move(answer);
	KickProvider provider(kick.fetch(), "streamer", metric, nullptr);
	return settled(provider);
}

} // namespace

TEST_CASE(kick_followers_need_no_credentials)
{
	FakeKick kick;
	KickProvider provider(kick.fetch(), "streamer", Metric::Followers, nullptr);
	const MetricReading reading = settled(provider);
	CHECK_EQ(reading.state, ProviderState::Live);
	CHECK_EQ(reading.value.value_or(0), uint64_t{120});
	CHECK_EQ(provider.platform(), Platform::Kick);
	CHECK_EQ(provider.metric(), Metric::Followers);
	std::lock_guard lock(*kick.mutex);
	REQUIRE(kick.urls->size() == 1);
	CHECK_EQ((*kick.urls)[0], kick_channel_url("streamer"));
}

TEST_CASE(kick_viewers_follow_the_live_status)
{
	MetricReading reading = reading_for(HttpResult{200, kLive, ""}, Metric::Viewers);
	CHECK_EQ(reading.state, ProviderState::Live);
	CHECK_EQ(reading.value.value_or(0), uint64_t{1532});
	reading = reading_for(HttpResult{200, kOffline, ""}, Metric::Viewers);
	CHECK_EQ(reading.state, ProviderState::NotLive);
	CHECK_EQ(reading_for(HttpResult{200, kOffline, ""}).value.value_or(0), uint64_t{120});
}

TEST_CASE(kick_errors_are_classified)
{
	const MetricReading blocked = reading_for(HttpResult{403, "<html><title>Just a moment...</title></html>", ""});
	CHECK_EQ(blocked.state, ProviderState::Error);
	CHECK_EQ(blocked.message, std::string(kKickBlocked));
	CHECK_EQ(reading_for(HttpResult{429, "", ""}).state, ProviderState::RateLimited);
	CHECK_EQ(reading_for(HttpResult{404, R"({"message":"Channel not found."})", ""}).message,
		 std::string(kKickChannelNotFound));
	const MetricReading network = reading_for(HttpResult{0, "", "timed out"});
	CHECK_EQ(network.state, ProviderState::Error);
	CHECK_EQ(network.message, std::string("timed out"));
	const MetricReading server = reading_for(HttpResult{500, "", ""});
	CHECK_EQ(server.state, ProviderState::Error);
	CHECK_EQ(server.message, std::string("HTTP 500"));
	CHECK_EQ(reading_for(HttpResult{200, "<html>", ""}).message, std::string(kKickBlocked));
}

TEST_CASE(kick_provider_stops_promptly_during_a_request)
{
	KickFetch hanging = [](const std::string &, CancelToken &cancel) {
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
		return HttpResult{0, "", "cancelled"};
	};
	auto provider = std::make_unique<KickProvider>(hanging, "streamer", Metric::Followers, nullptr);
	std::this_thread::sleep_for(30ms); // the request is in flight
	const auto start = std::chrono::steady_clock::now();
	provider.reset();
	CHECK(std::chrono::steady_clock::now() - start < 200ms);
}
