#include "core/kick_api.hpp"

#include "test_harness.hpp"

#include <chrono>
#include <string>

using namespace goalbar;
using namespace std::chrono_literals;

namespace {

// Trimmed from the GET /public/v1/channels schema on docs.kick.com/apis/channels.
constexpr const char *kLiveChannel = R"({
  "data": [{
    "broadcaster_user_id": 123,
    "slug": "streamer",
    "stream_title": "Hello",
    "active_subscribers_count": 87,
    "active_gifted_subscribers_count": 3,
    "canceled_subscribers_count": 1,
    "category": {"id": 1, "name": "Just Chatting", "thumbnail": ""},
    "stream": {"is_live": true, "viewer_count": 1532, "is_mature": false, "language": "en",
               "start_time": "2026-11-09T18:00:00Z", "custom_tags": []}
  }],
  "message": "OK"
})";

constexpr const char *kOfflineChannel = R"({
  "data": [{"slug": "streamer", "active_subscribers_count": 87,
            "stream": {"is_live": false, "viewer_count": 0}}],
  "message": "OK"
})";

} // namespace

TEST_CASE(slug_accepts_names_handles_and_urls)
{
	CHECK_EQ(normalize_kick_slug("streamer").value_or("?"), std::string("streamer"));
	CHECK_EQ(normalize_kick_slug("  @Streamer ").value_or("?"), std::string("streamer"));
	CHECK_EQ(normalize_kick_slug("kick.com/some-one").value_or("?"), std::string("some-one"));
	CHECK_EQ(normalize_kick_slug("https://www.kick.com/Some_One/").value_or("?"), std::string("some_one"));
	CHECK_EQ(normalize_kick_slug("https://kick.com/streamer?tab=about").value_or("?"), std::string("streamer"));
}

TEST_CASE(slug_rejects_invalid_input)
{
	CHECK(!normalize_kick_slug("").has_value());
	CHECK(!normalize_kick_slug("@").has_value());
	CHECK(!normalize_kick_slug("kick.com/").has_value());
	CHECK(!normalize_kick_slug("two words").has_value());
	CHECK(!normalize_kick_slug("a/b").has_value());
	CHECK(!normalize_kick_slug("abcdefghijklmnopqrstuvwxyz").has_value()); // 26 > 25
	CHECK(normalize_kick_slug("abcdefghijklmnopqrstuvwxy").has_value());
}

TEST_CASE(channel_url_and_token_body)
{
	CHECK_EQ(kick_channel_url("some-one"), std::string("https://api.kick.com/public/v1/channels?slug=some-one"));
	CHECK_EQ(kick_client_credentials_body("id 1", "s&cret"),
		 std::string("grant_type=client_credentials&client_id=id%201&client_secret=s%26cret"));
}

TEST_CASE(token_success_is_parsed)
{
	const KickTokenResponse response = parse_kick_token_response(
		200, R"({"access_token":"tok","token_type":"Bearer","expires_in":3600})");
	REQUIRE(response.token.has_value());
	CHECK_EQ(response.token->access_token, std::string("tok"));
	CHECK_EQ(response.token->expires_in.count(), 3600LL);
	CHECK_EQ(response.error, KickTokenError::None);
}

TEST_CASE(token_failures_are_classified)
{
	CHECK_EQ(parse_kick_token_response(0, "").error, KickTokenError::Network);
	const KickTokenResponse bad = parse_kick_token_response(400, R"({"error":"Invalid request"})");
	CHECK(!bad.token.has_value());
	CHECK_EQ(bad.error, KickTokenError::InvalidClient);
	CHECK_EQ(bad.message, std::string("Invalid request"));
	// Real answer of id.kick.com/oauth/token for unknown credentials (2026-11-09).
	const KickTokenResponse unknown = parse_kick_token_response(
		401, R"({"error":"invalid_client","error_description":"Client authentication failed"})");
	CHECK_EQ(unknown.error, KickTokenError::InvalidClient);
	CHECK_EQ(unknown.message, std::string("Client authentication failed"));
	CHECK_EQ(parse_kick_token_response(429, "").error, KickTokenError::RateLimited);
	CHECK_EQ(parse_kick_token_response(503, "oops").error, KickTokenError::Other);
	// 200 without a token is not a success.
	CHECK_EQ(parse_kick_token_response(200, R"({"token_type":"Bearer"})").error, KickTokenError::Other);
}

