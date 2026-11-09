#include "core/goal_settings.hpp"

#include "test_harness.hpp"

#include <string>

using namespace goalbar;

TEST_CASE(settings_round_trip)
{
	GoalSettings settings;
	settings.goal = Goal{1200, 1300, 1250, true};
	settings.metric = Metric::Subscribers;
	settings.platform = Platform::YouTube;
	settings.step = 50;

	const GoalSettings decoded = decode_goal_settings(encode_goal_settings(settings));
	CHECK_EQ(decoded.goal.start, uint64_t{1200});
	CHECK_EQ(decoded.goal.target, uint64_t{1300});
	CHECK_EQ(decoded.goal.current, uint64_t{1250});
	CHECK(decoded.goal.reached_fired);
	CHECK(decoded.metric == Metric::Subscribers);
	CHECK(decoded.platform == Platform::YouTube);
	CHECK_EQ(decoded.step, uint64_t{50});
}

TEST_CASE(settings_encode_uses_stable_ids)
{
	GoalSettings settings;
	settings.platform = Platform::Twitch;
	settings.metric = Metric::Followers;
	const SettingsMap values = encode_goal_settings(settings);
	CHECK(values.count("metric") == 1 && values.at("metric") == "followers");
	CHECK(values.count("platform") == 1 && values.at("platform") == "twitch");
}

TEST_CASE(empty_map_decodes_to_defaults)
{
	const GoalSettings decoded = decode_goal_settings({});
	CHECK_EQ(decoded.goal.start, uint64_t{0});
	CHECK_EQ(decoded.goal.target, kDefaultGoalTarget);
	CHECK_EQ(decoded.goal.current, uint64_t{0});
	CHECK(!decoded.goal.reached_fired);
	CHECK(decoded.metric == Metric::Likes);
	CHECK(decoded.platform == Platform::Manual);
	CHECK_EQ(decoded.step, kDefaultGoalStep);
}

TEST_CASE(malformed_values_fall_back_to_defaults)
{
	const SettingsMap values = {
		{"goal_start", "-5"},
		{"goal_target", "0"},
		{"goal_current", "12abc"},
		{"goal_reached_fired", "maybe"},
		{"metric", "views"},
		{"platform", "tiktok"},
		{"goal_step", "99999999999999999999999"},
	};
	const GoalSettings decoded = decode_goal_settings(values);
	CHECK_EQ(decoded.goal.start, uint64_t{0});
	CHECK_EQ(decoded.goal.target, kDefaultGoalTarget);
	CHECK_EQ(decoded.goal.current, uint64_t{0});
	CHECK(!decoded.goal.reached_fired);
	CHECK(decoded.metric == Metric::Likes);
	CHECK(decoded.platform == Platform::Manual);
	CHECK_EQ(decoded.step, kDefaultGoalStep);
}

TEST_CASE(zero_step_falls_back_to_default)
{
	const GoalSettings decoded = decode_goal_settings({{"goal_step", "0"}});
	CHECK_EQ(decoded.step, kDefaultGoalStep);
}
