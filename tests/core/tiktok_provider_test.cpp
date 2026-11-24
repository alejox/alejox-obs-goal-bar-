#include "providers/tiktok_provider.hpp"

#include "test_harness.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

constexpr const char *kLive =
	R"({"data":{"user":{"roomId":"7441234567890123456"},"stats":{"followerCount":120},)"
	R"("liveRoom":{"status":2,"liveRoomStats":{"userCount":1532}}},"message":"","statusCode":0})";
constexpr const char *kOffline =
	R"({"data":{"user":{"roomId":"7441234567890123456"},"stats":{"followerCount":120},)"
	R"("liveRoom":{"status":4,"liveRoomStats":{"userCount":1}}},"message":"","statusCode":0})";

struct FakeTikTok {
	std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
	std::shared_ptr<std::vector<std::string>> urls = std::make_shared<std::vector<std::string>>();
	std::shared_ptr<HttpResult> answer = std::make_shared<HttpResult>(HttpResult{200, kLive, ""});

	TikTokFetchRoom fetch() const
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
	FakeTikTok tiktok;
	*tiktok.answer = std::move(answer);
	TikTokProvider provider(tiktok.fetch(), "creator", metric, nullptr);
	return settled(provider);
}

} // namespace

TEST_CASE(tiktok_followers_need_no_account)
{
	FakeTikTok tiktok;
	TikTokProvider provider(tiktok.fetch(), "creator", Metric::Followers, nullptr);
	const MetricReading reading = settled(provider);
	CHECK_EQ(reading.state, ProviderState::Live);
	CHECK_EQ(reading.value.value_or(0), uint64_t{120});
	CHECK_EQ(provider.platform(), Platform::TikTok);
	CHECK_EQ(provider.metric(), Metric::Followers);
	std::lock_guard lock(*tiktok.mutex);
	REQUIRE(tiktok.urls->size() == 1);
	CHECK_EQ((*tiktok.urls)[0], tiktok_room_url("creator"));
}

TEST_CASE(tiktok_viewers_follow_the_live_status)
{
	MetricReading reading = reading_for(HttpResult{200, kLive, ""}, Metric::Viewers);
	CHECK_EQ(reading.state, ProviderState::Live);
	CHECK_EQ(reading.value.value_or(0), uint64_t{1532});
	reading = reading_for(HttpResult{200, kOffline, ""}, Metric::Viewers);
	CHECK_EQ(reading.state, ProviderState::NotLive);
	CHECK(!reading.value.has_value());
}

TEST_CASE(tiktok_errors_are_classified)
{
	CHECK_EQ(reading_for(HttpResult{200, "<!DOCTYPE html>", ""}).message, std::string(kTikTokBlocked));
	CHECK_EQ(reading_for(HttpResult{429, "", ""}).state, ProviderState::RateLimited);
	CHECK_EQ(reading_for(HttpResult{200, R"({"data":null,"message":"user_not_found"})", ""}).message,
		 std::string(kTikTokUserNotFound));
	const MetricReading network = reading_for(HttpResult{0, "", "timed out"});
	CHECK_EQ(network.state, ProviderState::Error);
	CHECK_EQ(network.message, std::string("timed out"));
}
