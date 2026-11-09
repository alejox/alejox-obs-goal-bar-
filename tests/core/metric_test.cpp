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
