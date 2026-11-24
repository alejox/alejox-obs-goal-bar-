#include "core/metric.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar;

TEST_CASE(metric_ids_round_trip)
{
	CHECK_EQ(std::string(metric_id(Metric::Likes)), std::string("likes"));
	CHECK_EQ(std::string(metric_id(Metric::Subscribers)), std::string("subscribers"));
	CHECK_EQ(std::string(metric_id(Metric::Followers)), std::string("followers"));
	CHECK_EQ(std::string(metric_id(Metric::Viewers)), std::string("viewers"));
	for (Metric metric : {Metric::Likes, Metric::Subscribers, Metric::Followers, Metric::Viewers}) {
		CHECK(metric_from_id(metric_id(metric)) == metric);
	}
}

TEST_CASE(platform_ids_round_trip)
{
	CHECK_EQ(std::string(platform_id(Platform::Manual)), std::string("manual"));
	CHECK_EQ(std::string(platform_id(Platform::YouTube)), std::string("youtube"));
	CHECK_EQ(std::string(platform_id(Platform::Twitch)), std::string("twitch"));
	CHECK_EQ(std::string(platform_id(Platform::Kick)), std::string("kick"));
	for (Platform platform : {Platform::Manual, Platform::YouTube, Platform::Twitch, Platform::Kick}) {
		CHECK(platform_from_id(platform_id(platform)) == platform);
	}
}

TEST_CASE(unknown_ids_are_rejected)
{
	CHECK(!metric_from_id("views").has_value());
	CHECK(!metric_from_id("").has_value());
	CHECK(!metric_from_id("Likes").has_value());
	CHECK(!platform_from_id("Kick").has_value());
	CHECK(!metric_from_id("Viewers").has_value());
	CHECK(!platform_from_id("").has_value());
}

TEST_CASE(platforms_read_only_their_metrics)
{
	CHECK(platform_reads(Platform::YouTube, Metric::Likes));
	CHECK(platform_reads(Platform::YouTube, Metric::Subscribers));
	CHECK(!platform_reads(Platform::YouTube, Metric::Viewers));
	CHECK(platform_reads(Platform::Twitch, Metric::Followers));
	CHECK(platform_reads(Platform::Twitch, Metric::Subscribers));
	CHECK(platform_reads(Platform::Kick, Metric::Followers));
	CHECK(platform_reads(Platform::Kick, Metric::Viewers));
	CHECK(!platform_reads(Platform::Kick, Metric::Subscribers));
	CHECK(!platform_reads(Platform::Kick, Metric::Likes));
	CHECK(platform_reads(Platform::Manual, Metric::Subscribers));
	CHECK(!platform_reads(Platform::Manual, Metric::Viewers));
}

TEST_CASE(unreadable_metrics_fall_back_to_the_platform_default)
{
	// A Kick source saved with subscribers (T11) now reads followers.
	CHECK(readable_metric(Platform::Kick, Metric::Subscribers) == Metric::Followers);
	CHECK(readable_metric(Platform::Kick, Metric::Viewers) == Metric::Viewers);
	CHECK(readable_metric(Platform::Twitch, Metric::Likes) == Metric::Followers);
	CHECK(readable_metric(Platform::YouTube, Metric::Viewers) == Metric::Likes);
	CHECK(readable_metric(Platform::Manual, Metric::Viewers) == Metric::Likes);
	CHECK(readable_metric(Platform::Manual, Metric::Followers) == Metric::Followers);
}

// Bits and comments are counted from the live chat.
TEST_CASE(chat_metrics_have_stable_ids)
{
	CHECK_EQ(std::string(metric_id(Metric::Bits)), std::string("bits"));
	CHECK_EQ(std::string(metric_id(Metric::Comments)), std::string("comments"));
	CHECK(metric_from_id("bits") == Metric::Bits);
	CHECK(metric_from_id("comments") == Metric::Comments);
	CHECK(metric_is_counted(Metric::Bits));
	CHECK(metric_is_counted(Metric::Comments));
	CHECK(!metric_is_counted(Metric::Followers));
	CHECK(!metric_is_counted(Metric::Likes));
}

