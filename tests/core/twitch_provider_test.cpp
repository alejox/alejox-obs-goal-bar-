#include "providers/twitch_provider.hpp"

#include "test_harness.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

struct FakeClient final : TwitchHelixClient {
	std::mutex mutex;
	HelixUserResult user{HelixOutcome::Ok, TwitchUser{"42", "streamer", "Streamer", "affiliate"}, ""};
	HelixResponse answer{HelixOutcome::Ok, HttpResult{200, R"({"total":1234,"data":[]})", ""}};
	std::vector<std::string> urls;

	HelixUserResult current_user(CancelToken &) override
	{
		std::lock_guard lock(mutex);
		return user;
	}
	HelixResponse get(const std::string &url, CancelToken &) override
	{
		std::lock_guard lock(mutex);
		urls.push_back(url);
		return answer;
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

} // namespace

TEST_CASE(followers_total_becomes_a_live_reading)
{
	auto client = std::make_shared<FakeClient>();
	TwitchProvider provider(client, Metric::Followers, nullptr);
	const MetricReading reading = settled(provider);
	CHECK_EQ(reading.state, ProviderState::Live);
	CHECK_EQ(reading.value.value_or(0), uint64_t{1234});
	CHECK_EQ(provider.platform(), Platform::Twitch);
	std::lock_guard lock(client->mutex);
	REQUIRE(client->urls.size() == 1);
	CHECK_EQ(client->urls[0], helix_followers_url("42"));
}

TEST_CASE(subscribers_read_the_subscriptions_total)
{
	auto client = std::make_shared<FakeClient>();
	TwitchProvider provider(client, Metric::Subscribers, nullptr);
	CHECK_EQ(settled(provider).value.value_or(0), uint64_t{1234});
	std::lock_guard lock(client->mutex);
	CHECK_EQ(client->urls.at(0), helix_subscriptions_url("42"));
}

TEST_CASE(subscribers_need_affiliate_or_partner)
{
	auto client = std::make_shared<FakeClient>();
	client->user.user->broadcaster_type = "";
	TwitchProvider provider(client, Metric::Subscribers, nullptr);
	const MetricReading reading = settled(provider);
	CHECK_EQ(reading.state, ProviderState::Error);
	CHECK_EQ(reading.message, std::string(kTwitchNeedsAffiliate));
	std::lock_guard lock(client->mutex);
	CHECK(client->urls.empty());
}

TEST_CASE(forbidden_subscriptions_report_the_affiliate_message)
{
	auto client = std::make_shared<FakeClient>();
	client->answer.http = HttpResult{403, R"({"status":403})", ""};
	TwitchProvider provider(client, Metric::Subscribers, nullptr);
	CHECK_EQ(settled(provider).message, std::string(kTwitchNeedsAffiliate));
}

TEST_CASE(missing_account_reports_auth_required_and_recovers)
{
	auto client = std::make_shared<FakeClient>();
	{
		std::lock_guard lock(client->mutex);
		client->user = HelixUserResult{HelixOutcome::AuthRequired, std::nullopt, ""};
	}
	TwitchProvider provider(client, Metric::Followers, nullptr, 30s, 0s);
	CHECK_EQ(settled(provider).state, ProviderState::AuthRequired);
	{
		std::lock_guard lock(client->mutex);
		client->user = HelixUserResult{HelixOutcome::Ok, TwitchUser{"42", "s", "S", ""}, ""};
	}
	const auto deadline = std::chrono::steady_clock::now() + 2s;
	while (provider.latest().state != ProviderState::Live && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(5ms);
	CHECK_EQ(provider.latest().state, ProviderState::Live);
}

TEST_CASE(http_errors_become_error_readings)
{
	auto client = std::make_shared<FakeClient>();
	client->answer.http = HttpResult{500, "", ""};
	TwitchProvider provider(client, Metric::Followers, nullptr);
	CHECK_EQ(settled(provider).message, std::string("HTTP 500"));
}

TEST_CASE(twitch_provider_stops_quickly)
{
	const auto start = std::chrono::steady_clock::now();
	{
		TwitchProvider provider(std::make_shared<FakeClient>(), Metric::Followers, nullptr, 30s);
		settled(provider);
	}
	CHECK(std::chrono::steady_clock::now() - start < 1s);
}
