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