TEST_CASE(chat_metrics_by_platform)
{
	CHECK(platform_reads(Platform::Twitch, Metric::Bits));
	CHECK(platform_reads(Platform::Twitch, Metric::Comments));
	CHECK(platform_reads(Platform::YouTube, Metric::Comments));
	CHECK(platform_reads(Platform::Kick, Metric::Comments));
	CHECK(!platform_reads(Platform::YouTube, Metric::Bits));
	CHECK(!platform_reads(Platform::Kick, Metric::Bits));
	// Manual mode types the value: counting from a chat makes no sense there.
	CHECK(!platform_reads(Platform::Manual, Metric::Bits));
	CHECK(!platform_reads(Platform::Manual, Metric::Comments));
	CHECK(readable_metric(Platform::Kick, Metric::Bits) == Metric::Followers);
	CHECK(readable_metric(Platform::Manual, Metric::Comments) == Metric::Likes);
}

// Paid subscriptions are counted from the chat where the platform does not
// publish a total: YouTube memberships and Kick subscriptions. Twitch has the
// real total (Subscribers) through its API.
TEST_CASE(paid_subscriptions_metric)
{
	CHECK_EQ(std::string(metric_id(Metric::PaidSubs)), std::string("paid_subs"));
	CHECK(metric_from_id("paid_subs") == Metric::PaidSubs);
	CHECK(metric_is_counted(Metric::PaidSubs));
	CHECK(platform_reads(Platform::YouTube, Metric::PaidSubs));
	CHECK(platform_reads(Platform::Kick, Metric::PaidSubs));
	CHECK(!platform_reads(Platform::Twitch, Metric::PaidSubs));
	CHECK(!platform_reads(Platform::Manual, Metric::PaidSubs));
}

// TikTok: followers and viewers from its room endpoint; likes (the room's
// total), comments and diamonds (gifts) from its live chat.
TEST_CASE(tiktok_metrics)
{
	CHECK_EQ(std::string(platform_id(Platform::TikTok)), std::string("tiktok"));
	CHECK(platform_from_id("tiktok") == Platform::TikTok);
	CHECK_EQ(std::string(metric_id(Metric::Diamonds)), std::string("diamonds"));
	CHECK(metric_from_id("diamonds") == Metric::Diamonds);
	CHECK(metric_is_counted(Metric::Diamonds));

	for (Metric metric : {Metric::Followers, Metric::Viewers, Metric::Likes, Metric::Comments, Metric::Diamonds})
		CHECK(platform_reads(Platform::TikTok, metric));
	CHECK(!platform_reads(Platform::TikTok, Metric::Subscribers));
	CHECK(!platform_reads(Platform::TikTok, Metric::Bits));
	CHECK(!platform_reads(Platform::TikTok, Metric::PaidSubs));
	CHECK(!platform_reads(Platform::YouTube, Metric::Diamonds));
	CHECK(!platform_reads(Platform::Manual, Metric::Diamonds));
	CHECK(readable_metric(Platform::TikTok, Metric::Subscribers) == Metric::Followers);

	// Likes on TikTok come from the chat but are a total, not counted.
	CHECK(metric_from_chat(Platform::TikTok, Metric::Likes));
	CHECK(!metric_is_counted(Metric::Likes));
	CHECK(!metric_from_chat(Platform::YouTube, Metric::Likes));
	CHECK(metric_from_chat(Platform::TikTok, Metric::Diamonds));
	CHECK(metric_from_chat(Platform::Kick, Metric::Comments));
	CHECK(!metric_from_chat(Platform::TikTok, Metric::Followers));
	CHECK(!metric_from_chat(Platform::TikTok, Metric::Viewers));
}
