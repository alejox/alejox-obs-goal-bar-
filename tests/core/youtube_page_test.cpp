#include "core/youtube_page.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar;

namespace {

// Hand-trimmed fragments of real pages: only the markers the parser reads.
constexpr const char *kLivePage =
	R"(<html><head><link rel="canonical" href="https://www.youtube.com/watch?v=4t4AQ15yj1M">)"
	R"(</head><script>var ytInitialPlayerResponse = {"videoDetails":{"videoId":"4t4AQ15yj1M",)"
	R"("isLiveContent":true},"liveBroadcastDetails":{"isLiveNow":true,)"
	R"("startTimestamp":"2026-11-03T18:00:39+00:00"},"externalVideoId":"4t4AQ15yj1M","likeCount":"5150"};)"
	R"(var ytInitialData = {"accessibilityText":"like this video along with 5,150 other people",)"
	R"("subscriberCountText":{"accessibility":{"accessibilityData":{"label":"15.8 million subscribers"}},)"
	R"("simpleText":"15.8M subscribers"}};</script></html>)";

constexpr const char *kChannelPage =
	R"(<html><head><link rel="canonical" href="https://www.youtube.com/channel/UCSJ4gkVC6NrvII8umztf0Ow">)"
	R"(</head><script>var ytInitialData = {"metadataRows":[{"metadataParts":[{"text":)"
	R"({"content":"12.3K subscribers"}}]}]};</script></html>)";

constexpr const char *kUpcomingPage =
	R"(<link rel="canonical" href="https://www.youtube.com/watch?v=abcdefghijk">)"
	R"({"videoDetails":{"isLiveContent":true,"isUpcoming":true},"liveBroadcastDetails":{"isLiveNow":false,)"
	R"("startTimestamp":"2026-12-01T18:00:00+00:00"},"likeCount":"12",)"
	R"("subscriberCountText":{"accessibility":{"accessibilityData":{"label":"987 subscribers"}}}})";

constexpr const char *kHiddenLikesPage =
	R"(<link rel="canonical" href="https://www.youtube.com/watch?v=Zz_9-AbCdEf">)"
	R"({"videoDetails":{"isLiveContent":true},"liveBroadcastDetails":{"isLiveNow":true},)"
	R"("accessibilityText":"I like this",)"
	R"("subscriberCountText":{"accessibility":{"accessibilityData":{"label":"1 subscriber"}}}})";

constexpr const char *kAccessibilityLikesPage =
	R"(<link rel="canonical" href="https://www.youtube.com/watch?v=4t4AQ15yj1M">)"
	R"({"liveBroadcastDetails":{"isLiveNow":true},)"
	R"("accessibilityText":"like this video along with 1,234,567 other people"})";

} // namespace

TEST_CASE(live_page_reports_video_likes_and_subscribers)
{
	const YoutubeLiveSnapshot snap = parse_youtube_live_page(kLivePage);
	CHECK(snap.live);
	REQUIRE(snap.video_id.has_value());
	CHECK_EQ(*snap.video_id, std::string("4t4AQ15yj1M"));
	REQUIRE(snap.likes.has_value());
	CHECK_EQ(*snap.likes, uint64_t{5150});
	REQUIRE(snap.subscribers.has_value());
	CHECK_EQ(*snap.subscribers, uint64_t{15800000});
}

TEST_CASE(channel_page_without_live_is_not_live_but_has_subscribers)
{
	const YoutubeLiveSnapshot snap = parse_youtube_live_page(kChannelPage);
	CHECK(!snap.live);
	CHECK(!snap.video_id.has_value());
	CHECK(!snap.likes.has_value());
	REQUIRE(snap.subscribers.has_value());
	CHECK_EQ(*snap.subscribers, uint64_t{12300});
}

TEST_CASE(upcoming_stream_is_not_live)
{
	const YoutubeLiveSnapshot snap = parse_youtube_live_page(kUpcomingPage);
	CHECK(!snap.live);
	CHECK(!snap.likes.has_value());
	REQUIRE(snap.subscribers.has_value());
	CHECK_EQ(*snap.subscribers, uint64_t{987});
}

TEST_CASE(hidden_likes_are_missing_on_a_live_page)
{
	const YoutubeLiveSnapshot snap = parse_youtube_live_page(kHiddenLikesPage);
	CHECK(snap.live);
	REQUIRE(snap.video_id.has_value());
	CHECK_EQ(*snap.video_id, std::string("Zz_9-AbCdEf"));
	CHECK(!snap.likes.has_value());
	REQUIRE(snap.subscribers.has_value());
	CHECK_EQ(*snap.subscribers, uint64_t{1});
}