TEST_CASE(token_deadline_keeps_a_margin)
{
	const auto t0 = std::chrono::steady_clock::time_point{} + 1000s;
	CHECK(kick_token_deadline(t0, 3600s) == t0 + 3300s);
	CHECK(kick_token_deadline(t0, 200s) == t0 + 100s); // half the lifetime when shorter than the margin
	CHECK(!kick_token_deadline(t0, 0s).has_value());
}

TEST_CASE(live_channel_is_parsed)
{
	const KickChannelLookup lookup = parse_kick_channels(kLiveChannel, "streamer");
	CHECK_EQ(lookup.status, KickChannelStatus::Found);
	REQUIRE(lookup.channel.has_value());
	CHECK(lookup.channel->is_live);
	CHECK_EQ(lookup.channel->viewer_count, uint64_t{1532});
	CHECK_EQ(lookup.channel->active_subscribers.value_or(0), uint64_t{87});
}

TEST_CASE(offline_and_missing_channels)
{
	const KickChannelLookup offline = parse_kick_channels(kOfflineChannel, "streamer");
	REQUIRE(offline.channel.has_value());
	CHECK(!offline.channel->is_live);
	CHECK_EQ(parse_kick_channels(R"({"data":[],"message":"OK"})", "nobody").status, KickChannelStatus::NotFound);
	CHECK_EQ(parse_kick_channels("<html>", "streamer").status, KickChannelStatus::Malformed);
	CHECK_EQ(parse_kick_channels(R"({"message":"OK"})", "streamer").status, KickChannelStatus::Malformed);
	// No stream object: not live.
	const KickChannelLookup bare = parse_kick_channels(R"({"data":[{"slug":"streamer"}]})", "streamer");
	REQUIRE(bare.channel.has_value());
	CHECK(!bare.channel->is_live);
	CHECK(!bare.channel->active_subscribers.has_value());
}

TEST_CASE(channel_is_picked_by_slug)
{
	const KickChannelLookup lookup = parse_kick_channels(
		R"({"data":[{"slug":"other","stream":{"is_live":true,"viewer_count":5}},)"
		R"({"slug":"Streamer","stream":{"is_live":true,"viewer_count":9}}]})",
		"streamer");
	REQUIRE(lookup.channel.has_value());
	CHECK_EQ(lookup.channel->viewer_count, uint64_t{9});
}

TEST_CASE(readings_follow_metric_and_live_status)
{
	KickChannel channel{"streamer", true, 1532, uint64_t{87}};
	MetricReading viewers = kick_metric_reading(channel, Metric::Viewers);
	CHECK_EQ(viewers.state, ProviderState::Live);
	CHECK_EQ(viewers.value.value_or(0), uint64_t{1532});
	MetricReading subs = kick_metric_reading(channel, Metric::Subscribers);
	CHECK_EQ(subs.state, ProviderState::Live);
	CHECK_EQ(subs.value.value_or(0), uint64_t{87});

	channel.is_live = false;
	viewers = kick_metric_reading(channel, Metric::Viewers);
	CHECK_EQ(viewers.state, ProviderState::NotLive);
	CHECK(!viewers.value.has_value());
	CHECK_EQ(kick_metric_reading(channel, Metric::Subscribers).state, ProviderState::Live);

	channel.active_subscribers.reset();
	CHECK_EQ(kick_metric_reading(channel, Metric::Subscribers).state, ProviderState::Error);
}
