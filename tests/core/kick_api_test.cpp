#include "core/kick_api.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar;

namespace {

// Trimmed from a real GET https://kick.com/api/v2/channels/alejoxcode answer
// (2026-11-09, HTTP 200, application/json): an offline channel.
constexpr const char *kOfflineChannel = R"({
  "id": 135657770,
  "user_id": 136303981,
  "slug": "alejoxcode",
  "is_banned": false,
  "vod_enabled": true,
  "subscription_enabled": false,
  "is_affiliate": false,
  "followers_count": 3,
  "livestream": null,
  "verified": false
})";

// Trimmed from a real answer for a channel that was live (2026-11-09).
constexpr const char *kLiveChannel = R"({
  "id": 17883767,
  "slug": "sxb",
  "followers_count": 2343528,
  "livestream": {
    "id": 136853793,
    "channel_id": 17883767,
    "is_live": true,
    "start_time": "2026-11-09 11:23:44",
    "viewer_count": 159190,
    "language": "Arabic"
  }
})";

// The shape of a Cloudflare challenge page.
constexpr const char *kCloudflarePage =
	"<!DOCTYPE html><html lang=\"en-US\"><head><title>Just a moment...</title></head>"
	"<body><div id=\"challenge-error-text\">Enable JavaScript and cookies to continue</div>"
	"<script src=\"/cdn-cgi/challenge-platform/h/b/orchestrate/chl_page/v1\"></script></body></html>";

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

TEST_CASE(channel_url_uses_the_public_endpoint)
{
	CHECK_EQ(kick_channel_url("some-one"), std::string("https://kick.com/api/v2/channels/some-one"));
}

TEST_CASE(offline_channel_has_followers_and_no_viewers)
{
	const KickChannelLookup lookup = parse_kick_channel(200, kOfflineChannel);
	CHECK_EQ(lookup.status, KickChannelStatus::Found);
	REQUIRE(lookup.channel.has_value());
	CHECK_EQ(lookup.channel->followers.value_or(99), uint64_t{3});
	CHECK(!lookup.channel->is_live);
	const MetricReading followers = kick_metric_reading(lookup, Metric::Followers);
	CHECK_EQ(followers.state, ProviderState::Live);
	CHECK_EQ(followers.value.value_or(99), uint64_t{3});
	const MetricReading viewers = kick_metric_reading(lookup, Metric::Viewers);
	CHECK_EQ(viewers.state, ProviderState::NotLive);
	CHECK(!viewers.value.has_value());
}

TEST_CASE(live_channel_has_viewers)
{
	const KickChannelLookup lookup = parse_kick_channel(200, kLiveChannel);
	REQUIRE(lookup.channel.has_value());
	CHECK(lookup.channel->is_live);
	const MetricReading viewers = kick_metric_reading(lookup, Metric::Viewers);
	CHECK_EQ(viewers.state, ProviderState::Live);
	CHECK_EQ(viewers.value.value_or(0), uint64_t{159190});
	CHECK_EQ(kick_metric_reading(lookup, Metric::Followers).value.value_or(0), uint64_t{2343528});
	// Kick does not publish paid subscribers on this endpoint.
	CHECK_EQ(kick_metric_reading(lookup, Metric::Subscribers).state, ProviderState::Error);
}

TEST_CASE(live_flag_false_or_missing_viewers)
{
	const KickChannelLookup ended =
		parse_kick_channel(200, R"({"followers_count":5,"livestream":{"is_live":false,"viewer_count":7}})");
	CHECK_EQ(kick_metric_reading(ended, Metric::Viewers).state, ProviderState::NotLive);
	const KickChannelLookup no_count = parse_kick_channel(200, R"({"livestream":{"is_live":true}})");
	CHECK_EQ(kick_metric_reading(no_count, Metric::Viewers).state, ProviderState::Error);
	const KickChannelLookup bare = parse_kick_channel(200, R"({"slug":"alejoxcode"})");
	CHECK_EQ(bare.status, KickChannelStatus::Found);
	CHECK_EQ(kick_metric_reading(bare, Metric::Followers).state, ProviderState::Error);
	CHECK_EQ(kick_metric_reading(bare, Metric::Viewers).state, ProviderState::NotLive);
	const KickChannelLookup negative = parse_kick_channel(200, R"({"followers_count":-1})");
	CHECK_EQ(kick_metric_reading(negative, Metric::Followers).state, ProviderState::Error);
}

TEST_CASE(blocked_answers_are_not_a_zero)
{
	const KickChannelLookup challenge = parse_kick_channel(403, kCloudflarePage);
	CHECK_EQ(challenge.status, KickChannelStatus::Blocked);
	CHECK_EQ(parse_kick_channel(403, "").status, KickChannelStatus::Blocked);
	CHECK_EQ(parse_kick_channel(503, kCloudflarePage).status, KickChannelStatus::Blocked);
	CHECK_EQ(parse_kick_channel(200, kCloudflarePage).status, KickChannelStatus::Blocked);
	for (Metric metric : {Metric::Followers, Metric::Viewers}) {
		const MetricReading reading = kick_metric_reading(challenge, metric);
		CHECK_EQ(reading.state, ProviderState::Error);
		CHECK_EQ(reading.message, std::string(kKickBlocked));
		CHECK(!reading.value.has_value());
	}
}

TEST_CASE(other_answers_are_classified)
{
	const KickChannelLookup missing =
		parse_kick_channel(404, R"({"error":"Not Found","message":"Channel not found.","status":404})");
	CHECK_EQ(missing.status, KickChannelStatus::NotFound);
	CHECK_EQ(kick_metric_reading(missing, Metric::Followers).message, std::string(kKickChannelNotFound));

	const KickChannelLookup limited = parse_kick_channel(429, "");
	CHECK_EQ(limited.status, KickChannelStatus::RateLimited);
	CHECK_EQ(kick_metric_reading(limited, Metric::Viewers).state, ProviderState::RateLimited);

	const KickChannelLookup server = parse_kick_channel(500, R"({"message":"Server Error"})");
	CHECK_EQ(server.status, KickChannelStatus::HttpError);
	CHECK_EQ(kick_metric_reading(server, Metric::Followers).message, std::string("HTTP 500"));

	const KickChannelLookup list = parse_kick_channel(200, "[]");
	CHECK_EQ(list.status, KickChannelStatus::Malformed);
	CHECK_EQ(kick_metric_reading(list, Metric::Followers).state, ProviderState::Error);
}