TEST_CASE(likes_fall_back_to_accessibility_text)
{
	const YoutubeLiveSnapshot snap = parse_youtube_live_page(kAccessibilityLikesPage);
	CHECK(snap.live);
	REQUIRE(snap.likes.has_value());
	CHECK_EQ(*snap.likes, uint64_t{1234567});
}

TEST_CASE(empty_page_is_not_live)
{
	const YoutubeLiveSnapshot snap = parse_youtube_live_page("");
	CHECK(!snap.live);
	CHECK(!snap.likes.has_value());
	CHECK(!snap.subscribers.has_value());
}

TEST_CASE(subscriber_text_forms)
{
	CHECK_EQ(parse_subscriber_text("15.8 million subscribers").value_or(0), uint64_t{15800000});
	CHECK_EQ(parse_subscriber_text("15.8M subscribers").value_or(0), uint64_t{15800000});
	CHECK_EQ(parse_subscriber_text("12.3K subscribers").value_or(0), uint64_t{12300});
	CHECK_EQ(parse_subscriber_text("12.3 thousand subscribers").value_or(0), uint64_t{12300});
	CHECK_EQ(parse_subscriber_text("1.05 billion subscribers").value_or(0), uint64_t{1050000000});
	CHECK_EQ(parse_subscriber_text("2B subscribers").value_or(0), uint64_t{2000000000});
	CHECK_EQ(parse_subscriber_text("987 subscribers").value_or(0), uint64_t{987});
	CHECK_EQ(parse_subscriber_text("1,234 subscribers").value_or(0), uint64_t{1234});
	CHECK_EQ(parse_subscriber_text("1 subscriber").value_or(0), uint64_t{1});
	CHECK(!parse_subscriber_text("").has_value());
	CHECK(!parse_subscriber_text("subscribers").has_value());
	CHECK(!parse_subscriber_text("lots of fans").has_value());
}

TEST_CASE(channel_input_is_normalized)
{
	const std::string handle = "https://www.youtube.com/@LofiGirl";
	CHECK_EQ(normalize_youtube_channel("@LofiGirl").value_or(""), handle);
	CHECK_EQ(normalize_youtube_channel("  LofiGirl \n").value_or(""), handle);
	CHECK_EQ(normalize_youtube_channel("https://www.youtube.com/@LofiGirl").value_or(""), handle);
	CHECK_EQ(normalize_youtube_channel("youtube.com/@LofiGirl/live").value_or(""), handle);
	CHECK_EQ(normalize_youtube_channel("http://m.youtube.com/@LofiGirl/videos?view=0").value_or(""), handle);
	CHECK_EQ(normalize_youtube_channel("https://www.youtube.com/@LofiGirl/").value_or(""), handle);

	const std::string id = "https://www.youtube.com/channel/UCSJ4gkVC6NrvII8umztf0Ow";
	CHECK_EQ(normalize_youtube_channel("UCSJ4gkVC6NrvII8umztf0Ow").value_or(""), id);
	CHECK_EQ(normalize_youtube_channel("/channel/UCSJ4gkVC6NrvII8umztf0Ow").value_or(""), id);
	CHECK_EQ(normalize_youtube_channel("https://youtube.com/channel/UCSJ4gkVC6NrvII8umztf0Ow/live").value_or(""),
		 id);

	CHECK_EQ(normalize_youtube_channel("/c/LofiGirl").value_or(""), std::string("https://www.youtube.com/c/LofiGirl"));
	CHECK_EQ(normalize_youtube_channel("https://www.youtube.com/user/Someone/featured").value_or(""),
		 std::string("https://www.youtube.com/user/Someone"));
}

TEST_CASE(invalid_channel_input_is_rejected)
{
	CHECK(!normalize_youtube_channel("").has_value());
	CHECK(!normalize_youtube_channel("   ").has_value());
	CHECK(!normalize_youtube_channel("@").has_value());
	CHECK(!normalize_youtube_channel("two words").has_value());
	CHECK(!normalize_youtube_channel("https://twitch.tv/someone").has_value());
	CHECK(!normalize_youtube_channel("https://www.youtube.com/watch?v=4t4AQ15yj1M").has_value());
	CHECK(!normalize_youtube_channel("https://www.youtube.com/channel/").has_value());
}

TEST_CASE(live_url_forces_english)
{
	CHECK_EQ(youtube_live_url("https://www.youtube.com/@LofiGirl"),
		 std::string("https://www.youtube.com/@LofiGirl/live?hl=en"));
}
